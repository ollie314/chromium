// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/ntp_snippets/ntp_snippets_service.h"

#include "base/json/json_reader.h"
#include "base/macros.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "components/ntp_snippets/ntp_snippet.h"
#include "components/ntp_snippets/ntp_snippets_fetcher.h"
#include "components/prefs/testing_pref_service.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace ntp_snippets {

namespace {

const base::Time::Exploded kDefaultCreationTime = {2015, 11, 4, 25, 13, 46, 45};

base::Time GetDefaultCreationTime() {
  return base::Time::FromUTCExploded(kDefaultCreationTime);
}

std::string GetTestJson(const std::string& content_creation_time_str,
                        const std::string& expiry_time_str) {
  char json_str_format[] =
      "{ \"recos\": [ "
      "{ \"contentInfo\": {"
      "\"url\" : \"http://localhost/foobar\","
      "\"site_title\" : \"Site Title\","
      "\"favicon_url\" : \"http://localhost/favicon\","
      "\"title\" : \"Title\","
      "\"snippet\" : \"Snippet\","
      "\"thumbnailUrl\" : \"http://localhost/salient_image\","
      "\"creationTimestampSec\" : \"%s\","
      "\"expiryTimestampSec\" : \"%s\","
      "\"sourceCorpusInfo\" : [ "
      "{\"ampUrl\" : \"http://localhost/amp\"},"
      "{\"corpusId\" : \"id\"}]"
      "}}"
      "]}";

  return base::StringPrintf(json_str_format, content_creation_time_str.c_str(),
                            expiry_time_str.c_str());
}

std::string GetTestJson(const std::string& content_creation_time_str) {
  base::Time expiry_time = base::Time::Now() + base::TimeDelta::FromHours(1);
  return GetTestJson(content_creation_time_str,
                     NTPSnippet::TimeToJsonString(expiry_time));
}

std::string GetTestJson() {
  return GetTestJson(NTPSnippet::TimeToJsonString(GetDefaultCreationTime()));
}

std::string GetTestExpiredJson() {
  return GetTestJson(NTPSnippet::TimeToJsonString(GetDefaultCreationTime()),
                     NTPSnippet::TimeToJsonString(base::Time::Now()));
}

std::string GetInvalidJson() {
  std::string json_str = GetTestJson();
  // Make the json invalid by removing the final closing brace.
  return json_str.substr(0, json_str.size() - 1);
}

std::string GetIncompleteJson() {
  std::string json_str = GetTestJson();
  // Rename the "url" entry. The result is syntactically valid json that will
  // fail to parse as snippets.
  size_t pos = json_str.find("\"url\"");
  if (pos == std::string::npos) {
    NOTREACHED();
    return std::string();
  }
  json_str[pos + 1] = 'x';
  return json_str;
}

void ParseJson(
    bool expect_success,
    const std::string& json,
    const ntp_snippets::NTPSnippetsService::SuccessCallback& success_callback,
    const ntp_snippets::NTPSnippetsService::ErrorCallback& error_callback) {
  base::JSONReader json_reader;
  scoped_ptr<base::Value> value = json_reader.ReadToValue(json);
  bool success = !!value;
  EXPECT_EQ(expect_success, success);
  if (value) {
    success_callback.Run(std::move(value));
  } else {
    error_callback.Run(json_reader.GetErrorMessage());
  }
}

}  // namespace

class NTPSnippetsServiceTest : public testing::Test {
 public:
  NTPSnippetsServiceTest()
      : pref_service_(new TestingPrefServiceSimple()) {}
  ~NTPSnippetsServiceTest() override {}

  void SetUp() override {
    NTPSnippetsService::RegisterProfilePrefs(pref_service_->registry());

    CreateSnippetsService();
  }

  void CreateSnippetsService() {
    scoped_refptr<base::SingleThreadTaskRunner> task_runner(
        base::ThreadTaskRunnerHandle::Get());
    scoped_refptr<net::TestURLRequestContextGetter> request_context_getter =
        new net::TestURLRequestContextGetter(task_runner.get());

    service_.reset(new NTPSnippetsService(
        pref_service_.get(), nullptr, task_runner, std::string("fr"), nullptr,
        make_scoped_ptr(new NTPSnippetsFetcher(
            task_runner, std::move(request_context_getter), true)),
        base::Bind(&ParseJson, true)));
    service_->Init(true);
  }

 protected:
  NTPSnippetsService* service() {
    return service_.get();
  }

  void LoadFromJSONString(const std::string& json) {
    service_->OnSnippetsDownloaded(json);
  }

  void SetExpectJsonParseSuccess(bool expect_success) {
    service_->parse_json_callback_ = base::Bind(&ParseJson, expect_success);
  }

 private:
  base::MessageLoop message_loop_;
  scoped_ptr<TestingPrefServiceSimple> pref_service_;
  scoped_ptr<NTPSnippetsService> service_;

  DISALLOW_COPY_AND_ASSIGN(NTPSnippetsServiceTest);
};

TEST_F(NTPSnippetsServiceTest, Loop) {
  std::string json_str(
      "{ \"recos\": [ "
      "{ \"contentInfo\": { \"url\" : \"http://localhost/foobar\" }}"
      "]}");
  LoadFromJSONString(json_str);

  // The same for loop without the '&' should not compile.
  for (auto& snippet : *service()) {
    // Snippet here is a const.
    EXPECT_EQ(snippet.url(), GURL("http://localhost/foobar"));
  }
  // Without the const, this should not compile.
  for (const NTPSnippet& snippet : *service()) {
    EXPECT_EQ(snippet.url(), GURL("http://localhost/foobar"));
  }
}

TEST_F(NTPSnippetsServiceTest, Full) {
  std::string json_str(GetTestJson());

  LoadFromJSONString(json_str);
  EXPECT_EQ(service()->size(), 1u);

  // The same for loop without the '&' should not compile.
  for (auto& snippet : *service()) {
    // Snippet here is a const.
    EXPECT_EQ(snippet.url(), GURL("http://localhost/foobar"));
    EXPECT_EQ(snippet.site_title(), "Site Title");
    EXPECT_EQ(snippet.favicon_url(), GURL("http://localhost/favicon"));
    EXPECT_EQ(snippet.title(), "Title");
    EXPECT_EQ(snippet.snippet(), "Snippet");
    EXPECT_EQ(snippet.salient_image_url(),
              GURL("http://localhost/salient_image"));
    EXPECT_EQ(GetDefaultCreationTime(), snippet.publish_date());
    EXPECT_EQ(snippet.amp_url(), GURL("http://localhost/amp"));
  }
}

TEST_F(NTPSnippetsServiceTest, Clear) {
  std::string json_str(GetTestJson());

  LoadFromJSONString(json_str);
  EXPECT_EQ(service()->size(), 1u);

  service()->ClearSnippets();
  EXPECT_EQ(service()->size(), 0u);
}

TEST_F(NTPSnippetsServiceTest, LoadInvalidJson) {
  SetExpectJsonParseSuccess(false);
  LoadFromJSONString(GetInvalidJson());
  EXPECT_EQ(service()->size(), 0u);
}

TEST_F(NTPSnippetsServiceTest, LoadInvalidJsonWithExistingSnippets) {
  LoadFromJSONString(GetTestJson());
  ASSERT_EQ(service()->size(), 1u);

  SetExpectJsonParseSuccess(false);
  LoadFromJSONString(GetInvalidJson());
  // This should not have changed the existing snippets.
  EXPECT_EQ(service()->size(), 1u);
}

TEST_F(NTPSnippetsServiceTest, LoadIncompleteJson) {
  LoadFromJSONString(GetIncompleteJson());
  EXPECT_EQ(service()->size(), 0u);
}

TEST_F(NTPSnippetsServiceTest, LoadIncompleteJsonWithExistingSnippets) {
  LoadFromJSONString(GetTestJson());
  ASSERT_EQ(service()->size(), 1u);

  LoadFromJSONString(GetIncompleteJson());
  // This should not have changed the existing snippets.
  EXPECT_EQ(service()->size(), 1u);
}

TEST_F(NTPSnippetsServiceTest, Discard) {
  std::string json_str(
      "{ \"recos\": [ { \"contentInfo\": { \"url\" : \"http://site.com\" }}]}");
  LoadFromJSONString(json_str);

  ASSERT_EQ(1u, service()->size());

  // Discarding a non-existent snippet shouldn't do anything.
  EXPECT_FALSE(service()->DiscardSnippet(GURL("http://othersite.com")));
  EXPECT_EQ(1u, service()->size());

  // Discard the snippet.
  EXPECT_TRUE(service()->DiscardSnippet(GURL("http://site.com")));
  EXPECT_EQ(0u, service()->size());

  // Make sure that fetching the same snippet again does not re-add it.
  LoadFromJSONString(json_str);
  EXPECT_EQ(0u, service()->size());

  // The snippet should stay discarded even after re-creating the service.
  CreateSnippetsService();
  LoadFromJSONString(json_str);
  EXPECT_EQ(0u, service()->size());

  // The snippet can be added again after clearing discarded snippets.
  service()->ClearDiscardedSnippets();
  EXPECT_EQ(0u, service()->size());
  LoadFromJSONString(json_str);
  EXPECT_EQ(1u, service()->size());
}

TEST_F(NTPSnippetsServiceTest, GetDiscarded) {
  std::string json_str(
      "{ \"recos\": [ { \"contentInfo\": { \"url\" : \"http://site.com\" }}]}");
  LoadFromJSONString(json_str);

  // For the test, we need the snippet to get discarded.
  ASSERT_TRUE(service()->DiscardSnippet(GURL("http://site.com")));
  const NTPSnippetsService::NTPSnippetStorage& snippets =
      service()->discarded_snippets();
  EXPECT_EQ(1u, snippets.size());
  for (auto& snippet : snippets) {
    EXPECT_EQ(GURL("http://site.com"), snippet->url());
  }

  // There should be no discarded snippet after clearing the list.
  service()->ClearDiscardedSnippets();
  EXPECT_EQ(0u, service()->discarded_snippets().size());
}

TEST_F(NTPSnippetsServiceTest, CreationTimestampParseFail) {
  std::string json_str(GetTestJson("aaa1448459205"));

  LoadFromJSONString(json_str);
  EXPECT_EQ(service()->size(), 1u);

  // The same for loop without the '&' should not compile.
  for (auto& snippet : *service()) {
    // Snippet here is a const.
    EXPECT_EQ(snippet.url(), GURL("http://localhost/foobar"));
    EXPECT_EQ(snippet.title(), "Title");
    EXPECT_EQ(snippet.snippet(), "Snippet");
    EXPECT_EQ(base::Time::UnixEpoch(), snippet.publish_date());
  }
}

TEST_F(NTPSnippetsServiceTest, RemoveExpiredContent) {
  std::string json_str(GetTestExpiredJson());

  LoadFromJSONString(json_str);
  EXPECT_EQ(service()->size(), 0u);
}

}  // namespace ntp_snippets

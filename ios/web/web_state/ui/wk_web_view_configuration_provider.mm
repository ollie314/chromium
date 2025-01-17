// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/web_state/ui/wk_web_view_configuration_provider.h"

#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>

#include "base/ios/ios_util.h"
#import "base/ios/weak_nsobject.h"
#include "base/logging.h"
#include "ios/web/public/browser_state.h"
#import "ios/web/web_state/js/page_script_util.h"
#import "ios/web/web_state/ui/crw_wk_script_message_router.h"

namespace web {

namespace {
// A key used to associate a WKWebViewConfigurationProvider with a BrowserState.
const char kWKWebViewConfigProviderKeyName[] = "wk_web_view_config_provider";

// Returns an autoreleased instance of WKUserScript to be added to
// configuration's userContentController.
WKUserScript* InternalGetEarlyPageScript() {
  return [[[WKUserScript alloc]
        initWithSource:GetEarlyPageScript()
         injectionTime:WKUserScriptInjectionTimeAtDocumentStart
      forMainFrameOnly:YES] autorelease];
}

}  // namespace

// static
WKWebViewConfigurationProvider&
WKWebViewConfigurationProvider::FromBrowserState(BrowserState* browser_state) {
  DCHECK([NSThread isMainThread]);
  DCHECK(browser_state);
  if (!browser_state->GetUserData(kWKWebViewConfigProviderKeyName)) {
    bool is_off_the_record = browser_state->IsOffTheRecord();
    browser_state->SetUserData(
        kWKWebViewConfigProviderKeyName,
        new WKWebViewConfigurationProvider(is_off_the_record));
  }
  return *(static_cast<WKWebViewConfigurationProvider*>(
      browser_state->GetUserData(kWKWebViewConfigProviderKeyName)));
}

WKWebViewConfigurationProvider::WKWebViewConfigurationProvider(
    bool is_off_the_record)
    : is_off_the_record_(is_off_the_record) {}

WKWebViewConfigurationProvider::~WKWebViewConfigurationProvider() {
}

WKWebViewConfiguration*
WKWebViewConfigurationProvider::GetWebViewConfiguration() {
  DCHECK([NSThread isMainThread]);
  if (!configuration_) {
    configuration_.reset([[WKWebViewConfiguration alloc] init]);
    if (is_off_the_record_) {
      [configuration_
          setWebsiteDataStore:[WKWebsiteDataStore nonPersistentDataStore]];
    }
// TODO(crbug.com/620878) Remove these guards after moving to iOS10 SDK.
#if defined(__IPHONE_10_0) && __IPHONE_OS_VERSION_MAX_ALLOWED >= __IPHONE_10_0
    if (base::ios::IsRunningOnIOS10OrLater()) {
      [configuration_ setDataDetectorTypes:WKDataDetectorTypeCalendarEvent |
                                           WKDataDetectorTypeFlightNumber];
    }
#endif
    // API available on iOS 9, although doesn't appear to enable inline playback
    // Works as intended on iOS 10+
    [configuration_ setAllowsInlineMediaPlayback:YES];
    // setJavaScriptCanOpenWindowsAutomatically is required to support popups.
    [[configuration_ preferences] setJavaScriptCanOpenWindowsAutomatically:YES];
    [[configuration_ userContentController]
        addUserScript:InternalGetEarlyPageScript()];
  }
  // Prevent callers from changing the internals of configuration.
  return [[configuration_ copy] autorelease];
}

CRWWKScriptMessageRouter*
WKWebViewConfigurationProvider::GetScriptMessageRouter() {
  DCHECK([NSThread isMainThread]);
  if (!router_) {
    WKUserContentController* userContentController =
        [GetWebViewConfiguration() userContentController];
    router_.reset([[CRWWKScriptMessageRouter alloc]
        initWithUserContentController:userContentController]);
  }
  return router_;
}

void WKWebViewConfigurationProvider::Purge() {
  DCHECK([NSThread isMainThread]);
#if !defined(NDEBUG) || !defined(DCHECK_ALWAYS_ON)  // Matches DCHECK_IS_ON.
  base::WeakNSObject<id> weak_configuration(configuration_);
  base::WeakNSObject<id> weak_router(router_);
  base::WeakNSObject<id> weak_process_pool([configuration_ processPool]);
#endif  // !defined(NDEBUG) || defined(DCHECK_ALWAYS_ON)
  configuration_.reset();
  router_.reset();
  // Make sure that no one retains configuration, router, processPool.
  DCHECK(!weak_configuration);
  DCHECK(!weak_router);
  // TODO(crbug.com/522672): Enable this DCHECK.
  // DCHECK(!weak_process_pool);
}

}  // namespace web

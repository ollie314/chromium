// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBRARIES_NACL_IO_TEST_FAKE_UTIL_H_
#define LIBRARIES_NACL_IO_TEST_FAKE_UTIL_H_

#include <string>

#include <ppapi/c/pp_completion_callback.h>

#include "fake_ppapi/fake_filesystem.h"
#include "fake_ppapi/fake_resource_manager.h"

class FakeFileRefResource : public FakeResource {
 public:
  FakeFileRefResource() : filesystem(NULL) {}
  static const char* classname() { return "FakeFileRefResource"; }

  FakeFilesystem* filesystem;  // Weak reference.
  FakeFilesystem::Path path;
  std::string contents;
};

class FakeFileSystemResource : public FakeResource {
 public:
  FakeFileSystemResource() : filesystem(NULL), opened(false) {}
  ~FakeFileSystemResource() { delete filesystem; }
  static const char* classname() { return "FakeFileSystemResource"; }

  FakeFilesystem* filesystem;  // Owned.
  bool opened;
};

class FakeHtml5FsResource : public FakeResource {
 public:
  FakeHtml5FsResource() : filesystem_template(NULL) {}
  static const char* classname() { return "FakeHtml5FsResource"; }

  FakeFilesystem* filesystem_template;  // Weak reference.
};

int32_t RunCompletionCallback(PP_CompletionCallback* callback, int32_t result);

#endif  // LIBRARIES_NACL_IO_TEST_FAKE_UTIL_H_

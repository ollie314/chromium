// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/public/provider/chrome/browser/omaha/omaha_service_provider.h"

OmahaServiceProvider::OmahaServiceProvider() {}

OmahaServiceProvider::~OmahaServiceProvider() {}

GURL OmahaServiceProvider::GetUpdateServerURL() const {
  return GURL();
}

std::string OmahaServiceProvider::GetApplicationID() const {
  return std::string();
}

std::string OmahaServiceProvider::GetBrandCode() const {
  return std::string();
}

void OmahaServiceProvider::AppendExtraAttributes(const std::string& tag,
                                                 OmahaXmlWriter* writer) const {
}

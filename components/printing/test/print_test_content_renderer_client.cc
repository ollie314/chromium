// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/printing/test/print_test_content_renderer_client.h"

#include "base/memory/ptr_util.h"
#include "components/printing/renderer/print_web_view_helper.h"
#include "printing/features/features.h"
#include "third_party/WebKit/public/web/WebElement.h"

namespace printing {

namespace {

class PrintWebViewHelperDelegate : public PrintWebViewHelper::Delegate {
 public:
  ~PrintWebViewHelperDelegate() override {}
  bool CancelPrerender(content::RenderView* render_view,
                       int routing_id) override {
    return false;
  }
  blink::WebElement GetPdfElement(blink::WebLocalFrame* frame) override {
    return blink::WebElement();
  }
  bool IsPrintPreviewEnabled() override {
#if BUILDFLAG(ENABLE_PRINT_PREVIEW)
    return true;
#else
    return false;
#endif
  }
  bool OverridePrint(blink::WebLocalFrame* frame) override { return false; }
};

}  // namespace

PrintTestContentRendererClient::PrintTestContentRendererClient() {
}

PrintTestContentRendererClient::~PrintTestContentRendererClient() {
}

void PrintTestContentRendererClient::RenderViewCreated(
    content::RenderView* render_view) {
  new printing::PrintWebViewHelper(
      render_view, base::MakeUnique<PrintWebViewHelperDelegate>());
}

}  // namespace printing

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/webui/vr_shell/vr_shell_ui_message_handler.h"

#include <memory>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/callback.h"
#include "base/values.h"
#include "chrome/browser/android/vr_shell/vr_shell.h"
#include "content/public/browser/web_ui.h"

VrShellUIMessageHandler::VrShellUIMessageHandler() {}

VrShellUIMessageHandler::~VrShellUIMessageHandler() {}

void VrShellUIMessageHandler::RegisterMessages() {
  vr_shell_ = vr_shell::VrShell::GetWeakPtr();

  web_ui()->RegisterMessageCallback(
      "domLoaded", base::Bind(&VrShellUIMessageHandler::HandleDomLoaded,
                              base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "updateScene", base::Bind(&VrShellUIMessageHandler::HandleUpdateScene,
                            base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "doAction", base::Bind(&VrShellUIMessageHandler::HandleDoAction,
                             base::Unretained(this)));
}

void VrShellUIMessageHandler::HandleDomLoaded(const base::ListValue* args) {
  if (!vr_shell_)
    return;

  vr_shell_->OnDomContentsLoaded();

  // TODO(bshe): Get size from native side directly.
  CHECK_EQ(2u, args->GetSize());
  int width, height;
  CHECK(args->GetInteger(0, &width));
  CHECK(args->GetInteger(1, &height));
  vr_shell_->SetUiTextureSize(width, height);
}

void VrShellUIMessageHandler::HandleUpdateScene(const base::ListValue* args) {
  if (!vr_shell_)
    return;

  // Copy the update instructions and handle them on the render thread.
  auto cb = base::Bind(&vr_shell::UiScene::HandleCommands,
                       base::Unretained(vr_shell_->GetScene()),
                       base::Owned(args->CreateDeepCopy().release()),
                       vr_shell::UiScene::TimeInMicroseconds());
  vr_shell_->QueueTask(cb);
}

void VrShellUIMessageHandler::HandleDoAction(const base::ListValue* args) {
  NOTIMPLEMENTED();
}

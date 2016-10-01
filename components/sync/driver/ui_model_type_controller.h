// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_UI_MODEL_TYPE_CONTROLLER_H_
#define COMPONENTS_SYNC_DRIVER_UI_MODEL_TYPE_CONTROLLER_H_

#include "components/sync/driver/non_blocking_data_type_controller.h"

namespace syncer {

class SyncClient;

// Implementation for Unified Sync and Storage datatypes that reside on the UI
// thread.
class UIModelTypeController : public NonBlockingDataTypeController {
 public:
  // |dump_stack| is called when an unrecoverable error occurs.
  UIModelTypeController(ModelType type,
                        const base::Closure& dump_stack,
                        SyncClient* sync_client);
  ~UIModelTypeController() override;

 private:
  // NonBlockingDataTypeController implementations.
  // Since this is UI model type controller, we hide this function here.
  bool RunOnModelThread(const tracked_objects::Location& from_here,
                        const base::Closure& task) override;

  DISALLOW_COPY_AND_ASSIGN(UIModelTypeController);
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_DRIVER_UI_MODEL_TYPE_CONTROLLER_H_

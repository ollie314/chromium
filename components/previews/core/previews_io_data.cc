// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/previews/core/previews_io_data.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/files/file_path.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/sequenced_task_runner.h"
#include "base/time/default_clock.h"
#include "components/previews/core/previews_black_list.h"
#include "components/previews/core/previews_opt_out_store.h"
#include "components/previews/core/previews_ui_service.h"

namespace previews {

PreviewsIOData::PreviewsIOData(
    const scoped_refptr<base::SingleThreadTaskRunner>& ui_task_runner,
    const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner)
    : ui_task_runner_(ui_task_runner),
      io_task_runner_(io_task_runner),
      weak_factory_(this) {}

PreviewsIOData::~PreviewsIOData() {}

void PreviewsIOData::Initialize(
    base::WeakPtr<PreviewsUIService> previews_ui_service,
    std::unique_ptr<PreviewsOptOutStore> previews_opt_out_store) {
  DCHECK(ui_task_runner_->BelongsToCurrentThread());
  previews_ui_service_ = previews_ui_service;

  // Set up the IO thread portion of |this|.
  io_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&PreviewsIOData::InitializeOnIOThread, base::Unretained(this),
                 base::Passed(&previews_opt_out_store)));
}

void PreviewsIOData::InitializeOnIOThread(
    std::unique_ptr<PreviewsOptOutStore> previews_opt_out_store) {
  DCHECK(io_task_runner_->BelongsToCurrentThread());
  previews_black_list_.reset(
      new PreviewsBlackList(std::move(previews_opt_out_store),
                            base::MakeUnique<base::DefaultClock>()));
  ui_task_runner_->PostTask(
      FROM_HERE, base::Bind(&PreviewsUIService::SetIOData, previews_ui_service_,
                            weak_factory_.GetWeakPtr()));
}

}  // namespace previews

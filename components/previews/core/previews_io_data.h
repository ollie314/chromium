// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_PREVIEWS_CORE_PREVIEWS_IO_DATA_H_
#define COMPONENTS_PREVIEWS_CORE_PREVIEWS_IO_DATA_H_

#include <memory>
#include <string>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "components/previews/core/previews_opt_out_store.h"

namespace previews {
class PreviewsBlackList;
class PreviewsUIService;

// A class to manage the IO portion of inter-thread communication between
// previews/ objects. Created on the UI thread, but used only on the IO thread
// after initialization.
class PreviewsIOData {
 public:
  PreviewsIOData(
      const scoped_refptr<base::SingleThreadTaskRunner>& ui_task_runner,
      const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner);
  virtual ~PreviewsIOData();

  // Stores |previews_ui_service| as |previews_ui_service_| and posts a task to
  // InitializeOnIOThread on the IO thread.
  void Initialize(base::WeakPtr<PreviewsUIService> previews_ui_service,
                  std::unique_ptr<PreviewsOptOutStore> previews_opt_out_store);

  PreviewsBlackList* black_list() const { return previews_black_list_.get(); }

 protected:
  // Posts a task to SetIOData for |previews_ui_service_| on the UI thread with
  // a weak pointer to |this|. Virtualized for testing.
  virtual void InitializeOnIOThread(
      std::unique_ptr<PreviewsOptOutStore> previews_opt_out_store);

 private:
  // The UI thread portion of the inter-thread communication for previews.
  base::WeakPtr<PreviewsUIService> previews_ui_service_;

  std::unique_ptr<PreviewsBlackList> previews_black_list_;

  // The UI and IO thread task runners. |ui_task_runner_| is used to post
  // tasks to |previews_ui_service_|, and |io_task_runner_| is used to post from
  // Initialize to InitializeOnIOThread as well as verify that execution is
  // happening on the IO thread.
  scoped_refptr<base::SingleThreadTaskRunner> ui_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;

  base::WeakPtrFactory<PreviewsIOData> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(PreviewsIOData);
};

}  // namespace previews

#endif  // COMPONENTS_PREVIEWS_CORE_PREVIEWS_IO_DATA_H_

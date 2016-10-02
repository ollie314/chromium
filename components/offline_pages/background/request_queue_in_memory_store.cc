// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/offline_pages/background/request_queue_in_memory_store.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/threading/thread_task_runner_handle.h"
#include "components/offline_pages/background/save_page_request.h"

namespace offline_pages {

RequestQueueInMemoryStore::RequestQueueInMemoryStore() {}

RequestQueueInMemoryStore::~RequestQueueInMemoryStore() {}

void RequestQueueInMemoryStore::GetRequests(
    const GetRequestsCallback& callback) {
  std::vector<std::unique_ptr<SavePageRequest>> result_requests;
  for (const auto& id_request_pair : requests_) {
    std::unique_ptr<SavePageRequest> request(
        new SavePageRequest(id_request_pair.second));
    result_requests.push_back(std::move(request));
  }
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(callback, true, base::Passed(std::move(result_requests))));
}

void RequestQueueInMemoryStore::AddRequest(const SavePageRequest& request,
                                           const AddCallback& callback) {
  RequestsMap::iterator iter = requests_.find(request.request_id());
  ItemActionStatus status;
  if (iter == requests_.end()) {
    requests_.insert(iter, std::make_pair(request.request_id(), request));
    status = ItemActionStatus::SUCCESS;
  } else {
    status = ItemActionStatus::ALREADY_EXISTS;
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                base::Bind(callback, status));
}

void RequestQueueInMemoryStore::UpdateRequests(
    const std::vector<SavePageRequest>& requests,
    const RequestQueue::UpdateCallback& callback) {
  std::unique_ptr<UpdateRequestsResult> result(
      new UpdateRequestsResult(state()));

  ItemActionStatus status;
  for (const auto& request : requests) {
    RequestsMap::iterator iter = requests_.find(request.request_id());
    if (iter != requests_.end()) {
      status = ItemActionStatus::SUCCESS;
      iter->second = request;
      result->updated_items.push_back(request);
    } else {
      status = ItemActionStatus::NOT_FOUND;
    }
    result->item_statuses.push_back(
        std::make_pair(request.request_id(), status));
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::Bind(callback, base::Passed(&result)));
}

void RequestQueueInMemoryStore::RemoveRequests(
    const std::vector<int64_t>& request_ids,
    const RemoveCallback& callback) {
  RequestQueue::UpdateMultipleRequestResults results;
  RequestQueue::UpdateRequestResult result;
  std::vector<std::unique_ptr<SavePageRequest>> requests;
  RequestsMap::iterator iter;

  // If we find a request, mark it as succeeded, and put it in the request list.
  // Otherwise mark it as failed.
  for (auto request_id : request_ids) {
    iter = requests_.find(request_id);
    if (iter != requests_.end()) {
      std::unique_ptr<SavePageRequest> request(
          new SavePageRequest(iter->second));
      requests_.erase(iter);
      result = RequestQueue::UpdateRequestResult::SUCCESS;
      requests.push_back(std::move(request));
    } else {
      result = RequestQueue::UpdateRequestResult::REQUEST_DOES_NOT_EXIST;
    }
    results.push_back(std::make_pair(request_id, result));
  }

  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE,
      base::Bind(callback, results, base::Passed(std::move(requests))));
}

void RequestQueueInMemoryStore::Reset(const ResetCallback& callback) {
  requests_.clear();
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE,
                                                base::Bind(callback, true));
}

StoreState RequestQueueInMemoryStore::state() const {
  return StoreState::LOADED;
}

}  // namespace offline_pages

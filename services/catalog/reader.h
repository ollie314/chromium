// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_CATALOG_READER_H_
#define SERVICES_CATALOG_READER_H_

#include "base/callback.h"
#include "base/files/file_path.h"
#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "base/memory/weak_ptr.h"
#include "services/catalog/types.h"
#include "services/shell/public/interfaces/shell_resolver.mojom.h"

namespace base {
class TaskRunner;
}

namespace catalog {

class Entry;
class ManifestProvider;

// Responsible for loading manifests & building the Entry data structures.
class Reader {
 public:
   using ReadManifestCallback =
      base::Callback<void(scoped_ptr<Entry>)>;
   using CreateEntryForNameCallback =
      base::Callback<void(shell::mojom::ResolveResultPtr)>;

  Reader(base::TaskRunner* file_task_runner,
         ManifestProvider* manifest_provider);
  ~Reader();

  // Scans the contents of |package_dir|, reading all application manifests and
  // populating |cache|. Runs |read_complete_closure| when done.
  void Read(const base::FilePath& package_dir,
            EntryCache* cache,
            const base::Closure& read_complete_closure);

  // Returns an Entry for |mojo_name| via |callback|, assuming a manifest file
  // in the canonical location
  void CreateEntryForName(
      const std::string& mojo_name,
      EntryCache* cache,
      const CreateEntryForNameCallback& entry_created_callback);

 private:
  void OnReadManifest(EntryCache* cache,
                      const CreateEntryForNameCallback& entry_created_callback,
                      scoped_ptr<Entry> entry);

  base::FilePath system_package_dir_;
  base::TaskRunner* file_task_runner_;
  ManifestProvider* const manifest_provider_;
  base::WeakPtrFactory<Reader> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(Reader);
};

}  // namespace catalog

#endif  // SERVICES_CATALOG_READER_H_

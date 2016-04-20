// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_MUS_APP_H_
#define COMPONENTS_MUS_MUS_APP_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/macros.h"
#include "base/memory/scoped_ptr.h"
#include "components/mus/public/interfaces/display.mojom.h"
#include "components/mus/public/interfaces/gpu.mojom.h"
#include "components/mus/public/interfaces/user_access_manager.mojom.h"
#include "components/mus/public/interfaces/window_manager_factory.mojom.h"
#include "components/mus/public/interfaces/window_server_test.mojom.h"
#include "components/mus/public/interfaces/window_tree.mojom.h"
#include "components/mus/public/interfaces/window_tree_host.mojom.h"
#include "components/mus/ws/platform_display_init_params.h"
#include "components/mus/ws/user_id.h"
#include "components/mus/ws/window_server_delegate.h"
#include "services/shell/public/cpp/application_runner.h"
#include "services/shell/public/cpp/interface_factory.h"
#include "services/shell/public/cpp/shell_client.h"
#include "services/tracing/public/cpp/tracing_impl.h"

#if defined(USE_OZONE)
#include "ui/ozone/public/client_native_pixmap_factory.h"
#endif

namespace shell {
class Connector;
}

namespace ui {
class PlatformEventSource;
}

namespace mus {
namespace ws {
class ForwardingWindowManager;
class WindowServer;
}

class MandolineUIServicesApp
    : public shell::ShellClient,
      public ws::WindowServerDelegate,
      public shell::InterfaceFactory<mojom::DisplayManager>,
      public shell::InterfaceFactory<mojom::UserAccessManager>,
      public shell::InterfaceFactory<mojom::WindowManagerFactoryService>,
      public shell::InterfaceFactory<mojom::WindowTreeFactory>,
      public shell::InterfaceFactory<mojom::WindowTreeHostFactory>,
      public shell::InterfaceFactory<mojom::WindowServerTest>,
      public shell::InterfaceFactory<mojom::Gpu> {
 public:
  MandolineUIServicesApp();
  ~MandolineUIServicesApp() override;

 private:
  // Holds InterfaceRequests received before the first WindowTreeHost Display
  // has been established.
  struct PendingRequest;
  struct UserState;

  using UserIdToUserState = std::map<ws::UserId, scoped_ptr<UserState>>;

  void InitializeResources(shell::Connector* connector);

  // Returns the user specific state for the user id of |connection|. MusApp
  // owns the return value.
  // TODO(sky): if we allow removal of user ids then we need to close anything
  // associated with the user (all incoming pipes...) on removal.
  UserState* GetUserState(shell::Connection* connection);

  void AddUserIfNecessary(shell::Connection* connection);

  // shell::ShellClient:
  void Initialize(shell::Connector* connector,
                  const shell::Identity& identity,
                  uint32_t id) override;
  bool AcceptConnection(shell::Connection* connection) override;

  // WindowServerDelegate:
  void OnFirstDisplayReady() override;
  void OnNoMoreDisplays() override;
  bool IsTestConfig() const override;
  void CreateDefaultDisplays() override;

  // shell::InterfaceFactory<mojom::DisplayManager> implementation.
  void Create(shell::Connection* connection,
              mojom::DisplayManagerRequest request) override;

  // shell::InterfaceFactory<mojom::UserAccessManager> implementation.
  void Create(shell::Connection* connection,
              mojom::UserAccessManagerRequest request) override;

  // shell::InterfaceFactory<mojom::WindowManagerFactoryService> implementation.
  void Create(shell::Connection* connection,
              mojom::WindowManagerFactoryServiceRequest request) override;

  // shell::InterfaceFactory<mojom::WindowTreeFactory>:
  void Create(shell::Connection* connection,
              mojom::WindowTreeFactoryRequest request) override;

  // shell::InterfaceFactory<mojom::WindowTreeHostFactory>:
  void Create(shell::Connection* connection,
              mojom::WindowTreeHostFactoryRequest request) override;

  // shell::InterfaceFactory<mojom::WindowServerTest> implementation.
  void Create(shell::Connection* connection,
              mojom::WindowServerTestRequest request) override;

  // shell::InterfaceFactory<mojom::Gpu> implementation.
  void Create(shell::Connection* connection,
              mojom::GpuRequest request) override;

  ws::PlatformDisplayInitParams platform_display_init_params_;
  scoped_ptr<ws::WindowServer> window_server_;
  scoped_ptr<ui::PlatformEventSource> event_source_;
  mojo::TracingImpl tracing_;
  using PendingRequests = std::vector<scoped_ptr<PendingRequest>>;
  PendingRequests pending_requests_;

  UserIdToUserState user_id_to_user_state_;

  bool test_config_;
#if defined(USE_OZONE)
  scoped_ptr<ui::ClientNativePixmapFactory> client_native_pixmap_factory_;
#endif

  DISALLOW_COPY_AND_ASSIGN(MandolineUIServicesApp);
};

}  // namespace mus

#endif  // COMPONENTS_MUS_MUS_APP_H_

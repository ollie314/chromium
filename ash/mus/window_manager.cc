// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/mus/window_manager.h"

#include <stdint.h>

#include <utility>

#include "ash/common/shell_window_ids.h"
#include "ash/mus/accelerators/accelerator_handler.h"
#include "ash/mus/accelerators/accelerator_ids.h"
#include "ash/mus/app_list_presenter_mus.h"
#include "ash/mus/bridge/wm_lookup_mus.h"
#include "ash/mus/bridge/wm_root_window_controller_mus.h"
#include "ash/mus/bridge/wm_shell_mus.h"
#include "ash/mus/bridge/wm_window_mus.h"
#include "ash/mus/move_event_handler.h"
#include "ash/mus/non_client_frame_controller.h"
#include "ash/mus/property_util.h"
#include "ash/mus/root_window_controller.h"
#include "ash/mus/shadow_controller.h"
#include "ash/mus/shell_delegate_mus.h"
#include "ash/mus/window_manager_observer.h"
#include "ash/public/interfaces/container.mojom.h"
#include "base/memory/ptr_util.h"
#include "services/ui/common/event_matcher_util.h"
#include "services/ui/common/types.h"
#include "services/ui/public/cpp/property_type_converters.h"
#include "services/ui/public/cpp/window.h"
#include "services/ui/public/cpp/window_property.h"
#include "services/ui/public/cpp/window_tree_client.h"
#include "services/ui/public/interfaces/mus_constants.mojom.h"
#include "services/ui/public/interfaces/window_manager.mojom.h"
#include "ui/base/hit_test.h"
#include "ui/events/mojo/event.mojom.h"
#include "ui/views/mus/pointer_watcher_event_router.h"
#include "ui/views/mus/screen_mus.h"

namespace ash {
namespace mus {

WindowManager::WindowManager(shell::Connector* connector)
    : connector_(connector) {}

WindowManager::~WindowManager() {
  Shutdown();
}

void WindowManager::Init(
    std::unique_ptr<ui::WindowTreeClient> window_tree_client,
    const scoped_refptr<base::SequencedWorkerPool>& blocking_pool) {
  DCHECK(!window_tree_client_);
  window_tree_client_ = std::move(window_tree_client);

  screen_ = base::MakeUnique<display::ScreenBase>();

  pointer_watcher_event_router_.reset(
      new views::PointerWatcherEventRouter(window_tree_client_.get()));

  shadow_controller_.reset(new ShadowController(window_tree_client_.get()));

  // The insets are roughly what is needed by CustomFrameView. The expectation
  // is at some point we'll write our own NonClientFrameView and get the insets
  // from it.
  ui::mojom::FrameDecorationValuesPtr frame_decoration_values =
      ui::mojom::FrameDecorationValues::New();
  const gfx::Insets client_area_insets =
      NonClientFrameController::GetPreferredClientAreaInsets();
  frame_decoration_values->normal_client_area_insets = client_area_insets;
  frame_decoration_values->maximized_client_area_insets = client_area_insets;
  frame_decoration_values->max_title_bar_button_width =
      NonClientFrameController::GetMaxTitleBarButtonWidth();
  window_manager_client_->SetFrameDecorationValues(
      std::move(frame_decoration_values));

  shell_.reset(new WmShellMus(base::MakeUnique<ShellDelegateMus>(connector_),
                              this, pointer_watcher_event_router_.get()));
  shell_->Initialize(blocking_pool);
  lookup_.reset(new WmLookupMus);
}

void WindowManager::SetScreenLocked(bool is_locked) {
  // TODO: screen locked state needs to be persisted for newly added displays.
  for (auto& root_window_controller : root_window_controllers_) {
    WmWindowMus* non_lock_screen_containers_container =
        root_window_controller->GetWindowByShellWindowId(
            kShellWindowId_NonLockScreenContainersContainer);
    non_lock_screen_containers_container->mus_window()->SetVisible(!is_locked);
  }
}

ui::Window* WindowManager::NewTopLevelWindow(
    std::map<std::string, std::vector<uint8_t>>* properties) {
  // TODO(sky): need to maintain active as well as allowing specifying display.
  RootWindowController* root_window_controller =
      root_window_controllers_.begin()->get();
  return root_window_controller->NewTopLevelWindow(properties);
}

std::set<RootWindowController*> WindowManager::GetRootWindowControllers() {
  std::set<RootWindowController*> result;
  for (auto& root_window_controller : root_window_controllers_)
    result.insert(root_window_controller.get());
  return result;
}

bool WindowManager::GetNextAcceleratorNamespaceId(uint16_t* id) {
  if (accelerator_handlers_.size() == std::numeric_limits<uint16_t>::max())
    return false;
  while (accelerator_handlers_.count(next_accelerator_namespace_id_) > 0)
    ++next_accelerator_namespace_id_;
  *id = next_accelerator_namespace_id_;
  ++next_accelerator_namespace_id_;
  return true;
}

void WindowManager::AddAcceleratorHandler(uint16_t id_namespace,
                                          AcceleratorHandler* handler) {
  DCHECK_EQ(0u, accelerator_handlers_.count(id_namespace));
  accelerator_handlers_[id_namespace] = handler;
}

void WindowManager::RemoveAcceleratorHandler(uint16_t id_namespace) {
  accelerator_handlers_.erase(id_namespace);
}

void WindowManager::AddObserver(WindowManagerObserver* observer) {
  observers_.AddObserver(observer);
}

void WindowManager::RemoveObserver(WindowManagerObserver* observer) {
  observers_.RemoveObserver(observer);
}

RootWindowController* WindowManager::CreateRootWindowController(
    ui::Window* window,
    const display::Display& display) {
  // TODO(sky): should be passed whether display is primary.

  // There needs to be at least one display before creating
  // RootWindowController, otherwise initializing the compositor fails.
  const bool was_displays_empty = screen_->display_list()->displays().empty();
  if (was_displays_empty) {
    screen_->display_list()->AddDisplay(display,
                                        display::DisplayList::Type::PRIMARY);
  }

  std::unique_ptr<RootWindowController> root_window_controller_ptr(
      new RootWindowController(this, window, display));
  RootWindowController* root_window_controller =
      root_window_controller_ptr.get();
  root_window_controllers_.insert(std::move(root_window_controller_ptr));

  FOR_EACH_OBSERVER(WindowManagerObserver, observers_,
                    OnRootWindowControllerAdded(root_window_controller));

  if (!was_displays_empty) {
    // If this isn't the initial display then add the display to Screen after
    // creating the RootWindowController. We need to do this after creating the
    // RootWindowController as adding the display triggers OnDisplayAdded(),
    // which triggers some overrides asking for the RootWindowController for the
    // new display.
    screen_->display_list()->AddDisplay(
        display, display::DisplayList::Type::NOT_PRIMARY);
  }
  return root_window_controller;
}

void WindowManager::DestroyRootWindowController(
    RootWindowController* root_window_controller) {
  if (root_window_controllers_.size() > 1) {
    DCHECK_NE(root_window_controller, GetPrimaryRootWindowController());
    root_window_controller->wm_root_window_controller()->MoveWindowsTo(
        WmWindowMus::Get(GetPrimaryRootWindowController()->root()));
  }

  ui::Window* root_window = root_window_controller->root();
  auto it = FindRootWindowControllerByWindow(root_window);
  DCHECK(it != root_window_controllers_.end());

  (*it)->Shutdown();

  // NOTE: classic ash deleted RootWindowController after a delay (DeleteSoon())
  // this may need to change to mirror that.
  root_window_controllers_.erase(it);
}

void WindowManager::Shutdown() {
  if (!window_tree_client_)
    return;

  // Observers can rely on WmShell from the callback. So notify the observers
  // before destroying it.
  FOR_EACH_OBSERVER(WindowManagerObserver, observers_,
                    OnWindowTreeClientDestroyed());

  // Destroy the roots of the RootWindowControllers, which triggers removal
  // in OnWindowDestroyed().
  while (!root_window_controllers_.empty())
    DestroyRootWindowController(root_window_controllers_.begin()->get());

  lookup_.reset();
  shell_->Shutdown();
  shell_.reset();
  shadow_controller_.reset();

  pointer_watcher_event_router_.reset();

  window_tree_client_.reset();
  window_manager_client_ = nullptr;
}

WindowManager::RootWindowControllers::iterator
WindowManager::FindRootWindowControllerByWindow(ui::Window* window) {
  for (auto it = root_window_controllers_.begin();
       it != root_window_controllers_.end(); ++it) {
    if ((*it)->root() == window)
      return it;
  }
  return root_window_controllers_.end();
}

RootWindowController* WindowManager::GetPrimaryRootWindowController() {
  return static_cast<WmRootWindowControllerMus*>(
             WmShell::Get()->GetPrimaryRootWindowController())
      ->root_window_controller();
}

void WindowManager::OnEmbed(ui::Window* root) {
  // WindowManager should never see this, instead OnWmNewDisplay() is called.
  NOTREACHED();
}

void WindowManager::OnEmbedRootDestroyed(ui::Window* root) {
  // WindowManager should never see this.
  NOTREACHED();
}

void WindowManager::OnLostConnection(ui::WindowTreeClient* client) {
  DCHECK_EQ(client, window_tree_client_.get());
  Shutdown();
  // TODO(sky): this case should trigger shutting down WindowManagerApplication
  // too.
}

void WindowManager::OnPointerEventObserved(const ui::PointerEvent& event,
                                           ui::Window* target) {
  pointer_watcher_event_router_->OnPointerEventObserved(event, target);
}

void WindowManager::SetWindowManagerClient(ui::WindowManagerClient* client) {
  window_manager_client_ = client;
}

bool WindowManager::OnWmSetBounds(ui::Window* window, gfx::Rect* bounds) {
  // TODO(sky): this indirectly sets bounds, which is against what
  // OnWmSetBounds() recommends doing. Remove that restriction, or fix this.
  WmWindowMus::Get(window)->SetBounds(*bounds);
  *bounds = window->bounds();
  return true;
}

bool WindowManager::OnWmSetProperty(
    ui::Window* window,
    const std::string& name,
    std::unique_ptr<std::vector<uint8_t>>* new_data) {
  // TODO(sky): constrain this to set of keys we know about, and allowed values.
  return name == ui::mojom::WindowManager::kShowState_Property ||
         name == ui::mojom::WindowManager::kPreferredSize_Property ||
         name == ui::mojom::WindowManager::kResizeBehavior_Property ||
         name == ui::mojom::WindowManager::kWindowAppIcon_Property ||
         name == ui::mojom::WindowManager::kWindowTitle_Property;
}

ui::Window* WindowManager::OnWmCreateTopLevelWindow(
    std::map<std::string, std::vector<uint8_t>>* properties) {
  return NewTopLevelWindow(properties);
}

void WindowManager::OnWmClientJankinessChanged(
    const std::set<ui::Window*>& client_windows,
    bool janky) {
  for (auto* window : client_windows)
    SetWindowIsJanky(window, janky);
}

void WindowManager::OnWmNewDisplay(ui::Window* window,
                                   const display::Display& display) {
  CreateRootWindowController(window, display);
}

void WindowManager::OnWmDisplayRemoved(ui::Window* window) {
  auto iter = FindRootWindowControllerByWindow(window);
  DCHECK(iter != root_window_controllers_.end());
  DestroyRootWindowController(iter->get());
}

void WindowManager::OnWmPerformMoveLoop(
    ui::Window* window,
    ui::mojom::MoveLoopSource source,
    const gfx::Point& cursor_location,
    const base::Callback<void(bool)>& on_done) {
  WmWindowMus* child_window = WmWindowMus::Get(window);
  MoveEventHandler* handler = MoveEventHandler::GetForWindow(child_window);
  if (!handler) {
    on_done.Run(false);
    return;
  }

  DCHECK(!handler->IsDragInProgress());
  aura::client::WindowMoveSource aura_source =
      source == ui::mojom::MoveLoopSource::MOUSE
          ? aura::client::WINDOW_MOVE_SOURCE_MOUSE
          : aura::client::WINDOW_MOVE_SOURCE_TOUCH;
  handler->AttemptToStartDrag(cursor_location, HTCAPTION, aura_source, on_done);
}

void WindowManager::OnWmCancelMoveLoop(ui::Window* window) {
  WmWindowMus* child_window = WmWindowMus::Get(window);
  MoveEventHandler* handler = MoveEventHandler::GetForWindow(child_window);
  if (handler)
    handler->RevertDrag();
}

ui::mojom::EventResult WindowManager::OnAccelerator(uint32_t id,
                                                    const ui::Event& event) {
  auto iter = accelerator_handlers_.find(GetAcceleratorNamespaceId(id));
  if (iter == accelerator_handlers_.end())
    return ui::mojom::EventResult::HANDLED;

  return iter->second->OnAccelerator(id, event);
}

}  // namespace mus
}  // namespace ash

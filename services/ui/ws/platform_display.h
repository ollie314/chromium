// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_WS_PLATFORM_DISPLAY_H_
#define SERVICES_UI_WS_PLATFORM_DISPLAY_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <utility>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "build/build_config.h"
#include "services/ui/display/viewport_metrics.h"
#include "services/ui/public/interfaces/cursor.mojom.h"
#include "services/ui/public/interfaces/window_manager.mojom.h"
#include "services/ui/public/interfaces/window_manager_constants.mojom.h"
#include "services/ui/public/interfaces/window_tree.mojom.h"
#include "services/ui/ws/frame_generator.h"
#include "services/ui/ws/frame_generator_delegate.h"
#include "services/ui/ws/platform_display_delegate.h"
#include "ui/display/display.h"
#include "ui/platform_window/platform_window_delegate.h"

namespace cc {
class CopyOutputRequest;
}  // namespace cc

namespace gfx {
class Rect;
}

namespace gpu {
class GpuChannelHost;
}

namespace ui {
class CursorLoader;
class PlatformWindow;
struct TextInputState;
}  // namespace ui

namespace ui {

class FrameGenerator;

namespace ws {

class EventDispatcher;
class PlatformDisplayFactory;
struct PlatformDisplayInitParams;
class ServerWindow;

// PlatformDisplay is used to connect the root ServerWindow to a display.
class PlatformDisplay {
 public:
  virtual ~PlatformDisplay() {}

  static PlatformDisplay* Create(const PlatformDisplayInitParams& init_params);

  virtual int64_t GetId() const = 0;

  virtual void Init(PlatformDisplayDelegate* delegate) = 0;

  virtual void SetViewportSize(const gfx::Size& size) = 0;

  virtual void SetTitle(const base::string16& title) = 0;

  virtual void SetCapture() = 0;

  virtual void ReleaseCapture() = 0;

  virtual void SetCursorById(mojom::Cursor cursor) = 0;

  virtual void UpdateTextInputState(const ui::TextInputState& state) = 0;
  virtual void SetImeVisibility(bool visible) = 0;

  virtual gfx::Rect GetBounds() const = 0;

  // Updates the viewport metrics for the display, returning true if any
  // metrics have changed.
  virtual bool UpdateViewportMetrics(
      const display::ViewportMetrics& metrics) = 0;

  virtual const display::ViewportMetrics& GetViewportMetrics() const = 0;

  virtual bool IsPrimaryDisplay() const = 0;

  // Notifies the PlatformDisplay that a connection to the gpu has been
  // established.
  virtual void OnGpuChannelEstablished(
      scoped_refptr<gpu::GpuChannelHost> gpu_channel) = 0;

  // Overrides factory for testing. Default (NULL) value indicates regular
  // (non-test) environment.
  static void set_factory_for_testing(PlatformDisplayFactory* factory) {
    PlatformDisplay::factory_ = factory;
  }

 private:
  // Static factory instance (always NULL for non-test).
  static PlatformDisplayFactory* factory_;
};

// PlatformDisplay implementation that connects to the services necessary to
// actually display.
class DefaultPlatformDisplay : public PlatformDisplay,
                               public ui::PlatformWindowDelegate,
                               private FrameGeneratorDelegate {
 public:
  explicit DefaultPlatformDisplay(const PlatformDisplayInitParams& init_params);
  ~DefaultPlatformDisplay() override;

  // PlatformDisplay:
  void Init(PlatformDisplayDelegate* delegate) override;
  int64_t GetId() const override;
  void SetViewportSize(const gfx::Size& size) override;
  void SetTitle(const base::string16& title) override;
  void SetCapture() override;
  void ReleaseCapture() override;
  void SetCursorById(mojom::Cursor cursor) override;
  void UpdateTextInputState(const ui::TextInputState& state) override;
  void SetImeVisibility(bool visible) override;
  gfx::Rect GetBounds() const override;
  bool UpdateViewportMetrics(const display::ViewportMetrics& metrics) override;
  const display::ViewportMetrics& GetViewportMetrics() const override;
  bool IsPrimaryDisplay() const override;
  void OnGpuChannelEstablished(
      scoped_refptr<gpu::GpuChannelHost> gpu_channel) override;

 private:
  // Update the root_location of located events to be relative to the origin
  // of this display. For example, if the origin of this display is (1800, 0)
  // and the location of the event is (100, 200) then the root_location will be
  // updated to be (1900, 200).
  void UpdateEventRootLocation(ui::LocatedEvent* event);

  // ui::PlatformWindowDelegate:
  void OnBoundsChanged(const gfx::Rect& new_bounds) override;
  void OnDamageRect(const gfx::Rect& damaged_region) override;
  void DispatchEvent(ui::Event* event) override;
  void OnCloseRequest() override;
  void OnClosed() override;
  void OnWindowStateChanged(ui::PlatformWindowState new_state) override;
  void OnLostCapture() override;
  void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget,
                                    float device_scale_factor) override;
  void OnAcceleratedWidgetDestroyed() override;
  void OnActivationChanged(bool active) override;

  // FrameGeneratorDelegate:
  bool IsInHighContrastMode() override;

  int64_t id_;

#if !defined(OS_ANDROID)
  std::unique_ptr<ui::CursorLoader> cursor_loader_;
#endif

  PlatformDisplayDelegate* delegate_ = nullptr;
  std::unique_ptr<FrameGenerator> frame_generator_;

  display::ViewportMetrics metrics_;
  std::unique_ptr<ui::PlatformWindow> platform_window_;

  DISALLOW_COPY_AND_ASSIGN(DefaultPlatformDisplay);
};

}  // namespace ws

}  // namespace ui

#endif  // SERVICES_UI_WS_PLATFORM_DISPLAY_H_

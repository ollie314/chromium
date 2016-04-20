// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_MUS_SURFACES_TOP_LEVEL_DISPLAY_CLIENT_H_
#define COMPONENTS_MUS_SURFACES_TOP_LEVEL_DISPLAY_CLIENT_H_

#include <stdint.h>

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "cc/surfaces/display_client.h"
#include "cc/surfaces/surface_factory.h"
#include "cc/surfaces/surface_factory_client.h"
#include "components/mus/gles2/gpu_state.h"
#include "components/mus/public/interfaces/window_manager.mojom.h"
#include "components/mus/surfaces/surfaces_state.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "ui/gfx/native_widget_types.h"

namespace cc {
class CopyOutputResult;
class Display;
class DisplayScheduler;
class SurfaceFactory;
}

namespace mus {

class DisplayDelegate;
class SurfacesState;

// A TopLevelDisplayClient manages the top level surface that is rendered into a
// provided AcceleratedWidget. Frames are submitted here. New frames are
// scheduled to be generated here based on VSync.
class TopLevelDisplayClient : public cc::DisplayClient,
                              public cc::SurfaceFactoryClient {
 public:
  TopLevelDisplayClient(gfx::AcceleratedWidget widget,
                        const scoped_refptr<GpuState>& gpu_state,
                        const scoped_refptr<SurfacesState>& surfaces_state);
  ~TopLevelDisplayClient() override;

  void SubmitCompositorFrame(std::unique_ptr<cc::CompositorFrame> frame,
                             const base::Closure& callback);
  const cc::SurfaceId& surface_id() const { return cc_id_; }

  void RequestCopyOfOutput(
      std::unique_ptr<cc::CopyOutputRequest> output_request);

 private:
  // DisplayClient implementation.
  void OutputSurfaceLost() override;
  void SetMemoryPolicy(const cc::ManagedMemoryPolicy& policy) override;

  // SurfaceFactoryClient implementation.
  void ReturnResources(const cc::ReturnedResourceArray& resources) override;
  void SetBeginFrameSource(cc::BeginFrameSource* begin_frame_source) override;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  scoped_refptr<SurfacesState> surfaces_state_;
  cc::SurfaceFactory factory_;
  cc::SurfaceId cc_id_;

  gfx::Size last_submitted_frame_size_;
  std::unique_ptr<cc::CompositorFrame> pending_frame_;

  std::unique_ptr<cc::Display> display_;

  DISALLOW_COPY_AND_ASSIGN(TopLevelDisplayClient);
};

}  // namespace mus

#endif  // COMPONENTS_MUS_SURFACES_TOP_LEVEL_DISPLAY_CLIENT_H_

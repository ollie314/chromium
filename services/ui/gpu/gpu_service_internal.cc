// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/gpu/gpu_service_internal.h"

#include "base/memory/shared_memory.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/config/gpu_info_collector.h"
#include "gpu/config/gpu_switches.h"
#include "gpu/config/gpu_util.h"
#include "gpu/ipc/common/gpu_memory_buffer_support.h"
#include "gpu/ipc/common/memory_stats.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "gpu/ipc/service/gpu_watchdog_thread.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_sync_message_filter.h"
#include "media/gpu/ipc/service/gpu_jpeg_decode_accelerator.h"
#include "media/gpu/ipc/service/gpu_video_decode_accelerator.h"
#include "media/gpu/ipc/service/gpu_video_encode_accelerator.h"
#include "media/gpu/ipc/service/media_service.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_switches.h"
#include "ui/gl/gpu_switching_manager.h"
#include "ui/gl/init/gl_factory.h"
#include "url/gurl.h"

namespace {

#if defined(OS_WIN)
std::unique_ptr<base::MessagePump> CreateMessagePumpWin() {
  base::MessagePumpForGpu::InitFactory();
  return base::MessageLoop::CreateMessagePumpForType(
      base::MessageLoop::TYPE_UI);
}
#endif  // defined(OS_WIN)

#if defined(USE_X11)
std::unique_ptr<base::MessagePump> CreateMessagePumpX11() {
  // TODO(sad): This should create a TYPE_UI message pump, and create a
  // PlatformEventSource when gpu process split happens.
  return base::MessageLoop::CreateMessagePumpForType(
      base::MessageLoop::TYPE_DEFAULT);
}
#endif  // defined(USE_X11)

#if defined(OS_MACOSX)
std::unique_ptr<base::MessagePump> CreateMessagePumpMac() {
  return base::MakeUnique<base::MessagePumpCFRunLoop>();
}
#endif  // defined(OS_MACOSX)

}  // namespace

namespace ui {

GpuServiceInternal::GpuServiceInternal(
    const gpu::GPUInfo& gpu_info,
    gpu::GpuWatchdogThread* watchdog_thread,
    gpu::GpuMemoryBufferFactory* gpu_memory_buffer_factory)
    : main_task_runner_(base::ThreadTaskRunnerHandle::Get()),
      shutdown_event_(base::WaitableEvent::ResetPolicy::MANUAL,
                      base::WaitableEvent::InitialState::NOT_SIGNALED),
      gpu_thread_("GpuThread"),
      io_thread_("GpuIOThread"),
      watchdog_thread_(watchdog_thread),
      gpu_memory_buffer_factory_(gpu_memory_buffer_factory),
      gpu_info_(gpu_info),
      binding_(this) {
  base::Thread::Options thread_options;

#if defined(OS_WIN)
  thread_options.message_pump_factory = base::Bind(&CreateMessagePumpWin);
#elif defined(USE_X11)
  thread_options.message_pump_factory = base::Bind(&CreateMessagePumpX11);
#elif defined(OS_LINUX)
  thread_options.message_loop_type = base::MessageLoop::TYPE_DEFAULT;
#elif defined(OS_MACOSX)
  thread_options.message_pump_factory = base::Bind(&CreateMessagePumpMac);
#else
  thread_options.message_loop_type = base::MessageLoop::TYPE_IO;
#endif

#if defined(OS_ANDROID) || defined(OS_CHROMEOS)
  thread_options.priority = base::ThreadPriority::DISPLAY;
#endif
  CHECK(gpu_thread_.StartWithOptions(thread_options));

  // TODO(sad): We do not need the IO thread once gpu has a separate process. It
  // should be possible to use |main_task_runner_| for doing IO tasks.
  thread_options = base::Thread::Options(base::MessageLoop::TYPE_IO, 0);
  thread_options.priority = base::ThreadPriority::NORMAL;
#if defined(OS_ANDROID) || defined(OS_CHROMEOS)
  // TODO(reveman): Remove this in favor of setting it explicitly for each type
  // of process.
  thread_options.priority = base::ThreadPriority::DISPLAY;
#endif
  CHECK(io_thread_.StartWithOptions(thread_options));
}

GpuServiceInternal::~GpuServiceInternal() {
  // Tear down the binding in the gpu thread.
  gpu_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&GpuServiceInternal::TearDownGpuThread,
                            base::Unretained(this)));
  gpu_thread_.Stop();

  // Signal this event before destroying the child process.  That way all
  // background threads can cleanup.
  // For example, in the renderer the RenderThread instances will be able to
  // notice shutdown before the render process begins waiting for them to exit.
  shutdown_event_.Signal();
  io_thread_.Stop();
}

void GpuServiceInternal::Add(mojom::GpuServiceInternalRequest request) {
  // Unretained() is OK here since the thread/task runner is owned by |this|.
  gpu_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&GpuServiceInternal::BindOnGpuThread, base::Unretained(this),
                 base::Passed(std::move(request))));
}

void GpuServiceInternal::BindOnGpuThread(
    mojom::GpuServiceInternalRequest request) {
  binding_.Close();
  binding_.Bind(std::move(request));
}

void GpuServiceInternal::TearDownGpuThread() {
  binding_.Close();
  media_service_.reset();
  gpu_channel_manager_.reset();
  owned_sync_point_manager_.reset();
}

gfx::GpuMemoryBufferHandle GpuServiceInternal::CreateGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    const gfx::Size& size,
    gfx::BufferFormat format,
    gfx::BufferUsage usage,
    int client_id,
    gpu::SurfaceHandle surface_handle) {
  DCHECK(gpu_thread_.task_runner()->BelongsToCurrentThread());
  return gpu_memory_buffer_factory_->CreateGpuMemoryBuffer(
      id, size, format, usage, client_id, surface_handle);
}

void GpuServiceInternal::DestroyGpuMemoryBuffer(
    gfx::GpuMemoryBufferId id,
    int client_id,
    const gpu::SyncToken& sync_token) {
  DCHECK(gpu_thread_.task_runner()->BelongsToCurrentThread());
  if (gpu_channel_manager_)
    gpu_channel_manager_->DestroyGpuMemoryBuffer(id, client_id, sync_token);
}

void GpuServiceInternal::DidCreateOffscreenContext(const GURL& active_url) {
  NOTIMPLEMENTED();
}

void GpuServiceInternal::DidDestroyChannel(int client_id) {
  media_service_->RemoveChannel(client_id);
  NOTIMPLEMENTED();
}

void GpuServiceInternal::DidDestroyOffscreenContext(const GURL& active_url) {
  NOTIMPLEMENTED();
}

void GpuServiceInternal::DidLoseContext(bool offscreen,
                                        gpu::error::ContextLostReason reason,
                                        const GURL& active_url) {
  NOTIMPLEMENTED();
}

void GpuServiceInternal::StoreShaderToDisk(int client_id,
                                           const std::string& key,
                                           const std::string& shader) {
  NOTIMPLEMENTED();
}

#if defined(OS_WIN)
void GpuServiceInternal::SendAcceleratedSurfaceCreatedChildWindow(
    gpu::SurfaceHandle parent_window,
    gpu::SurfaceHandle child_window) {
  ::SetParent(child_window, parent_window);
}
#endif

void GpuServiceInternal::SetActiveURL(const GURL& url) {
  // TODO(penghuang): implement this function.
}

void GpuServiceInternal::Initialize(const InitializeCallback& callback) {
  DCHECK(gpu_thread_.task_runner()->BelongsToCurrentThread());
  gpu_info_.video_decode_accelerator_capabilities =
      media::GpuVideoDecodeAccelerator::GetCapabilities(gpu_preferences_);
  gpu_info_.video_encode_accelerator_supported_profiles =
      media::GpuVideoEncodeAccelerator::GetSupportedProfiles(gpu_preferences_);
  gpu_info_.jpeg_decode_accelerator_supported =
      media::GpuJpegDecodeAccelerator::IsSupported();

  DCHECK(!owned_sync_point_manager_);
  const bool allow_threaded_wait = false;
  owned_sync_point_manager_.reset(
      new gpu::SyncPointManager(allow_threaded_wait));

  // Defer creation of the render thread. This is to prevent it from handling
  // IPC messages before the sandbox has been enabled and all other necessary
  // initialization has succeeded.
  gpu_channel_manager_.reset(new gpu::GpuChannelManager(
      gpu_preferences_, this, watchdog_thread_,
      base::ThreadTaskRunnerHandle::Get().get(), io_thread_.task_runner().get(),
      &shutdown_event_, owned_sync_point_manager_.get(),
      gpu_memory_buffer_factory_));

  media_service_.reset(new media::MediaService(gpu_channel_manager_.get()));
  callback.Run(gpu_info_);
}

void GpuServiceInternal::EstablishGpuChannel(
    int32_t client_id,
    uint64_t client_tracing_id,
    bool is_gpu_host,
    const EstablishGpuChannelCallback& callback) {
  DCHECK(gpu_thread_.task_runner()->BelongsToCurrentThread());

  if (!gpu_channel_manager_) {
    callback.Run(mojo::ScopedMessagePipeHandle());
    return;
  }

  const bool preempts = is_gpu_host;
  const bool allow_view_command_buffers = is_gpu_host;
  const bool allow_real_time_streams = is_gpu_host;
  mojo::ScopedMessagePipeHandle channel_handle;
  IPC::ChannelHandle handle = gpu_channel_manager_->EstablishChannel(
      client_id, client_tracing_id, preempts, allow_view_command_buffers,
      allow_real_time_streams);
  channel_handle.reset(handle.mojo_handle);
  media_service_->AddChannel(client_id);
  callback.Run(std::move(channel_handle));
}

}  // namespace ui

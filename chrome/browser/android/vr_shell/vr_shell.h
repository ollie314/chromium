// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_

#include <jni.h>
#include <memory>
#include <vector>

#include "base/android/jni_weak_ref.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/android/vr_shell/ui_elements.h"
#include "chrome/browser/android/vr_shell/ui_scene.h"
#include "device/vr/android/gvr/gvr_delegate.h"
#include "third_party/gvr-android-sdk/src/ndk-beta/include/vr/gvr/capi/include/gvr.h"
#include "third_party/gvr-android-sdk/src/ndk-beta/include/vr/gvr/capi/include/gvr_types.h"

namespace content {
class ContentViewCore;
}

namespace ui {
class WindowAndroid;
}

namespace vr_shell {

class VrCompositor;
class VrShellDelegate;
class VrShellRenderer;

class VrShell : public device::GvrDelegate {
 public:
  VrShell(JNIEnv* env, jobject obj,
          content::ContentViewCore* content_cvc,
          ui::WindowAndroid* content_window,
          content::ContentViewCore* ui_cvc,
          ui::WindowAndroid* ui_window);

  void UpdateCompositorLayers(JNIEnv* env,
                              const base::android::JavaParamRef<jobject>& obj);
  void Destroy(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void SetDelegate(JNIEnv* env,
                   const base::android::JavaParamRef<jobject>& obj,
                   const base::android::JavaParamRef<jobject>& delegate);
  void GvrInit(JNIEnv* env,
               const base::android::JavaParamRef<jobject>& obj,
               jlong native_gvr_api);
  void InitializeGl(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    jint content_texture_handle,
                    jint ui_texture_handle);
  void DrawFrame(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void OnPause(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void OnResume(JNIEnv* env, const base::android::JavaParamRef<jobject>& obj);
  void SetWebVrMode(JNIEnv* env,
                    const base::android::JavaParamRef<jobject>& obj,
                    bool enabled);

  // html/js UI hooks.
  static base::WeakPtr<VrShell> GetWeakPtr();
  void OnDomContentsLoaded();
  void SetUiTextureSize(int width, int height);

  // device::GvrDelegate implementation
  void SubmitWebVRFrame() override;
  void UpdateWebVRTextureBounds(
      int eye, float left, float top, float width, float height) override;
  gvr::GvrApi* gvr_api() override;

  void ContentSurfaceChanged(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& object,
      jint width,
      jint height,
      const base::android::JavaParamRef<jobject>& surface);
  void UiSurfaceChanged(
      JNIEnv* env,
      const base::android::JavaParamRef<jobject>& object,
      jint width,
      jint height,
      const base::android::JavaParamRef<jobject>& surface);

 private:
  virtual ~VrShell();
  void LoadUIContent();
  void DrawVrShell(int64_t time);
  void DrawEye(const gvr::Mat4f& view_matrix,
               const gvr::BufferViewport& params);
  void DrawContentRect();
  void DrawWebVr();
  void DrawUI();
  void DrawCursor();

  void UpdateController();

  // samplerExternalOES texture data for UI content image.
  jint ui_texture_id_ = 0;
  // samplerExternalOES texture data for main content image.
  jint content_texture_id_ = 0;

  float desktop_screen_tilt_;
  float desktop_height_;

  ContentRectangle* desktop_plane_;
  gvr::Vec3f desktop_position_;

  UiScene scene_;

  std::unique_ptr<gvr::GvrApi> gvr_api_;
  std::unique_ptr<gvr::BufferViewportList> buffer_viewport_list_;
  std::unique_ptr<gvr::BufferViewport> buffer_viewport_;
  std::unique_ptr<gvr::SwapChain> swap_chain_;

  gvr::Mat4f view_matrix_;
  gvr::Mat4f projection_matrix_;

  gvr::Mat4f head_pose_;
  gvr::Vec3f forward_vector_;

  gvr::Sizei render_size_;
  float cursor_distance_;

  std::unique_ptr<VrCompositor> content_compositor_;
  content::ContentViewCore* content_cvc_;
  std::unique_ptr<VrCompositor> ui_compositor_;
  content::ContentViewCore* ui_cvc_;

  VrShellDelegate* delegate_;
  std::unique_ptr<VrShellRenderer> vr_shell_renderer_;
  base::android::ScopedJavaGlobalRef<jobject> j_vr_shell_;

  gvr::Quatf controller_quat_;
  bool controller_active_ = false;
  gvr::Vec3f look_at_vector_;
  int ui_tex_width_ = 0;
  int ui_tex_height_ = 0;

  bool webvr_mode_ = false;

  base::WeakPtrFactory<VrShell> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(VrShell);
};

bool RegisterVrShell(JNIEnv* env);

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_SHELL_H_

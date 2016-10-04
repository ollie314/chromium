// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/android/vr_shell/vr_shell.h"

#include <thread>

#include "chrome/browser/android/vr_shell/ui_scene.h"
#include "chrome/browser/android/vr_shell/vr_compositor.h"
#include "chrome/browser/android/vr_shell/vr_gl_util.h"
#include "chrome/browser/android/vr_shell/vr_math.h"
#include "chrome/browser/android/vr_shell/vr_shell_delegate.h"
#include "chrome/browser/android/vr_shell/vr_shell_renderer.h"
#include "content/public/browser/android/content_view_core.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "jni/VrShell_jni.h"
#include "ui/android/view_android.h"
#include "ui/android/window_android.h"
#include "ui/base/page_transition_types.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/init/gl_factory.h"

using base::android::JavaParamRef;

namespace {
// Constant taken from treasure_hunt demo.
static constexpr long kPredictionTimeWithoutVsyncNanos = 50000000;

static constexpr float kZNear = 0.1f;
static constexpr float kZFar = 1000.0f;

static constexpr gvr::Vec3f kDesktopPositionDefault = {0.0f, 0.0f, -2.0f};
static constexpr float kDesktopHeightDefault = 1.6f;

// Screen angle in degrees. 0 = vertical, positive = top closer.
static constexpr float kDesktopScreenTiltDefault = 0;

static constexpr float kScreenHeightRatio = 1.0f;
static constexpr float kScreenWidthRatio = 16.0f / 9.0f;

static constexpr float kReticleWidth = 0.025f;
static constexpr float kReticleHeight = 0.025f;

static constexpr float kLaserWidth = 0.01f;

// The neutral direction is fixed in world space, this is the
// reference angle pointing forward towards the horizon when the
// controller orientation is reset. This should match the yaw angle
// where the main screen is placed.
static constexpr gvr::Vec3f kNeutralPose = {0.0f, 0.0f, -1.0f};

static constexpr gvr::Vec3f kOrigin = {0.0f, 0.0f, 0.0f};

// In lieu of an elbow model, we assume a position for the user's hand.
// TODO(mthiesse): Handedness options.
static constexpr gvr::Vec3f kHandPosition = {0.2f, -0.5f, -0.2f};

// Fraction of the distance to the object the cursor is drawn at to avoid
// rounding errors drawing the cursor behind the object.
static constexpr float kReticleOffset = 0.99f;

// Limit the rendering distance of the reticle to the distance to a corner of
// the content quad, times this value.  This lets the rendering distance
// adjust according to content quad placement.
static constexpr float kReticleDistanceMultiplier = 1.5f;

// UI element 0 is the browser content rectangle.
static constexpr int kBrowserUiElementId = 0;

// Positions and sizes of statically placed UI elements in the UI texture.
// TODO(klausw): replace the hardcoded positions with JS position/offset
// retrieval once the infrastructure for that is hooked up.
//
// UI is designed with 1 pixel = 1mm at 1m distance. It's rescaled to
// maintain the same angular resolution if placed closer or further.
// The warning overlays should be fairly close since they cut holes
// into geometry (they ignore the Z buffer), leading to odd effects
// if they are far away.
static constexpr vr_shell::Recti kWebVrWarningTransientRect = {
  0, 128, 512, 256};
static constexpr vr_shell::Recti kWebVrWarningPermanentRect = {0, 0, 512, 128};
static constexpr float kWebVrWarningDistance = 0.7f;  // meters
static constexpr float kWebVrWarningPermanentAngle = 16.3f;  // degrees up
// How long the transient warning needs to be displayed.
static constexpr int64_t kWebVrWarningSeconds = 30;

vr_shell::VrShell* g_instance;

static const char kVrShellUIURL[] = "chrome://vr-shell-ui";

float Distance(const gvr::Vec3f& vec1, const gvr::Vec3f& vec2) {
  float xdiff = (vec1.x - vec2.x);
  float ydiff = (vec1.y - vec2.y);
  float zdiff = (vec1.z - vec2.z);
  float scale = xdiff * xdiff + ydiff * ydiff + zdiff * zdiff;
  return std::sqrt(scale);
}

// Generate a quaternion representing the rotation from the negative Z axis
// (0, 0, -1) to a specified vector.  This is an optimized version of a more
// general vector-to-vector calculation.
gvr::Quatf GetRotationFromZAxis(gvr::Vec3f vec) {
  vr_shell::NormalizeVector(vec);
  gvr::Quatf quat;
  quat.qw = 1.0f - vec.z;
  if (quat.qw < 1e-6f) {
    // Degenerate case: vectors are exactly opposite. Replace by an
    // arbitrary 180 degree rotation to avoid invalid normalization.
    quat.qx = 1.0f;
    quat.qy = 0.0f;
    quat.qz = 0.0f;
    quat.qw = 0.0f;
  } else {
    quat.qx = vec.y;
    quat.qy = -vec.x;
    quat.qz = 0.0f;
    vr_shell::NormalizeQuat(quat);
  }
  return quat;
}

}  // namespace

namespace vr_shell {

VrShell::VrShell(JNIEnv* env,
                 jobject obj,
                 content::ContentViewCore* content_cvc,
                 ui::WindowAndroid* content_window,
                 content::ContentViewCore* ui_cvc,
                 ui::WindowAndroid* ui_window)
    : desktop_screen_tilt_(kDesktopScreenTiltDefault),
      desktop_height_(kDesktopHeightDefault),
      content_cvc_(content_cvc),
      ui_cvc_(ui_cvc),
      delegate_(nullptr),
      weak_ptr_factory_(this) {
  g_instance = this;
  j_vr_shell_.Reset(env, obj);
  content_compositor_.reset(new VrCompositor(content_window, false));
  ui_compositor_.reset(new VrCompositor(ui_window, true));

  float screen_width = kScreenWidthRatio * desktop_height_;
  float screen_height = kScreenHeightRatio * desktop_height_;
  std::unique_ptr<ContentRectangle> rect(new ContentRectangle());
  rect->id = kBrowserUiElementId;
  rect->size = {screen_width, screen_height, 1.0f};
  rect->translation = kDesktopPositionDefault;
  scene_.AddUiElement(rect);

  desktop_plane_ = scene_.GetUiElementById(kBrowserUiElementId);

  LoadUIContent();
}

void VrShell::UpdateCompositorLayers(JNIEnv* env,
                                     const JavaParamRef<jobject>& obj) {
  content_compositor_->SetLayer(content_cvc_);
  ui_compositor_->SetLayer(ui_cvc_);
}

void VrShell::Destroy(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  delete this;
}

void VrShell::LoadUIContent() {
  GURL url(kVrShellUIURL);
  ui_cvc_->GetWebContents()->GetController().LoadURL(
      url, content::Referrer(),
      ui::PageTransition::PAGE_TRANSITION_AUTO_TOPLEVEL, std::string(""));
}

bool RegisterVrShell(JNIEnv* env) {
  return RegisterNativesImpl(env);
}

VrShell::~VrShell() {
  g_instance = nullptr;
  gl::init::ClearGLBindings();
}

void VrShell::SetDelegate(JNIEnv* env,
    const base::android::JavaParamRef<jobject>& obj,
    const base::android::JavaParamRef<jobject>& delegate) {
  delegate_ = VrShellDelegate::getNativeDelegate(env, delegate);
}

void VrShell::GvrInit(JNIEnv* env,
                      const JavaParamRef<jobject>& obj,
                      jlong native_gvr_api) {
  gvr_api_ =
      gvr::GvrApi::WrapNonOwned(reinterpret_cast<gvr_context*>(native_gvr_api));

  if (delegate_)
    delegate_->OnVrShellReady(this);
}

void VrShell::InitializeGl(JNIEnv* env,
                           const JavaParamRef<jobject>& obj,
                           jint content_texture_handle,
                           jint ui_texture_handle) {
  CHECK(gl::GetGLImplementation() != gl::kGLImplementationNone ||
        gl::init::InitializeGLOneOff());

  content_texture_id_ = content_texture_handle;
  ui_texture_id_ = ui_texture_handle;

  gvr_api_->InitializeGl();
  std::vector<gvr::BufferSpec> specs;
  specs.push_back(gvr_api_->CreateBufferSpec());
  render_size_ = specs[0].GetSize();
  swap_chain_.reset(new gvr::SwapChain(gvr_api_->CreateSwapchain(specs)));

  vr_shell_renderer_.reset(new VrShellRenderer());
  buffer_viewport_list_.reset(
      new gvr::BufferViewportList(gvr_api_->CreateEmptyBufferViewportList()));
  buffer_viewport_.reset(
      new gvr::BufferViewport(gvr_api_->CreateBufferViewport()));
}

void VrShell::UpdateController(const gvr::Vec3f& forward_vector) {
  if (!controller_active_) {
    // No controller detected, set up a gaze cursor that tracks the
    // forward direction.
    controller_quat_ = GetRotationFromZAxis(forward_vector);
  }

  gvr::Mat4f mat = QuatToMatrix(controller_quat_);
  gvr::Vec3f forward = MatrixVectorMul(mat, kNeutralPose);
  gvr::Vec3f origin = kHandPosition;

  target_element_ = nullptr;;
  float distance = scene_.GetUiElementById(kBrowserUiElementId)
      ->GetRayDistance(origin, forward);

  // Find distance to a corner of the content quad, and limit the cursor
  // distance to a multiple of that distance.  This lets us keep the reticle on
  // the content plane near the content window, and on the surface of a sphere
  // in other directions.
  // TODO(cjgrant): Note that this approach uses distance from controller,
  // rather than eye, for simplicity.  This will make the sphere slightly
  // off-center.
  gvr::Vec3f corner = {0.5f, 0.5f, 0.0f};
  corner = MatrixVectorMul(desktop_plane_->transform.to_world, corner);
  float max_distance = Distance(origin, corner) * kReticleDistanceMultiplier;
  if (distance > max_distance || distance <= 0.0f) {
    distance = max_distance;
  }
  target_point_ = GetRayPoint(origin, forward, distance);

  // Determine which UI element (if any) the cursor is pointing to.
  float closest_element = std::numeric_limits<float>::infinity();
  for (std::size_t i = 0; i < scene_.GetUiElements().size(); ++i) {
    const ContentRectangle& plane = *scene_.GetUiElements()[i].get();
    float distance_to_plane = plane.GetRayDistance(origin, forward);
    gvr::Vec3f plane_intersection_point =
        GetRayPoint(origin, forward, distance_to_plane);

    gvr::Vec3f rect_2d_point =
        MatrixVectorMul(plane.transform.from_world, plane_intersection_point);
    float x = rect_2d_point.x + 0.5f;
    float y = 0.5f - rect_2d_point.y;
    if (distance_to_plane > 0 && distance_to_plane < closest_element) {
      bool is_inside = x >= 0.0f && x < 1.0f && y >= 0.0f && y < 1.0f;
      if (is_inside) {
        closest_element = distance_to_plane;
        target_point_ = plane_intersection_point;
        target_element_ = &plane;
      }
    }
  }
}

void ApplyNeckModel(gvr::Mat4f& mat_forward) {
  // This assumes that the input matrix is a pure rotation matrix. The
  // input object_from_reference matrix has the inverse rotation of
  // the head rotation. Invert it (this is just a transpose).
  gvr::Mat4f mat = MatrixTranspose(mat_forward);

  // Position of the point between the eyes, relative to the neck pivot:
  const float kNeckHorizontalOffset = -0.080f;  // meters in Z
  const float kNeckVerticalOffset = 0.075f;     // meters in Y

  std::array<float, 4> neckOffset = {
      {0.0f, kNeckVerticalOffset, kNeckHorizontalOffset, 1.0f}};

  // Rotate eyes around neck pivot point.
  auto offset = MatrixVectorMul(mat, neckOffset);

  // Measure new position relative to original center of head, because
  // applying a neck model should not elevate the camera.
  offset[1] -= kNeckVerticalOffset;

  // Right-multiply the inverse translation onto the
  // object_from_reference_matrix.
  TranslateMRight(mat_forward, mat_forward, -offset[0], -offset[1], -offset[2]);
}

void VrShell::DrawFrame(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  buffer_viewport_list_->SetToRecommendedBufferViewports();

  gvr::Frame frame = swap_chain_->AcquireFrame();
  gvr::ClockTimePoint target_time = gvr::GvrApi::GetTimePointNow();
  target_time.monotonic_system_time_nanos += kPredictionTimeWithoutVsyncNanos;

  gvr::Mat4f head_pose =
      gvr_api_->GetHeadPoseInStartSpace(target_time);

  gvr::Vec3f position = GetTranslation(head_pose);
  if (position.x == 0.0f && position.y == 0.0f && position.z == 0.0f) {
    // This appears to be a 3DOF pose without a neck model. Add one.
    // The head pose has redundant data. Assume we're only using the
    // object_from_reference_matrix, we're not updating position_external.
    // TODO: Not sure what object_from_reference_matrix is. The new api removed
    // it. For now, removing it seems working fine.
    ApplyNeckModel(head_pose);
  }

  // Bind back to the default framebuffer.
  frame.BindBuffer(0);

  if (webvr_mode_) {
    DrawWebVr();
    if (!webvr_secure_origin_) {
      DrawWebVrOverlay(target_time.monotonic_system_time_nanos);
    }
  } else {
    DrawVrShell(head_pose);
  }

  frame.Unbind();
  frame.Submit(*buffer_viewport_list_, head_pose);
}

void VrShell::DrawVrShell(const gvr::Mat4f& head_pose) {
  float screen_tilt = desktop_screen_tilt_ * M_PI / 180.0f;

  HandleQueuedTasks();

  // Update the render position of all UI elements (including desktop).
  scene_.UpdateTransforms(screen_tilt, UiScene::TimeInMicroseconds());

  UpdateController(GetForwardVector(head_pose));

  // Everything should be positioned now, ready for drawing.
  gvr::Mat4f left_eye_view_matrix =
    MatrixMul(gvr_api_->GetEyeFromHeadMatrix(GVR_LEFT_EYE), head_pose);
  gvr::Mat4f right_eye_view_matrix =
      MatrixMul(gvr_api_->GetEyeFromHeadMatrix(GVR_RIGHT_EYE), head_pose);

  // Use culling to remove back faces.
  glEnable(GL_CULL_FACE);

  // Enable depth testing.
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);

  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

  buffer_viewport_list_->GetBufferViewport(GVR_LEFT_EYE,
                                           buffer_viewport_.get());
  DrawEye(left_eye_view_matrix, *buffer_viewport_);
  buffer_viewport_list_->GetBufferViewport(GVR_RIGHT_EYE,
                                           buffer_viewport_.get());
  DrawEye(right_eye_view_matrix, *buffer_viewport_);
}

void VrShell::DrawEye(const gvr::Mat4f& view_matrix,
                      const gvr::BufferViewport& params) {
  gvr::Recti pixel_rect =
      CalculatePixelSpaceRect(render_size_, params.GetSourceUv());
  glViewport(pixel_rect.left, pixel_rect.bottom,
             pixel_rect.right - pixel_rect.left,
             pixel_rect.top - pixel_rect.bottom);
  glScissor(pixel_rect.left, pixel_rect.bottom,
            pixel_rect.right - pixel_rect.left,
            pixel_rect.top - pixel_rect.bottom);

  gvr::Mat4f render_matrix = MatrixMul(
      PerspectiveMatrixFromView(params.GetSourceFov(), kZNear, kZFar),
      view_matrix);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // TODO(mthiesse): Draw order for transparency.
  DrawUI(render_matrix);
  DrawCursor(render_matrix);
}

bool VrShell::IsUiTextureReady() {
  return ui_tex_width_ > 0 && ui_tex_height_ > 0;
}

Rectf VrShell::MakeUiGlCopyRect(Recti pixel_rect) {
  CHECK(IsUiTextureReady());
  return Rectf({
      static_cast<float>(pixel_rect.x) / ui_tex_width_,
      static_cast<float>(pixel_rect.y) / ui_tex_height_,
      static_cast<float>(pixel_rect.width) / ui_tex_width_,
      static_cast<float>(pixel_rect.height) / ui_tex_height_});
}

void VrShell::DrawUI(const gvr::Mat4f& render_matrix) {
  for (const auto& rect : scene_.GetUiElements()) {
    if (!rect->visible) {
      continue;
    }

    Rectf copy_rect;
    jint texture_handle;
    if (rect->id == kBrowserUiElementId) {
      copy_rect = {0, 0, 1, 1};
      texture_handle = content_texture_id_;
    } else {
      copy_rect.x = static_cast<float>(rect->copy_rect.x) / ui_tex_width_;
      copy_rect.y = static_cast<float>(rect->copy_rect.y) / ui_tex_height_;
      copy_rect.width = static_cast<float>(rect->copy_rect.width) /
          ui_tex_width_;
      copy_rect.height = static_cast<float>(rect->copy_rect.height) /
          ui_tex_height_;
      texture_handle = ui_texture_id_;
    }

    gvr::Mat4f transform = MatrixMul(render_matrix, rect->transform.to_world);
    vr_shell_renderer_->GetTexturedQuadRenderer()->Draw(
        texture_handle, transform, copy_rect);
  }
}

void VrShell::DrawCursor(const gvr::Mat4f& render_matrix) {
  gvr::Mat4f mat;
  SetIdentityM(mat);

  // Draw the reticle.

  // Scale the pointer to have a fixed FOV size at any distance.
  const float eye_to_target = Distance(target_point_, kOrigin);
  ScaleM(mat, mat, kReticleWidth * eye_to_target,
         kReticleHeight * eye_to_target, 1.0f);

  gvr::Quatf rotation;
  if (target_element_ != nullptr) {
    // Make the reticle planar to the element it's hitting.
    rotation = GetRotationFromZAxis(target_element_->GetNormal());
  } else {
    // Rotate the cursor to directly face the eyes.
    rotation = GetRotationFromZAxis(target_point_);
  }
  mat = MatrixMul(QuatToMatrix(rotation), mat);

  // Place the pointer slightly in front of the plane intersection point.
  TranslateM(mat, mat, target_point_.x * kReticleOffset,
             target_point_.y * kReticleOffset,
             target_point_.z * kReticleOffset);

  gvr::Mat4f transform = MatrixMul(render_matrix, mat);
  vr_shell_renderer_->GetReticleRenderer()->Draw(transform);

  // Draw the laser.

  // Find the length of the beam (from hand to target).
  float laser_length = Distance(kHandPosition, target_point_);

  // Build a beam, originating from the origin.
  SetIdentityM(mat);

  // Move the beam half its height so that its end sits on the origin.
  TranslateM(mat, mat, 0.0f, 0.5f, 0.0f);
  ScaleM(mat, mat, kLaserWidth, laser_length, 1);

  // Tip back 90 degrees to flat, pointing at the scene.
  auto q = QuatFromAxisAngle({1.0f, 0.0f, 0.0f}, -M_PI / 2);
  auto m = QuatToMatrix(q);
  mat = MatrixMul(m, mat);

  // Orient according to controller position.
  mat = MatrixMul(QuatToMatrix(controller_quat_), mat);

  // Move the beam origin to the hand.
  TranslateM(mat, mat, kHandPosition.x, kHandPosition.y, kHandPosition.z);

  transform = MatrixMul(render_matrix, mat);
  vr_shell_renderer_->GetLaserRenderer()->Draw(transform);
}

void VrShell::DrawWebVr() {
  // Don't need face culling, depth testing, blending, etc. Turn it all off.
  glDisable(GL_CULL_FACE);
  glDepthMask(GL_FALSE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_POLYGON_OFFSET_FILL);

  // Don't need to clear, since we're drawing over the entire render target.

  glViewport(0, 0, render_size_.width, render_size_.height);
  vr_shell_renderer_->GetWebVrRenderer()->Draw(content_texture_id_);
}

void VrShell::DrawWebVrOverlay(int64_t present_time_nanos) {
  // Draw WebVR security warning overlays for each eye. This uses the
  // eye-from-head matrices but not the pose, goal is to place the icons in an
  // eye-relative position so that they follow along with head rotations.

  gvr::Mat4f left_eye_view_matrix =
      gvr_api_->GetEyeFromHeadMatrix(GVR_LEFT_EYE);
  gvr::Mat4f right_eye_view_matrix =
      gvr_api_->GetEyeFromHeadMatrix(GVR_RIGHT_EYE);

  buffer_viewport_list_->GetBufferViewport(GVR_LEFT_EYE,
                                           buffer_viewport_.get());
  DrawWebVrEye(left_eye_view_matrix, *buffer_viewport_, present_time_nanos);
  buffer_viewport_list_->GetBufferViewport(GVR_RIGHT_EYE,
                                           buffer_viewport_.get());
  DrawWebVrEye(right_eye_view_matrix, *buffer_viewport_, present_time_nanos);
}

void VrShell::DrawWebVrEye(const gvr::Mat4f& view_matrix,
                           const gvr::BufferViewport& params,
                           int64_t present_time_nanos) {
  gvr::Recti pixel_rect =
      CalculatePixelSpaceRect(render_size_, params.GetSourceUv());
  glViewport(pixel_rect.left, pixel_rect.bottom,
             pixel_rect.right - pixel_rect.left,
             pixel_rect.top - pixel_rect.bottom);
  glScissor(pixel_rect.left, pixel_rect.bottom,
            pixel_rect.right - pixel_rect.left,
            pixel_rect.top - pixel_rect.bottom);

  gvr::Mat4f projection_matrix =
      PerspectiveMatrixFromView(params.GetSourceFov(), kZNear, kZFar);

  if (!IsUiTextureReady()) {
    // If the UI texture hasn't been initialized yet, we can't draw the overlay.
    return;
  }

  // Show IDS_WEBSITE_SETTINGS_INSECURE_WEBVR_CONTENT_PERMANENT text.
  gvr::Mat4f icon_pos;
  SetIdentityM(icon_pos);
  // The UI is designed in pixels with the assumption that 1px = 1mm at 1m
  // distance. Scale mm-to-m and adjust to keep the same angular size if the
  // distance changes.
  const float small_icon_width =
      kWebVrWarningPermanentRect.width / 1000.f * kWebVrWarningDistance;
  const float small_icon_height =
      kWebVrWarningPermanentRect.height / 1000.f * kWebVrWarningDistance;
  const float small_icon_angle =
      kWebVrWarningPermanentAngle * M_PI / 180.f;  // Degrees to radians.
  ScaleM(icon_pos, icon_pos, small_icon_width, small_icon_height, 1.0f);
  TranslateM(icon_pos, icon_pos, 0.0f, 0.0f, -kWebVrWarningDistance);
  icon_pos = MatrixMul(
      QuatToMatrix(QuatFromAxisAngle({1.f, 0.f, 0.f}, small_icon_angle)),
      icon_pos);
  gvr::Mat4f combined = MatrixMul(projection_matrix,
                                  MatrixMul(view_matrix, icon_pos));
  vr_shell_renderer_->GetTexturedQuadRenderer()->Draw(
      ui_texture_id_, combined, MakeUiGlCopyRect(kWebVrWarningPermanentRect));

  // Check if we also need to show the transient warning.
  if (present_time_nanos > webvr_warning_end_nanos_) {
    return;
  }

  // Show IDS_WEBSITE_SETTINGS_INSECURE_WEBVR_CONTENT_TRANSIENT text.
  SetIdentityM(icon_pos);
  const float large_icon_width =
      kWebVrWarningTransientRect.width / 1000.f * kWebVrWarningDistance;
  const float large_icon_height =
      kWebVrWarningTransientRect.height / 1000.f * kWebVrWarningDistance;
  ScaleM(icon_pos, icon_pos, large_icon_width, large_icon_height, 1.0f);
  TranslateM(icon_pos, icon_pos, 0.0f, 0.0f, -kWebVrWarningDistance);
  combined = MatrixMul(projection_matrix,
                       MatrixMul(view_matrix, icon_pos));
  vr_shell_renderer_->GetTexturedQuadRenderer()->Draw(
      ui_texture_id_, combined, MakeUiGlCopyRect(kWebVrWarningTransientRect));

}

void VrShell::OnPause(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  if (gvr_api_ == nullptr)
    return;
  gvr_api_->PauseTracking();
}

void VrShell::OnResume(JNIEnv* env, const JavaParamRef<jobject>& obj) {
  if (gvr_api_ == nullptr)
    return;

  gvr_api_->RefreshViewerProfile();
  gvr_api_->ResumeTracking();
}

base::WeakPtr<VrShell> VrShell::GetWeakPtr() {
  // TODO: Ensure that only ui webcontents can request this weak ptr.
  if (g_instance != nullptr)
    return g_instance->weak_ptr_factory_.GetWeakPtr();
  return base::WeakPtr<VrShell>(nullptr);
}

void VrShell::OnDomContentsLoaded() {
  // TODO(mthiesse): Setting the background to transparent after the DOM content
  // has loaded is a hack to work around the background not updating when we set
  // it to transparent unless we perform a very specific sequence of events.
  // First the page background must load as not transparent, then we set the
  // background of the renderer to transparent, then we update the page
  // background to be transparent. This is probably a bug in blink that we
  // should fix.
  ui_cvc_->GetWebContents()->GetRenderWidgetHostView()->SetBackgroundColor(
      SK_ColorTRANSPARENT);
}

void VrShell::SetUiTextureSize(int width, int height) {
  // TODO(bshe): ui_text_width_ and ui_tex_height_ should be only used on render
  // thread.
  ui_tex_width_ = width;
  ui_tex_height_ = height;
}

void VrShell::SetWebVrMode(JNIEnv* env,
                           const base::android::JavaParamRef<jobject>& obj,
                           bool enabled) {
  webvr_mode_ = enabled;
  if (enabled) {
    int64_t now = gvr::GvrApi::GetTimePointNow().monotonic_system_time_nanos;
    constexpr int64_t seconds_to_nanos = 1000 * 1000 * 1000;
    webvr_warning_end_nanos_ = now + kWebVrWarningSeconds * seconds_to_nanos;
  } else {
    webvr_warning_end_nanos_ = 0;
  }
}

void VrShell::SetWebVRSecureOrigin(bool secure_origin) {
  webvr_secure_origin_ = secure_origin;
}

void VrShell::SubmitWebVRFrame() {
}

void VrShell::UpdateWebVRTextureBounds(
    int eye, float left, float top, float width, float height) {
  gvr::Rectf bounds = { left, top, width, height };
  vr_shell_renderer_->GetWebVrRenderer()->UpdateTextureBounds(eye, bounds);
}

gvr::GvrApi* VrShell::gvr_api() {
  return gvr_api_.get();
}

void VrShell::ContentSurfaceChanged(JNIEnv* env,
                                    const JavaParamRef<jobject>& object,
                                    jint width,
                                    jint height,
                                    const JavaParamRef<jobject>& surface) {
  content_compositor_->SurfaceChanged((int)width, (int)height, surface);
}

void VrShell::UiSurfaceChanged(JNIEnv* env,
                               const JavaParamRef<jobject>& object,
                               jint width,
                               jint height,
                               const JavaParamRef<jobject>& surface) {
  ui_compositor_->SurfaceChanged((int)width, (int)height, surface);
}

UiScene* VrShell::GetScene() {
  return &scene_;
}

void VrShell::QueueTask(base::Callback<void()>& callback) {
  base::AutoLock lock(task_queue_lock_);
  task_queue_.push(callback);
}

void VrShell::HandleQueuedTasks() {
  // To protect a stream of tasks from blocking rendering indefinitely,
  // process only the number of tasks present when first checked.
  std::vector<base::Callback<void()>> tasks;
  {
    base::AutoLock lock(task_queue_lock_);
    const size_t count = task_queue_.size();
    for (size_t i = 0; i < count; i++) {
      tasks.push_back(task_queue_.front());
      task_queue_.pop();
    }
  }
  for (auto &task : tasks) {
    task.Run();
  }
}

// ----------------------------------------------------------------------------
// Native JNI methods
// ----------------------------------------------------------------------------

jlong Init(JNIEnv* env,
           const JavaParamRef<jobject>& obj,
           const JavaParamRef<jobject>& content_web_contents,
           jlong content_window_android,
           const JavaParamRef<jobject>& ui_web_contents,
           jlong ui_window_android) {
  content::ContentViewCore* c_core = content::ContentViewCore::FromWebContents(
      content::WebContents::FromJavaWebContents(content_web_contents));
  content::ContentViewCore* ui_core = content::ContentViewCore::FromWebContents(
      content::WebContents::FromJavaWebContents(ui_web_contents));
  return reinterpret_cast<intptr_t>(new VrShell(
      env, obj, c_core,
      reinterpret_cast<ui::WindowAndroid*>(content_window_android), ui_core,
      reinterpret_cast<ui::WindowAndroid*>(ui_window_android)));
}

}  // namespace vr_shell

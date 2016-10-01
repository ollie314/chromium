// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_ANDROID_VR_SHELL_VR_CONTROLLER_H_
#define CHROME_BROWSER_ANDROID_VR_SHELL_VR_CONTROLLER_H_

#include "base/memory/ref_counted.h"
#include "chrome/browser/android/vr_shell/vr_gesture.h"
#include "third_party/gvr-android-sdk/src/ndk-beta/include/vr/gvr/capi/include/gvr.h"
#include "third_party/gvr-android-sdk/src/ndk-beta/include/vr/gvr/capi/include/gvr_controller.h"
#include "third_party/gvr-android-sdk/src/ndk-beta/include/vr/gvr/capi/include/gvr_types.h"

namespace vr_shell {

class VrController {
 public:
  // Controller API entry point.
  explicit VrController(gvr_context* gvr_context);
  ~VrController();

  // Must be called when the Activity gets OnResume().
  void OnResume();

  // Must be called when the Activity gets OnPause().
  void OnPause();

  // Must be called when the GL renderer gets OnSurfaceCreated().
  void Initialize(gvr_context* gvr_context);

  // Must be called when the GL renderer gets OnDrawFrame().
  void UpdateState();
  VrGesture DetectGesture();

  bool IsTouching();

  float TouchPosX();

  float TouchPosY();

  const gvr::Quatf Orientation();

  bool IsTouchDown();

  bool IsTouchUp();

  bool IsButtonUp(const int32_t button);
  bool IsButtonDown(const int32_t button);

  bool IsConnected();

 private:
  enum GestureDetectorState {
    WAITING,   // waiting for user to touch down
    TOUCHING,  // touching the touch pad but not scrolling
    SCROLLING  // scrolling on the touch pad
  };

  struct TouchPoint {
    gvr::Vec2f position;
    int64_t timestamp;
  };

  struct TouchInfo {
    TouchPoint touch_point;
    bool touch_up;
    bool touch_down;
    bool is_touching;
  };

  struct ButtonInfo {
    gvr::ControllerButton button;
    bool button_up;
    bool button_down;
    bool button_state;
    int64_t timestamp;
  };

  void UpdateGestureFromTouchInfo();

  bool GetButtonLongPressFromButtonInfo();

  // Handle the waiting state.
  void HandleWaitingState();

  // Handle the detecting state.
  void HandleDetectingState();

  // Handle the scrolling state.
  void HandleScrollingState();
  void Update(const gvr_controller_state* controller_state);
  void Update(bool touch_up,
              bool touch_down,
              bool is_touching,
              const gvr::Vec2f position,
              int64_t timestamp);

  // Returns true if the touch position is within the slop of the initial touch
  // point, false otherwise.
  bool InSlop(const gvr::Vec2f touch_position);

  void Reset();

  size_t GetGestureListSize() { return gesture_list_.size(); }

  const VrGesture* GetGesturePtr(const size_t index);

  // Update gesture parameters,
  void UpdateGesture(VrGesture* gesture);

  // If the user is touching the touch pad and the touch point is different from
  // before, update the touch point and return true. Otherwise, return false.
  bool UpdateCurrentTouchpoint();

  // State of gesture detector.
  GestureDetectorState state_;

  std::unique_ptr<gvr::ControllerApi> controller_api_;

  // The last controller state (updated once per frame).
  gvr::ControllerState controller_state_;

  float last_qx_;
  bool pinch_started_;
  bool zoom_in_progress_ = false;

  std::vector<VrGesture> gesture_list_;
  std::unique_ptr<TouchInfo> touch_info_;

  // A pointer storing the touch point from previous frame.
  std::unique_ptr<TouchPoint> prev_touch_point_;

  // A pointer storing the touch point from current frame.
  std::unique_ptr<TouchPoint> cur_touch_point_;

  // Initial touch point.
  std::unique_ptr<TouchPoint> init_touch_point_;

  // Overall velocity
  gvr::Vec2f overall_velocity_;

  DISALLOW_COPY_AND_ASSIGN(VrController);
};

}  // namespace vr_shell

#endif  // CHROME_BROWSER_ANDROID_VR_SHELL_VR_CONTROLLER_H_

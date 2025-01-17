// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/fullscreen/fullscreen_toolbar_animation_controller.h"

#import "chrome/browser/ui/cocoa/fullscreen_toolbar_controller.h"

namespace {

// The duration of the toolbar show/hide animation in ms.
const NSTimeInterval kToolbarAnimationDuration = 200;

// If the fullscreen toolbar is hidden, it is difficult for the user to see
// changes in the tabstrip. As a result, if a tab is inserted or the current
// tab switched to a new one, the toolbar must animate in and out to display
// the tabstrip changes to the user. The animation drops down the toolbar and
// then wait for 0.75 seconds before it hides the toolbar.
const NSTimeInterval kTabStripChangesDelay = 750;

}  // end namespace

//////////////////////////////////////////////////////////////////
// FullscreenToolbarAnimationController, public:

FullscreenToolbarAnimationController::FullscreenToolbarAnimationController(
    FullscreenToolbarController* owner)
    : owner_(owner),
      animation_(this),
      hide_toolbar_timer_(
          FROM_HERE,
          base::TimeDelta::FromMilliseconds(kTabStripChangesDelay),
          base::Bind(&FullscreenToolbarAnimationController::
                         AnimateToolbarOutIfPossible,
                     base::Unretained(this)),
          false),
      animation_start_value_(0),
      should_hide_toolbar_after_delay_(false) {
  animation_.SetSlideDuration(kToolbarAnimationDuration);
  animation_.SetTweenType(gfx::Tween::EASE_OUT);
}

void FullscreenToolbarAnimationController::ToolbarDidUpdate() {
  animation_start_value_ = [owner_ toolbarFraction];
}

void FullscreenToolbarAnimationController::StopAnimationAndTimer() {
  animation_.Stop();
  hide_toolbar_timer_.Stop();
}

void FullscreenToolbarAnimationController::AnimateToolbarForTabstripChanges() {
  // Don't kickstart the animation if the toolbar is already displayed.
  if ([owner_ mustShowFullscreenToolbar])
    return;

  AnimateToolbarIn();
  should_hide_toolbar_after_delay_ = true;
}

void FullscreenToolbarAnimationController::AnimateToolbarIn() {
  if (![owner_ isInFullscreen])
    return;

  if (animation_.IsShowing())
    return;

  animation_.Reset(animation_start_value_);
  animation_.Show();
}

void FullscreenToolbarAnimationController::AnimateToolbarOutIfPossible() {
  if (![owner_ isInFullscreen] || [owner_ mustShowFullscreenToolbar])
    return;

  if (animation_.IsClosing())
    return;

  animation_.Reset(animation_start_value_);
  animation_.Hide();
}

CGFloat FullscreenToolbarAnimationController::GetToolbarFractionFromProgress()
    const {
  return animation_.GetCurrentValue();
}

bool FullscreenToolbarAnimationController::IsAnimationRunning() const {
  return animation_.is_animating();
}

//////////////////////////////////////////////////////////////////
// FullscreenToolbarAnimationController::AnimationDelegate:

void FullscreenToolbarAnimationController::AnimationProgressed(
    const gfx::Animation* animation) {
  [owner_ updateToolbar];
}

void FullscreenToolbarAnimationController::AnimationEnded(
    const gfx::Animation* animation) {
  if (animation_.IsShowing() && should_hide_toolbar_after_delay_) {
    hide_toolbar_timer_.Reset();
    should_hide_toolbar_after_delay_ = false;
  }
}

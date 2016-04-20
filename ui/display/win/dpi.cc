// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/win/dpi.h"

#include <windows.h>

#include "base/win/scoped_hdc.h"
#include "ui/gfx/display.h"

namespace {

const float kDefaultDPI = 96.f;

float g_device_scale_factor = 0.f;

float GetUnforcedDeviceScaleFactor() {
  return g_device_scale_factor ?
      g_device_scale_factor :
      static_cast<float>(display::win::GetDPI().width()) / kDefaultDPI;
}

}  // namespace

namespace display {
namespace win {

void SetDefaultDeviceScaleFactor(float scale) {
  DCHECK_NE(0.f, scale);
  g_device_scale_factor = scale;
}

gfx::Size GetDPI() {
  static int dpi_x = 0;
  static int dpi_y = 0;
  static bool should_initialize = true;

  if (should_initialize) {
    should_initialize = false;
    base::win::ScopedGetDC screen_dc(NULL);
    // This value is safe to cache for the life time of the app since the
    // user must logout to change the DPI setting. This value also applies
    // to all screens.
    dpi_x = GetDeviceCaps(screen_dc, LOGPIXELSX);
    dpi_y = GetDeviceCaps(screen_dc, LOGPIXELSY);
  }
  return gfx::Size(dpi_x, dpi_y);
}

float GetDPIScale() {
  if (gfx::Display::HasForceDeviceScaleFactor())
    return gfx::Display::GetForcedDeviceScaleFactor();
  float dpi_scale = GetUnforcedDeviceScaleFactor();
  return (dpi_scale <= 1.25f) ? 1.f : dpi_scale;
}

int GetSystemMetricsInDIP(int metric) {
  // The system metrics always reflect the system DPI, not whatever scale we've
  // forced or decided to use.
  return static_cast<int>(
      std::round(GetSystemMetrics(metric) / GetUnforcedDeviceScaleFactor()));
}

}  // namespace win
}  // namespace display

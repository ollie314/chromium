// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/system/chromeos/tray_tracing.h"

#include "ash/common/material_design/material_design_controller.h"
#include "ash/common/metrics/user_metrics_action.h"
#include "ash/common/system/tray/actionable_view.h"
#include "ash/common/system/tray/fixed_sized_image_view.h"
#include "ash/common/system/tray/system_tray.h"
#include "ash/common/system/tray/system_tray_controller.h"
#include "ash/common/system/tray/system_tray_notifier.h"
#include "ash/common/system/tray/tray_constants.h"
#include "ash/common/system/tray/tray_popup_item_style.h"
#include "ash/common/system/tray/tray_popup_utils.h"
#include "ash/common/wm_shell.h"
#include "grit/ash_resources.h"
#include "grit/ash_strings.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/vector_icons_public.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace ash {
namespace tray {

class DefaultTracingView : public ActionableView {
 public:
  explicit DefaultTracingView(SystemTrayItem* owner) : ActionableView(owner) {
    SetLayoutManager(new views::BoxLayout(views::BoxLayout::kHorizontal,
                                          kTrayPopupPaddingHorizontal, 0,
                                          kTrayPopupPaddingBetweenItems));

    ui::ResourceBundle& bundle = ui::ResourceBundle::GetSharedInstance();
    image_ =
        new FixedSizedImageView(0, GetTrayConstant(TRAY_POPUP_ITEM_HEIGHT));
    if (!MaterialDesignController::UseMaterialDesignSystemIcons()) {
      // The icon doesn't change in non-md.
      image_->SetImage(
          bundle.GetImageNamed(IDR_AURA_UBER_TRAY_TRACING).ToImageSkia());
    }
    AddChildView(image_);

    label_ = TrayPopupUtils::CreateDefaultLabel();
    label_->SetMultiLine(true);
    label_->SetText(bundle.GetLocalizedString(IDS_ASH_STATUS_TRAY_TRACING));
    AddChildView(label_);

    if (MaterialDesignController::IsSystemTrayMenuMaterial())
      SetInkDropMode(InkDropHostView::InkDropMode::ON);
  }

  ~DefaultTracingView() override {}

 private:
  // ActionableView:
  void OnNativeThemeChanged(const ui::NativeTheme* theme) override {
    ActionableView::OnNativeThemeChanged(theme);

    if (!MaterialDesignController::IsSystemTrayMenuMaterial())
      return;

    TrayPopupItemStyle style(GetNativeTheme(),
                             TrayPopupItemStyle::FontStyle::DEFAULT_VIEW_LABEL);
    style.SetupLabel(label_);

    // TODO(tdanderson): Update the icon used for tracing or remove it from
    // the system menu. See crbug.com/625691.
    image_->SetImage(CreateVectorIcon(gfx::VectorIconId::CODE, kMenuIconSize,
                                      style.GetIconColor()));
  }

  bool PerformAction(const ui::Event& event) override {
    WmShell::Get()->RecordUserMetricsAction(
        UMA_STATUS_AREA_TRACING_DEFAULT_SELECTED);
    WmShell::Get()->system_tray_controller()->ShowChromeSlow();
    CloseSystemBubble();
    return true;
  }

  views::ImageView* image_;
  views::Label* label_;

  DISALLOW_COPY_AND_ASSIGN(DefaultTracingView);
};

}  // namespace tray

////////////////////////////////////////////////////////////////////////////////
// ash::TrayTracing

TrayTracing::TrayTracing(SystemTray* system_tray)
    : TrayImageItem(system_tray, IDR_AURA_UBER_TRAY_TRACING, UMA_TRACING),
      default_(nullptr) {
  DCHECK(system_tray);
  WmShell::Get()->system_tray_notifier()->AddTracingObserver(this);
}

TrayTracing::~TrayTracing() {
  WmShell::Get()->system_tray_notifier()->RemoveTracingObserver(this);
}

void TrayTracing::SetTrayIconVisible(bool visible) {
  if (tray_view())
    tray_view()->SetVisible(visible);
}

bool TrayTracing::GetInitialVisibility() {
  return false;
}

views::View* TrayTracing::CreateDefaultView(LoginStatus status) {
  CHECK(default_ == NULL);
  if (tray_view() && tray_view()->visible())
    default_ = new tray::DefaultTracingView(this);
  return default_;
}

views::View* TrayTracing::CreateDetailedView(LoginStatus status) {
  return NULL;
}

void TrayTracing::DestroyDefaultView() {
  default_ = NULL;
}

void TrayTracing::DestroyDetailedView() {}

void TrayTracing::OnTracingModeChanged(bool value) {
  SetTrayIconVisible(value);
}

}  // namespace ash

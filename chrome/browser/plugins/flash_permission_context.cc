// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/plugins/flash_permission_context.h"

#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/infobars/infobar_service.h"
#include "chrome/browser/permissions/permission_request_id.h"
#include "chrome/browser/plugins/plugin_utils.h"
#include "chrome/browser/plugins/plugins_field_trial.h"
#include "chrome/browser/ui/website_settings/website_settings_infobar_delegate.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "content/public/browser/render_frame_host.h"

FlashPermissionContext::FlashPermissionContext(Profile* profile)
    : PermissionContextBase(profile,
                            content::PermissionType::FLASH,
                            CONTENT_SETTINGS_TYPE_PLUGINS) {}

FlashPermissionContext::~FlashPermissionContext() {}

ContentSetting FlashPermissionContext::GetPermissionStatus(
    const GURL& requesting_origin,
    const GURL& embedding_origin) const {
  ContentSetting flash_setting = PluginUtils::GetFlashPluginContentSetting(
      HostContentSettingsMapFactory::GetForProfile(profile()), embedding_origin,
      requesting_origin);
  flash_setting = PluginsFieldTrial::EffectiveContentSetting(
      CONTENT_SETTINGS_TYPE_PLUGINS, flash_setting);
  if (flash_setting == CONTENT_SETTING_DETECT_IMPORTANT_CONTENT)
    return CONTENT_SETTING_ASK;
  return flash_setting;
}

void FlashPermissionContext::UpdateTabContext(const PermissionRequestID& id,
                                              const GURL& requesting_origin,
                                              bool allowed) {
  if (!allowed)
    return;
  content::WebContents* web_contents =
      content::WebContents::FromRenderFrameHost(
          content::RenderFrameHost::FromID(id.render_process_id(),
                                           id.render_frame_id()));
  InfoBarService* infobar_service =
      InfoBarService::FromWebContents(web_contents);
  if (infobar_service)
    WebsiteSettingsInfoBarDelegate::Create(infobar_service);
}

bool FlashPermissionContext::IsRestrictedToSecureOrigins() const {
  return false;
}

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/plugins/plugin_utils.h"

#include "base/values.h"
#include "chrome/common/plugin_utils.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "content/public/common/webplugininfo.h"
#include "url/gurl.h"

namespace {

const char kFlashPluginID[] = "adobe-flash-player";

void GetPluginContentSettingInternal(
    const HostContentSettingsMap* host_content_settings_map,
    bool use_javascript_setting,
    const GURL& policy_url,
    const GURL& plugin_url,
    const std::string& resource,
    ContentSetting* setting,
    bool* uses_default_content_setting,
    bool* is_managed) {
  std::unique_ptr<base::Value> value;
  content_settings::SettingInfo info;
  bool uses_plugin_specific_setting = false;
  if (use_javascript_setting) {
    value = host_content_settings_map->GetWebsiteSetting(
        policy_url, policy_url, CONTENT_SETTINGS_TYPE_JAVASCRIPT, std::string(),
        &info);
  } else {
    content_settings::SettingInfo specific_info;
    std::unique_ptr<base::Value> specific_setting =
        host_content_settings_map->GetWebsiteSetting(
            policy_url, plugin_url, CONTENT_SETTINGS_TYPE_PLUGINS, resource,
            &specific_info);
    content_settings::SettingInfo general_info;
    std::unique_ptr<base::Value> general_setting =
        host_content_settings_map->GetWebsiteSetting(
            policy_url, plugin_url, CONTENT_SETTINGS_TYPE_PLUGINS,
            std::string(), &general_info);
    // If there is a plugin-specific setting, we use it, unless the general
    // setting was set by policy, in which case it takes precedence.
    uses_plugin_specific_setting =
        specific_setting &&
        general_info.source != content_settings::SETTING_SOURCE_POLICY;
    if (uses_plugin_specific_setting) {
      value = std::move(specific_setting);
      info = specific_info;
    } else {
      value = std::move(general_setting);
      info = general_info;
    }
  }
  *setting = content_settings::ValueToContentSetting(value.get());
  if (uses_default_content_setting) {
    *uses_default_content_setting =
        !uses_plugin_specific_setting &&
        info.primary_pattern == ContentSettingsPattern::Wildcard() &&
        info.secondary_pattern == ContentSettingsPattern::Wildcard();
  }
  if (is_managed)
    *is_managed = info.source == content_settings::SETTING_SOURCE_POLICY;
}

}  // namespace

// static
void PluginUtils::GetPluginContentSetting(
    const HostContentSettingsMap* host_content_settings_map,
    const content::WebPluginInfo& plugin,
    const GURL& policy_url,
    const GURL& plugin_url,
    const std::string& resource,
    ContentSetting* setting,
    bool* uses_default_content_setting,
    bool* is_managed) {
  GetPluginContentSettingInternal(host_content_settings_map,
                                  ShouldUseJavaScriptSettingForPlugin(plugin),
                                  policy_url, plugin_url, resource, setting,
                                  uses_default_content_setting, is_managed);
}

// static
ContentSetting PluginUtils::GetFlashPluginContentSetting(
    const HostContentSettingsMap* host_content_settings_map,
    const GURL& policy_url,
    const GURL& plugin_url) {
  ContentSetting plugin_setting = CONTENT_SETTING_DEFAULT;
  GetPluginContentSettingInternal(
      host_content_settings_map, false /* use_javascript_setting */, policy_url,
      plugin_url, kFlashPluginID, &plugin_setting, nullptr, nullptr);
  return plugin_setting;
}

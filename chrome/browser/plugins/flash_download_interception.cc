// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/plugins/flash_download_interception.h"

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_util.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/permissions/permission_manager.h"
#include "chrome/browser/plugins/plugin_utils.h"
#include "chrome/browser/plugins/plugins_field_trial.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_features.h"
#include "components/navigation_interception/intercept_navigation_throttle.h"
#include "components/navigation_interception/navigation_params.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/permission_type.h"
#include "content/public/browser/web_contents.h"
#include "third_party/WebKit/public/platform/modules/permissions/permission_status.mojom.h"

using content::BrowserThread;
using content::NavigationHandle;
using content::NavigationThrottle;

namespace {

const char kFlashDownloadURL[] = "get.adobe.com/flash";

void DoNothing(blink::mojom::PermissionStatus result) {}

bool ShouldInterceptNavigation(
    content::WebContents* source,
    const navigation_interception::NavigationParams& params) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  PermissionManager* manager = PermissionManager::Get(
      Profile::FromBrowserContext(source->GetBrowserContext()));
  manager->RequestPermission(
      content::PermissionType::FLASH, source->GetMainFrame(),
      source->GetLastCommittedURL(), true, base::Bind(&DoNothing));

  return true;
}

}  // namespace

// static
bool FlashDownloadInterception::ShouldStopFlashDownloadAction(
    HostContentSettingsMap* host_content_settings_map,
    const GURL& source_url,
    const GURL& target_url,
    bool has_user_gesture) {
  if (!base::FeatureList::IsEnabled(features::kPreferHtmlOverPlugins))
    return false;

  if (!has_user_gesture)
    return false;

  if (!base::StartsWith(target_url.GetContent(), kFlashDownloadURL,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    return false;
  }

  ContentSetting flash_setting = PluginUtils::GetFlashPluginContentSetting(
      host_content_settings_map, source_url, source_url);
  flash_setting = PluginsFieldTrial::EffectiveContentSetting(
      CONTENT_SETTINGS_TYPE_PLUGINS, flash_setting);

  return flash_setting == CONTENT_SETTING_DETECT_IMPORTANT_CONTENT;
}

// static
std::unique_ptr<NavigationThrottle>
FlashDownloadInterception::MaybeCreateThrottleFor(NavigationHandle* handle) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  // Never intercept Flash Download navigations in a new window.
  if (handle->GetWebContents()->HasOpener())
    return nullptr;

  Profile* profile = Profile::FromBrowserContext(
      handle->GetWebContents()->GetBrowserContext());
  HostContentSettingsMap* host_content_settings_map =
      HostContentSettingsMapFactory::GetForProfile(profile);
  GURL source_url = handle->GetWebContents()->GetLastCommittedURL();
  if (!ShouldStopFlashDownloadAction(host_content_settings_map, source_url,
                                     handle->GetURL(),
                                     handle->HasUserGesture())) {
    return nullptr;
  }

  return base::MakeUnique<navigation_interception::InterceptNavigationThrottle>(
      handle, base::Bind(&ShouldInterceptNavigation), true);
}

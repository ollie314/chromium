// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/chrome/browser/ui/omnibox/omnibox_util.h"

#include "base/logging.h"
#include "ios/chrome/grit/ios_theme_resources.h"

int GetIconForAutocompleteMatchType(AutocompleteMatchType::Type type,
                                    bool is_starred,
                                    bool is_incognito) {
  if (is_starred)
    return is_incognito ? IDR_IOS_OMNIBOX_STAR_INCOGNITO : IDR_IOS_OMNIBOX_STAR;

  switch (type) {
    case AutocompleteMatchType::BOOKMARK_TITLE:
    case AutocompleteMatchType::CLIPBOARD:
    case AutocompleteMatchType::NAVSUGGEST:
    case AutocompleteMatchType::NAVSUGGEST_PERSONALIZED:
    case AutocompleteMatchType::PHYSICAL_WEB:
    case AutocompleteMatchType::PHYSICAL_WEB_OVERFLOW:
    case AutocompleteMatchType::URL_WHAT_YOU_TYPED:
      return is_incognito ? IDR_IOS_OMNIBOX_HTTP_INCOGNITO
                          : IDR_IOS_OMNIBOX_HTTP;
    case AutocompleteMatchType::HISTORY_BODY:
    case AutocompleteMatchType::HISTORY_KEYWORD:
    case AutocompleteMatchType::HISTORY_TITLE:
    case AutocompleteMatchType::HISTORY_URL:
    case AutocompleteMatchType::SEARCH_HISTORY:
      return is_incognito ? IDR_IOS_OMNIBOX_HISTORY_INCOGNITO
                          : IDR_IOS_OMNIBOX_HISTORY;
    case AutocompleteMatchType::CONTACT_DEPRECATED:
    case AutocompleteMatchType::SEARCH_OTHER_ENGINE:
    case AutocompleteMatchType::SEARCH_SUGGEST:
    case AutocompleteMatchType::SEARCH_SUGGEST_ENTITY:
    case AutocompleteMatchType::SEARCH_SUGGEST_PERSONALIZED:
    case AutocompleteMatchType::SEARCH_SUGGEST_PROFILE:
    case AutocompleteMatchType::SEARCH_SUGGEST_TAIL:
    case AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED:
    case AutocompleteMatchType::VOICE_SUGGEST:
      return is_incognito ? IDR_IOS_OMNIBOX_SEARCH_INCOGNITO
                          : IDR_IOS_OMNIBOX_SEARCH;
    case AutocompleteMatchType::CALCULATOR:
    case AutocompleteMatchType::EXTENSION_APP:
    case AutocompleteMatchType::NUM_TYPES:
      NOTREACHED();
      return IDR_IOS_OMNIBOX_HTTP;
  }
}

int GetIconForSecurityState(
    security_state::SecurityStateModel::SecurityLevel security_level) {
  switch (security_level) {
    case security_state::SecurityStateModel::NONE:
    case security_state::SecurityStateModel::HTTP_SHOW_WARNING:
      return IDR_IOS_OMNIBOX_HTTP;
    case security_state::SecurityStateModel::EV_SECURE:
    case security_state::SecurityStateModel::SECURE:
      return IDR_IOS_OMNIBOX_HTTPS_VALID;
    case security_state::SecurityStateModel::SECURITY_WARNING:
      // Surface Dubious as Neutral.
      return IDR_IOS_OMNIBOX_HTTP;
    case security_state::SecurityStateModel::SECURE_WITH_POLICY_INSTALLED_CERT:
      return IDR_IOS_OMNIBOX_HTTPS_POLICY_WARNING;
    case security_state::SecurityStateModel::DANGEROUS:
      return IDR_IOS_OMNIBOX_HTTPS_INVALID;
  }
}

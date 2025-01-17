// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_server_properties.h"

#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/stringprintf.h"
#include "net/socket/ssl_client_socket.h"
#include "net/ssl/ssl_config.h"

namespace net {

namespace {

enum AlternativeProxyUsage {
  // Alternative Proxy was used without racing a normal connection.
  ALTERNATIVE_PROXY_USAGE_NO_RACE = 0,
  // Alternative Proxy was used by winning a race with a normal connection.
  ALTERNATIVE_PROXY_USAGE_WON_RACE = 1,
  // Alternative Proxy was not used by losing a race with a normal connection.
  ALTERNATIVE_PROXY_USAGE_LOST_RACE = 2,
  // Maximum value for the enum.
  ALTERNATIVE_PROXY_USAGE_MAX,
};

AlternativeProxyUsage ConvertProtocolUsageToProxyUsage(
    AlternateProtocolUsage usage) {
  switch (usage) {
    case ALTERNATE_PROTOCOL_USAGE_NO_RACE:
      return ALTERNATIVE_PROXY_USAGE_NO_RACE;
    case ALTERNATE_PROTOCOL_USAGE_WON_RACE:
      return ALTERNATIVE_PROXY_USAGE_WON_RACE;
    case ALTERNATE_PROTOCOL_USAGE_LOST_RACE:
      return ALTERNATIVE_PROXY_USAGE_LOST_RACE;
    default:
      NOTREACHED();
      return ALTERNATIVE_PROXY_USAGE_MAX;
  }
}

}  // namespace anonymous

const char kAlternativeServiceHeader[] = "Alt-Svc";

void HistogramAlternateProtocolUsage(AlternateProtocolUsage usage,
                                     bool proxy_server_used) {
  if (proxy_server_used) {
    DCHECK_LE(usage, ALTERNATE_PROTOCOL_USAGE_LOST_RACE);
    UMA_HISTOGRAM_ENUMERATION("Net.QuicAlternativeProxy.Usage",
                              ConvertProtocolUsageToProxyUsage(usage),
                              ALTERNATIVE_PROXY_USAGE_MAX);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Net.AlternateProtocolUsage", usage,
                              ALTERNATE_PROTOCOL_USAGE_MAX);
  }
}

void HistogramBrokenAlternateProtocolLocation(
    BrokenAlternateProtocolLocation location){
  UMA_HISTOGRAM_ENUMERATION("Net.AlternateProtocolBrokenLocation", location,
                            BROKEN_ALTERNATE_PROTOCOL_LOCATION_MAX);
}

bool IsAlternateProtocolValid(AlternateProtocol protocol) {
  switch (protocol) {
    case NPN_HTTP_2:
      return true;
    case QUIC:
      return true;
    case UNINITIALIZED_ALTERNATE_PROTOCOL:
      return false;
  }
  NOTREACHED();
  return false;
}

const char* AlternateProtocolToString(AlternateProtocol protocol) {
  switch (protocol) {
    case QUIC:
      return "quic";
    case NPN_HTTP_2:
      return "h2";
    case UNINITIALIZED_ALTERNATE_PROTOCOL:
      return "Uninitialized";
  }
  NOTREACHED();
  return "";
}

AlternateProtocol AlternateProtocolFromString(const std::string& str) {
  if (str == "quic")
    return QUIC;
  if (str == "h2")
    return NPN_HTTP_2;
  // "npn-h2" and "npn-spdy/3.1" are accepted here so that persisted settings
  // with the old string can be loaded from disk.  TODO(bnc):  Remove around
  // 2016 December.
  if (str == "npn-h2")
    return NPN_HTTP_2;
  if (str == "npn-spdy/3.1")
    return NPN_HTTP_2;

  return UNINITIALIZED_ALTERNATE_PROTOCOL;
}

AlternateProtocol AlternateProtocolFromNextProto(NextProto next_proto) {
  switch (next_proto) {
    case kProtoHTTP2:
      return NPN_HTTP_2;
    case kProtoQUIC:
      return QUIC;

    case kProtoUnknown:
    case kProtoHTTP11:
      break;
  }

  NOTREACHED() << "Invalid NextProto: " << next_proto;
  return UNINITIALIZED_ALTERNATE_PROTOCOL;
}

std::string AlternativeService::ToString() const {
  return base::StringPrintf("%s %s:%d", AlternateProtocolToString(protocol),
                            host.c_str(), port);
}

std::string AlternativeServiceInfo::ToString() const {
  base::Time::Exploded exploded;
  expiration.LocalExplode(&exploded);
  return base::StringPrintf(
      "%s, expires %04d-%02d-%02d %02d:%02d:%02d",
      alternative_service.ToString().c_str(), exploded.year, exploded.month,
      exploded.day_of_month, exploded.hour, exploded.minute, exploded.second);
}

// static
void HttpServerProperties::ForceHTTP11(SSLConfig* ssl_config) {
  ssl_config->alpn_protos.clear();
  ssl_config->alpn_protos.push_back(kProtoHTTP11);
}

}  // namespace net

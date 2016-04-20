// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/bidirectional_stream.h"

#include <memory>

#include "base/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/thread_task_runner_handle.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/bidirectional_stream_request_info.h"
#include "net/http/http_network_session.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_stream.h"
#include "net/spdy/spdy_http_utils.h"
#include "net/ssl/ssl_cert_request_info.h"
#include "net/ssl/ssl_config.h"
#include "url/gurl.h"

namespace net {

BidirectionalStream::Delegate::Delegate() {}

BidirectionalStream::Delegate::~Delegate() {}

BidirectionalStream::BidirectionalStream(
    std::unique_ptr<BidirectionalStreamRequestInfo> request_info,
    HttpNetworkSession* session,
    Delegate* delegate)
    : BidirectionalStream(std::move(request_info),
                          session,
                          delegate,
                          base::WrapUnique(new base::Timer(false, false))) {}

BidirectionalStream::BidirectionalStream(
    std::unique_ptr<BidirectionalStreamRequestInfo> request_info,
    HttpNetworkSession* session,
    Delegate* delegate,
    std::unique_ptr<base::Timer> timer)
    : request_info_(std::move(request_info)),
      net_log_(BoundNetLog::Make(session->net_log(),
                                 NetLog::SOURCE_BIDIRECTIONAL_STREAM)),
      session_(session),
      delegate_(delegate),
      timer_(std::move(timer)),
      write_buffer_len_(0) {
  DCHECK(delegate_);
  DCHECK(request_info_);

  SSLConfig server_ssl_config;
  session->ssl_config_service()->GetSSLConfig(&server_ssl_config);
  session->GetAlpnProtos(&server_ssl_config.alpn_protos);
  session->GetNpnProtos(&server_ssl_config.npn_protos);

  if (!request_info_->url.SchemeIs(url::kHttpsScheme)) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(&BidirectionalStream::Delegate::OnFailed,
                   base::Unretained(delegate_), ERR_DISALLOWED_URL_SCHEME));
    return;
  }

  HttpRequestInfo http_request_info;
  http_request_info.url = request_info_->url;
  http_request_info.method = request_info_->method;
  http_request_info.extra_headers = request_info_->extra_headers;
  stream_request_.reset(
      session->http_stream_factory()->RequestBidirectionalStreamImpl(
          http_request_info, request_info_->priority, server_ssl_config,
          server_ssl_config, this, net_log_));
  // Check that this call cannot fail to set a non-NULL |stream_request_|.
  DCHECK(stream_request_);
  // Check that HttpStreamFactory does not invoke OnBidirectionalStreamImplReady
  // synchronously.
  DCHECK(!stream_impl_);
}

BidirectionalStream::~BidirectionalStream() {
  Cancel();
}

int BidirectionalStream::ReadData(IOBuffer* buf, int buf_len) {
  DCHECK(stream_impl_);

  int rv = stream_impl_->ReadData(buf, buf_len);
  if (rv > 0) {
    net_log_.AddByteTransferEvent(
        NetLog::TYPE_BIDIRECTIONAL_STREAM_BYTES_RECEIVED, rv, buf->data());
  } else if (rv == ERR_IO_PENDING) {
    read_buffer_ = buf;
    // Bytes will be logged in OnDataRead().
  }
  return rv;
}

void BidirectionalStream::SendData(IOBuffer* data,
                                   int length,
                                   bool end_stream) {
  DCHECK(stream_impl_);

  stream_impl_->SendData(data, length, end_stream);
  write_buffer_ = data;
  write_buffer_len_ = length;
}

void BidirectionalStream::Cancel() {
  stream_request_.reset();
  if (stream_impl_) {
    stream_impl_->Cancel();
    stream_impl_.reset();
  }
}

NextProto BidirectionalStream::GetProtocol() const {
  if (!stream_impl_)
    return kProtoUnknown;

  return stream_impl_->GetProtocol();
}

int64_t BidirectionalStream::GetTotalReceivedBytes() const {
  if (!stream_impl_)
    return 0;

  return stream_impl_->GetTotalReceivedBytes();
}

int64_t BidirectionalStream::GetTotalSentBytes() const {
  if (!stream_impl_)
    return 0;

  return stream_impl_->GetTotalSentBytes();
}

void BidirectionalStream::OnHeadersSent() {
  delegate_->OnHeadersSent();
}

void BidirectionalStream::OnHeadersReceived(
    const SpdyHeaderBlock& response_headers) {
  HttpResponseInfo response_info;
  if (!SpdyHeadersToHttpResponse(response_headers, HTTP2, &response_info)) {
    DLOG(WARNING) << "Invalid headers";
    delegate_->OnFailed(ERR_FAILED);
    return;
  }

  session_->http_stream_factory()->ProcessAlternativeServices(
      session_, response_info.headers.get(),
      HostPortPair::FromURL(request_info_->url));
  delegate_->OnHeadersReceived(response_headers);
}

void BidirectionalStream::OnDataRead(int bytes_read) {
  DCHECK(read_buffer_);

  net_log_.AddByteTransferEvent(
      NetLog::TYPE_BIDIRECTIONAL_STREAM_BYTES_RECEIVED, bytes_read,
      read_buffer_->data());
  read_buffer_ = nullptr;
  delegate_->OnDataRead(bytes_read);
}

void BidirectionalStream::OnDataSent() {
  DCHECK(write_buffer_);

  net_log_.AddByteTransferEvent(NetLog::TYPE_BIDIRECTIONAL_STREAM_BYTES_SENT,
                                write_buffer_len_, write_buffer_->data());
  write_buffer_ = nullptr;
  write_buffer_len_ = 0;
  delegate_->OnDataSent();
}

void BidirectionalStream::OnTrailersReceived(const SpdyHeaderBlock& trailers) {
  delegate_->OnTrailersReceived(trailers);
}

void BidirectionalStream::OnFailed(int status) {
  delegate_->OnFailed(status);
}

void BidirectionalStream::OnStreamReady(const SSLConfig& used_ssl_config,
                                        const ProxyInfo& used_proxy_info,
                                        HttpStream* stream) {
  NOTREACHED();
}

void BidirectionalStream::OnBidirectionalStreamImplReady(
    const SSLConfig& used_ssl_config,
    const ProxyInfo& used_proxy_info,
    BidirectionalStreamImpl* stream) {
  DCHECK(!stream_impl_);

  stream_request_.reset();
  stream_impl_.reset(stream);
  stream_impl_->Start(request_info_.get(), net_log_, this, std::move(timer_));
}

void BidirectionalStream::OnWebSocketHandshakeStreamReady(
    const SSLConfig& used_ssl_config,
    const ProxyInfo& used_proxy_info,
    WebSocketHandshakeStreamBase* stream) {
  NOTREACHED();
}

void BidirectionalStream::OnStreamFailed(int result,
                                         const SSLConfig& used_ssl_config,
                                         SSLFailureState ssl_failure_state) {
  DCHECK_LT(result, 0);
  DCHECK_NE(result, ERR_IO_PENDING);
  DCHECK(stream_request_);

  delegate_->OnFailed(result);
}

void BidirectionalStream::OnCertificateError(int result,
                                             const SSLConfig& used_ssl_config,
                                             const SSLInfo& ssl_info) {
  DCHECK_LT(result, 0);
  DCHECK_NE(result, ERR_IO_PENDING);
  DCHECK(stream_request_);

  delegate_->OnFailed(result);
}

void BidirectionalStream::OnNeedsProxyAuth(
    const HttpResponseInfo& proxy_response,
    const SSLConfig& used_ssl_config,
    const ProxyInfo& used_proxy_info,
    HttpAuthController* auth_controller) {
  DCHECK(stream_request_);

  delegate_->OnFailed(ERR_PROXY_AUTH_REQUESTED);
}

void BidirectionalStream::OnNeedsClientAuth(const SSLConfig& used_ssl_config,
                                            SSLCertRequestInfo* cert_info) {
  DCHECK(stream_request_);

  delegate_->OnFailed(ERR_SSL_CLIENT_AUTH_CERT_NEEDED);
}

void BidirectionalStream::OnHttpsProxyTunnelResponse(
    const HttpResponseInfo& response_info,
    const SSLConfig& used_ssl_config,
    const ProxyInfo& used_proxy_info,
    HttpStream* stream) {
  DCHECK(stream_request_);

  delegate_->OnFailed(ERR_HTTPS_PROXY_TUNNEL_RESPONSE);
}

void BidirectionalStream::OnQuicBroken() {}

}  // namespace net

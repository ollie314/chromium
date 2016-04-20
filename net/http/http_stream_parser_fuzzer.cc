// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/http_stream_parser.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_request_info.h"
#include "net/http/http_response_info.h"
#include "net/log/net_log.h"
#include "net/log/test_net_log.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/fuzzed_socket.h"
#include "url/gurl.h"

// Fuzzer for HttpStreamParser.
//
// |data| is used to create a FuzzedSocket.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Needed for thread checks and waits.
  base::MessageLoopForIO message_loop;

  net::TestCompletionCallback callback;
  net::BoundTestNetLog bound_test_net_log;
  std::unique_ptr<net::FuzzedSocket> fuzzed_socket(
      new net::FuzzedSocket(data, size, bound_test_net_log.bound()));
  CHECK_EQ(net::OK, fuzzed_socket->Connect(callback.callback()));

  net::ClientSocketHandle socket_handle;
  socket_handle.SetSocket(std::move(fuzzed_socket));

  net::HttpRequestInfo request_info;
  request_info.method = "GET";
  request_info.url = GURL("http://localhost/");

  scoped_refptr<net::GrowableIOBuffer> read_buffer(new net::GrowableIOBuffer());
  // Use a NetLog that listens to events, to get coverage of logging
  // callbacks.
  net::HttpStreamParser parser(&socket_handle, &request_info, read_buffer.get(),
                               bound_test_net_log.bound());

  net::HttpResponseInfo response_info;
  int result =
      parser.SendRequest("GET / HTTP/1.1\r\n", net::HttpRequestHeaders(),
                         &response_info, callback.callback());
  result = callback.GetResult(result);
  if (net::OK != result)
    return 0;

  result = parser.ReadResponseHeaders(callback.callback());
  result = callback.GetResult(result);

  while (result > 0) {
    scoped_refptr<net::IOBufferWithSize> io_buffer(
        new net::IOBufferWithSize(64));
    result = parser.ReadResponseBody(io_buffer.get(), io_buffer->size(),
                                     callback.callback());

    // Releasing the pointer to IOBuffer immediately is more likely to lead to a
    // use-after-free.
    io_buffer = nullptr;

    result = callback.GetResult(result);
  }

  return 0;
}

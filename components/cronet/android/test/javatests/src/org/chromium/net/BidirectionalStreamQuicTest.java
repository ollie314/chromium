// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.test.util.Feature;
import org.chromium.net.CronetTestBase.OnlyRunNativeCronet;
import org.json.JSONObject;

/**
 * Tests functionality of BidirectionalStream's QUIC implementation.
 */
public class BidirectionalStreamQuicTest extends CronetTestBase {
    private CronetTestFramework mTestFramework;
    private enum QuicBidirectionalStreams {
        ENABLED,
        DISABLED,
    }

    private void setUp(QuicBidirectionalStreams enabled) throws Exception {
        // Load library first to create MockCertVerifier.
        System.loadLibrary("cronet_tests");
        CronetEngine.Builder builder = new CronetEngine.Builder(getContext());

        QuicTestServer.startQuicTestServer(getContext());

        builder.enableQUIC(true);
        JSONObject quicParams = new JSONObject().put("host_whitelist", "test.example.com");
        if (enabled == QuicBidirectionalStreams.DISABLED) {
            quicParams.put("quic_disable_bidirectional_streams", true);
        }
        JSONObject experimentalOptions = new JSONObject().put("QUIC", quicParams);
        builder.setExperimentalOptions(experimentalOptions.toString());

        builder.addQuicHint(QuicTestServer.getServerHost(), QuicTestServer.getServerPort(),
                QuicTestServer.getServerPort());

        builder.setMockCertVerifierForTesting(QuicTestServer.createMockCertVerifier());

        mTestFramework = startCronetTestFrameworkWithUrlAndCronetEngineBuilder(null, builder);
        registerHostResolver(mTestFramework);
    }

    @Override
    protected void tearDown() throws Exception {
        QuicTestServer.shutdownQuicTestServer();
        super.tearDown();
    }

    @SmallTest
    @Feature({"Cronet"})
    @OnlyRunNativeCronet
    // Test that QUIC is negotiated.
    public void testSimpleGet() throws Exception {
        setUp(QuicBidirectionalStreams.ENABLED);
        String path = "/simple.txt";
        String quicURL = QuicTestServer.getServerURL() + path;
        TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
        BidirectionalStream stream = new BidirectionalStream
                                             .Builder(quicURL, callback, callback.getExecutor(),
                                                     mTestFramework.mCronetEngine)
                                             .setHttpMethod("GET")
                                             .build();
        stream.start();
        callback.blockForDone();
        assertTrue(stream.isDone());
        assertEquals(200, callback.mResponseInfo.getHttpStatusCode());
        assertEquals("This is a simple text file served by QUIC.\n", callback.mResponseAsString);
        assertEquals("quic/1+spdy/3", callback.mResponseInfo.getNegotiatedProtocol());
    }

    @SmallTest
    @Feature({"Cronet"})
    @OnlyRunNativeCronet
    public void testQuicBidirectionalStreamDisabled() throws Exception {
        setUp(QuicBidirectionalStreams.DISABLED);
        String path = "/simple.txt";
        String quicURL = QuicTestServer.getServerURL() + path;

        TestBidirectionalStreamCallback callback = new TestBidirectionalStreamCallback();
        BidirectionalStream stream = new BidirectionalStream
                                             .Builder(quicURL, callback, callback.getExecutor(),
                                                     mTestFramework.mCronetEngine)
                                             .setHttpMethod("GET")
                                             .build();
        stream.start();
        callback.blockForDone();
        assertTrue(stream.isDone());
        assertTrue(callback.mOnErrorCalled);
        assertNull(callback.mResponseInfo);
    }
}

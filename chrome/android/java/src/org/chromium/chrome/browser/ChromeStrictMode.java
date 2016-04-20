// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.app.ApplicationErrorReport;
import android.os.Build;
import android.os.Looper;
import android.os.MessageQueue;
import android.os.StrictMode;
import android.support.annotation.UiThread;

import org.chromium.base.BuildConfig;
import org.chromium.base.CommandLine;
import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.library_loader.LibraryLoader;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Initialize application-level StrictMode reporting.
 */
public class ChromeStrictMode {
    private static final String TAG = "ChromeStrictMode";
    private static final double UPLOAD_PROBABILITY = 0.01;
    private static final double MAX_UPLOADS_PER_SESSION = 3;

    private static boolean sIsStrictModeAlreadyConfigured = false;
    private static List<Object> sCachedStackTraces =
            Collections.synchronizedList(new ArrayList<Object>());
    private static AtomicInteger sNumUploads = new AtomicInteger();

    private static class SnoopingArrayList<T> extends ArrayList<T> {
        @Override
        public void clear() {
            for (int i = 0; i < size(); i++) {
                // It is likely that we have at most one violation pass this check each time around.
                if (Math.random() < UPLOAD_PROBABILITY) {
                    // Ensure that we do not upload too many StrictMode violations in any single
                    // session. To prevent races, we allow sNumUploads to increase beyond the
                    // limit, but just skip actually uploading the stack trace then.
                    if (sNumUploads.getAndAdd(1) >= MAX_UPLOADS_PER_SESSION) {
                        break;
                    }
                    sCachedStackTraces.add(get(i));
                }
            }
            super.clear();
        }
    }

    /**
     * Always process the violation on the UI thread. This ensures other crash reports are not
     * corrupted. Since each individual user has a very small chance of uploading each violation,
     * and we have a hard cap of 3 per session, this will not affect performance too much.
     *
     * @param violationInfo The violation info from the StrictMode violation in question.
     */
    @UiThread
    private static void reportStrictModeViolation(Object violationInfo) {
        try {
            Field crashInfoField = violationInfo.getClass().getField("crashInfo");
            ApplicationErrorReport.CrashInfo crashInfo =
                    (ApplicationErrorReport.CrashInfo) crashInfoField.get(violationInfo);
            String stackTrace = crashInfo.stackTrace;
            if (stackTrace == null) {
                Log.d(TAG, "StrictMode violation stack trace was null.");
            } else {
                Log.d(TAG, "Upload stack trace: " + stackTrace);
                JavaExceptionReporter.reportStackTrace(stackTrace);
            }
        } catch (Exception e) {
            // Ignore all exceptions.
            Log.d(TAG, "Could not handle observed StrictMode violation.", e);
        }
    }

    /**
     * Replace Android OS's StrictMode.violationsBeingTimed with a custom ArrayList acting as an
     * observer into violation stack traces. Set up an idle handler so StrictMode violations that
     * occur on startup are not ignored.
     */
    @SuppressWarnings({"unchecked", "rawtypes" })
    @UiThread
    private static void initializeStrictModeWatch() {
        try {
            Field violationsBeingTimedField =
                    StrictMode.class.getDeclaredField("violationsBeingTimed");
            violationsBeingTimedField.setAccessible(true);
            ThreadLocal<ArrayList> violationsBeingTimed =
                    (ThreadLocal<ArrayList>) violationsBeingTimedField.get(null);
            ArrayList replacementList = new SnoopingArrayList();
            violationsBeingTimed.set(replacementList);
        } catch (Exception e) {
            // Terminate watch if any exceptions are raised.
            Log.w(TAG, "Could not initialize StrictMode watch.", e);
            return;
        }
        sNumUploads.set(0);
        // Delay handling StrictMode violations during initialization until the main loop is idle.
        Looper.myQueue().addIdleHandler(new MessageQueue.IdleHandler() {
            @Override
            public boolean queueIdle() {
                // Will retry if the native library has not been initialized.
                if (!LibraryLoader.isInitialized()) return true;
                // Check again next time if no more cached stack traces to upload, and we have not
                // reached the max number of uploads for this session.
                if (sCachedStackTraces.isEmpty()) {
                    // TODO(wnwen): Add UMA count when this happens.
                    // In case of races, continue checking an extra time (equal condition).
                    return sNumUploads.get() <= MAX_UPLOADS_PER_SESSION;
                }
                // Since this is the only place we are removing elements, no need for additional
                // synchronization to ensure it is still non-empty.
                reportStrictModeViolation(sCachedStackTraces.remove(0));
                return true;
            }
        });
    }

    /**
     * Turn on StrictMode detection based on build and command-line switches.
     */
    @UiThread
    public static void configureStrictMode() {
        assert ThreadUtils.runningOnUiThread();
        if (sIsStrictModeAlreadyConfigured) {
            return;
        }
        sIsStrictModeAlreadyConfigured = true;
        CommandLine commandLine = CommandLine.getInstance();
        if ("eng".equals(Build.TYPE)
                || BuildConfig.sIsDebug
                || ChromeVersionInfo.isLocalBuild()
                || commandLine.hasSwitch(ChromeSwitches.STRICT_MODE)) {
            StrictMode.enableDefaults();
            StrictMode.ThreadPolicy.Builder threadPolicy =
                    new StrictMode.ThreadPolicy.Builder(StrictMode.getThreadPolicy());
            threadPolicy = threadPolicy.detectAll()
                    .penaltyFlashScreen()
                    .penaltyDeathOnNetwork();
            /*
             * Explicitly enable detection of all violations except file URI leaks, as that results
             * in false positives when file URI intents are passed between Chrome activities in
             * separate processes. See http://crbug.com/508282#c11.
             */
            StrictMode.VmPolicy.Builder vmPolicy = new StrictMode.VmPolicy.Builder();
            vmPolicy = vmPolicy.detectActivityLeaks()
                    .detectLeakedClosableObjects()
                    .detectLeakedRegistrationObjects()
                    .detectLeakedSqlLiteObjects()
                    .penaltyLog();
            if ("death".equals(commandLine.getSwitchValue(ChromeSwitches.STRICT_MODE))) {
                threadPolicy = threadPolicy.penaltyDeath();
                vmPolicy = vmPolicy.penaltyDeath();
            } else if ("testing".equals(commandLine.getSwitchValue(ChromeSwitches.STRICT_MODE))) {
                threadPolicy = threadPolicy.penaltyDeath();
                // Currently VmDeathPolicy kills the process, and is not visible on bot test output.
            }
            StrictMode.setThreadPolicy(threadPolicy.build());
            StrictMode.setVmPolicy(vmPolicy.build());
        }

        // Currently testing with local release builds only.
        // TODO(wnwen): Replace with finch experiment on dev.
        if (ChromeVersionInfo.isLocalBuild() && !BuildConfig.sIsDebug) {
            initializeStrictModeWatch();
        }
    }
}

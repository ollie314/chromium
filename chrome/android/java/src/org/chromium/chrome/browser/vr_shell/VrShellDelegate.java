// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.vr_shell;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.StrictMode;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewGroup.LayoutParams;
import android.view.WindowManager;
import android.widget.FrameLayout;

import org.chromium.base.Log;
import org.chromium.base.annotations.CalledByNative;
import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.ChromeTabbedActivity;
import org.chromium.chrome.browser.tab.EmptyTabObserver;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.TabObserver;
import org.chromium.content_public.common.BrowserControlsState;

import java.lang.reflect.Constructor;
import java.lang.reflect.InvocationTargetException;

/**
 * Manages interactions with the VR Shell.
 */
@JNINamespace("vr_shell")
public class VrShellDelegate {
    private static final String TAG = "VrShellDelegate";

    private final ChromeTabbedActivity mActivity;
    private final TabObserver mTabObserver;

    private boolean mVrAvailable;
    private Boolean mVrShellEnabled;

    private Class<? extends VrShell> mVrShellClass;
    private Class<? extends NonPresentingGvrContext> mNonPresentingGvrContextClass;
    private Class<? extends VrDaydreamApi> mVrDaydreamApiClass;
    private Class<? extends VrCoreVersionChecker> mVrCoreVersionCheckerClass;
    private VrShell mVrShell;
    private NonPresentingGvrContext mNonPresentingGvrContext;
    private VrDaydreamApi mVrDaydreamApi;
    private VrCoreVersionChecker mVrCoreVersionChecker;
    private boolean mInVr;
    private int mRestoreSystemUiVisibilityFlag = -1;
    private int mRestoreOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED;
    private long mNativeVrShellDelegate;
    private Tab mTab;

    // TODO(bshe): This should be replaced by string provided by NDK. Currently, it only available
    // in SDK and we don't want add dependency to SDK just to get this string. So hard code it here.
    private static final String DAYDREAM_VR_EXTRA = "android.intent.extra.VR_LAUNCH";
    private static final String DAYDREAM_DON_AUTO_TRANSITION =
            "org.chromium.chrome.browser.vr_shell.DAYDREAM_DON_AUTO_TRANSITION";

    public VrShellDelegate(ChromeTabbedActivity activity) {
        mActivity = activity;
        mVrAvailable = maybeFindVrClasses() && isVrCoreCompatible();
        createVrDaydreamApi();
        mTabObserver = new EmptyTabObserver() {
            @Override
            public void onContentChanged(Tab tab) {
                if (tab.getNativePage() != null || tab.isShowingSadTab()) {
                    // For now we don't handle native pages. crbug.com/661609
                    exitVRIfNecessary();
                }
            }

            @Override
            public void onWebContentsSwapped(Tab tab, boolean didStartLoad, boolean didFinishLoad) {
                // TODO(mthiesse): Update the native WebContents pointer and compositor. This
                // doesn't seem to get triggered in VR Shell currently, but that's likely to change
                // when we have omnibar navigation.
            }
        };
    }

    /**
     * Should be called once the native library is loaded so that the native portion of this
     * class can be initialized.
     */
    public void onNativeLibraryReady() {
        if (mVrAvailable) {
            mNativeVrShellDelegate = nativeInit();
        }
    }

    @SuppressWarnings("unchecked")
    private boolean maybeFindVrClasses() {
        try {
            mVrShellClass = (Class<? extends VrShell>) Class.forName(
                    "org.chromium.chrome.browser.vr_shell.VrShellImpl");
            mNonPresentingGvrContextClass =
                    (Class<? extends NonPresentingGvrContext>) Class.forName(
                            "org.chromium.chrome.browser.vr_shell.NonPresentingGvrContextImpl");
            mVrDaydreamApiClass = (Class<? extends VrDaydreamApi>) Class.forName(
                    "org.chromium.chrome.browser.vr_shell.VrDaydreamApiImpl");
            mVrCoreVersionCheckerClass = (Class<? extends VrCoreVersionChecker>) Class.forName(
                    "org.chromium.chrome.browser.vr_shell.VrCoreVersionCheckerImpl");
            return true;
        } catch (ClassNotFoundException e) {
            mVrShellClass = null;
            mNonPresentingGvrContextClass = null;
            mVrDaydreamApiClass = null;
            mVrCoreVersionCheckerClass = null;
            return false;
        }
    }

    /**
     * Enters VR Shell, displaying browser UI and tab contents in VR.
     *
     * This function performs native initialization, and so must only be called after native
     * libraries are ready.
     * @param inWebVR If true should begin displaying WebVR content rather than the VrShell UI.
     * @return Whether or not we are in VR when this function returns.
     */
    @CalledByNative
    public boolean enterVRIfNecessary(boolean inWebVR) {
        // If vr isn't in the build, or we haven't initialized yet, or vr shell is not enabled and
        // this is not a web vr request, then return immediately.
        if (!mVrAvailable || mNativeVrShellDelegate == 0 || (!isVrShellEnabled() && !inWebVR)) {
            return false;
        }
        Tab tab = mActivity.getActivityTab();
        // TODO(mthiesse): When we have VR UI for opening new tabs, etc., allow VR Shell to be
        // entered without any current tabs.
        if (tab == null || tab.getContentViewCore() == null) {
            return false;
        }

        // For now we don't handle native pages. crbug.com/661609
        if (tab.getNativePage() != null || tab.isShowingSadTab()) {
            return false;
        }

        if (mInVr) return true;

        mTab = tab;
        mTab.addObserver(mTabObserver);

        mRestoreOrientation = mActivity.getRequestedOrientation();
        // VrShell must be initialized in Landscape mode due to a bug in the GVR library.
        mActivity.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        if (!createVrShell()) {
            mActivity.setRequestedOrientation(mRestoreOrientation);
            return false;
        }
        addVrViews();
        setupVrModeWindowFlags();
        mVrShell.initializeNative(mTab, this);
        if (inWebVR) mVrShell.setWebVrModeEnabled(true);
        mVrShell.setVrModeEnabled(true);
        // onResume needs to be called on GvrLayout after initialization to make sure DON flow work
        // properly.
        mVrShell.resume();
        mInVr = true;
        mTab.updateFullscreenEnabledState();
        return true;
    }

    @CalledByNative
    private boolean exitWebVR() {
        if (!mInVr) return false;
        mVrShell.setWebVrModeEnabled(false);
        // TODO(bajones): Once VR Shell can be invoked outside of WebVR this
        // should no longer exit the shell outright. Need a way to determine
        // how VrShell was created.
        shutdownVR();
        return true;
    }

    /**
     * Resumes VR Shell.
     */
    public void maybeResumeVR() {
        if (isVrShellEnabled()) {
            registerDaydreamIntent();
        }

        // TODO(bshe): Ideally, we do not need two gvr context exist at the same time. We can
        // probably shutdown non presenting gvr when presenting and create a new one after exit
        // presenting. See crbug.com/655242
        if (mNonPresentingGvrContext != null) {
            StrictMode.ThreadPolicy oldPolicy = StrictMode.allowThreadDiskWrites();
            try {
                mNonPresentingGvrContext.resume();
            } catch (IllegalArgumentException e) {
                Log.e(TAG, "Unable to resume mNonPresentingGvrContext", e);
            } finally {
                StrictMode.setThreadPolicy(oldPolicy);
            }
        }

        if (mInVr) {
            setupVrModeWindowFlags();
            StrictMode.ThreadPolicy oldPolicy = StrictMode.allowThreadDiskWrites();
            try {
                mVrShell.resume();
            } catch (IllegalArgumentException e) {
                Log.e(TAG, "Unable to resume VrShell", e);
            } finally {
                StrictMode.setThreadPolicy(oldPolicy);
            }
        }
    }

    /**
     * Pauses VR Shell.
     */
    public void maybePauseVR() {
        if (isVrShellEnabled()) {
            unregisterDaydreamIntent();
        }

        if (mNonPresentingGvrContext != null) {
            mNonPresentingGvrContext.pause();
        }

        // TODO(mthiesse): When VR Shell lives in its own activity, and integrates with Daydream
        // home, pause instead of exiting VR here. For now, because VR Apps shouldn't show up in the
        // non-VR recents, and we don't want ChromeTabbedActivity disappearing, exit VR.
        exitVRIfNecessary();
    }

    /**
     * Exits the current VR mode (WebVR or VRShell)
     * @return Whether or not we exited VR.
     */
    public boolean exitVRIfNecessary() {
        if (!mInVr) return false;
        // If WebVR is presenting instruct it to exit.
        nativeExitWebVRIfNecessary(mNativeVrShellDelegate);
        shutdownVR();
        return true;
    }

    /**
     * Registers the Intent to fire after phone inserted into a headset.
     */
    private void registerDaydreamIntent() {
        if (mVrDaydreamApi == null) return;

        Intent intent = new Intent();
        // TODO(bshe): Ideally, this should go through ChromeLauncherActivity. To avoid polluting
        // metrics, use ChromeTabbedActivity directly for now.
        intent.setClass(mActivity, ChromeTabbedActivity.class);
        intent.putExtra(DAYDREAM_DON_AUTO_TRANSITION, true);
        PendingIntent pendingIntent =
                PendingIntent.getActivity(mActivity, 0, intent, PendingIntent.FLAG_ONE_SHOT);
        mVrDaydreamApi.registerDaydreamIntent(pendingIntent);
    }

    /**
     * Unregisters the Intent which registered by this context if any.
     */
    private void unregisterDaydreamIntent() {
        if (mVrDaydreamApi != null) {
            mVrDaydreamApi.unregisterDaydreamIntent();
        }
    }

    /**
     * Closes DaydreamApi.
     */
    public void close() {
        if (mVrDaydreamApi != null) {
            mVrDaydreamApi.close();
        }
    }

    @CalledByNative
    private long createNonPresentingNativeContext() {
        if (!mVrAvailable) return 0;

        try {
            Constructor<?> nonPresentingGvrContextConstructor =
                    mNonPresentingGvrContextClass.getConstructor(Activity.class);
            mNonPresentingGvrContext =
                    (NonPresentingGvrContext)
                            nonPresentingGvrContextConstructor.newInstance(mActivity);
        } catch (InstantiationException | IllegalAccessException | IllegalArgumentException
                | InvocationTargetException | NoSuchMethodException e) {
            Log.e(TAG, "Unable to instantiate NonPresentingGvrContext", e);
            return 0;
        }
        return mNonPresentingGvrContext.getNativeGvrContext();
    }

    @CalledByNative
    private void shutdownNonPresentingNativeContext() {
        mNonPresentingGvrContext.shutdown();
        mNonPresentingGvrContext = null;
    }

    /**
     * Exits VR Shell, performing all necessary cleanup.
     */
    private void shutdownVR() {
        if (!mInVr) return;
        mActivity.setRequestedOrientation(mRestoreOrientation);
        mVrShell.setVrModeEnabled(false);
        mVrShell.pause();
        removeVrViews();
        clearVrModeWindowFlags();
        destroyVrShell();
        mInVr = false;
        mTab.removeObserver(mTabObserver);
        mTab.updateFullscreenEnabledState();
        mTab.updateBrowserControlsState(BrowserControlsState.SHOWN, true);
    }

    private boolean isVrCoreCompatible() {
        if (mVrCoreVersionChecker != null) {
            return mVrCoreVersionChecker.isVrCoreCompatible();
        }

        if (mVrCoreVersionCheckerClass == null) {
            return false;
        }

        try {
            Constructor<?> mVrCoreVersionCheckerConstructor =
                    mVrCoreVersionCheckerClass.getConstructor();
            mVrCoreVersionChecker =
                    (VrCoreVersionChecker) mVrCoreVersionCheckerConstructor.newInstance();
        } catch (InstantiationException | IllegalAccessException | IllegalArgumentException
                | InvocationTargetException | NoSuchMethodException e) {
            Log.e(TAG, "Unable to instantiate VrCoreVersionChecker", e);
            return false;
        }
        return mVrCoreVersionChecker.isVrCoreCompatible();
    }

    private boolean createVrDaydreamApi() {
        if (!mVrAvailable) return false;

        try {
            Constructor<?> vrPrivateApiConstructor =
                    mVrDaydreamApiClass.getConstructor(Context.class);
            mVrDaydreamApi = (VrDaydreamApi) vrPrivateApiConstructor.newInstance(mActivity);
        } catch (InstantiationException | IllegalAccessException | IllegalArgumentException
                | InvocationTargetException | NoSuchMethodException e) {
            Log.e(TAG, "Unable to instantiate VrDaydreamApi", e);
            return false;
        }
        return true;
    }

    private boolean createVrShell() {
        StrictMode.ThreadPolicy oldPolicy = StrictMode.allowThreadDiskReads();
        StrictMode.allowThreadDiskWrites();
        try {
            Constructor<?> vrShellConstructor = mVrShellClass.getConstructor(Activity.class);
            mVrShell = (VrShell) vrShellConstructor.newInstance(mActivity);
        } catch (InstantiationException | IllegalAccessException | IllegalArgumentException
                | InvocationTargetException | NoSuchMethodException e) {
            Log.e(TAG, "Unable to instantiate VrShell", e);
            return false;
        } finally {
            StrictMode.setThreadPolicy(oldPolicy);
        }
        return true;
    }

    private void addVrViews() {
        FrameLayout decor = (FrameLayout) mActivity.getWindow().getDecorView();
        LayoutParams params = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT);
        decor.addView(mVrShell.getContainer(), params);
        mActivity.setUIVisibilityForVR(View.GONE);
    }

    private void removeVrViews() {
        mActivity.setUIVisibilityForVR(View.VISIBLE);
        FrameLayout decor = (FrameLayout) mActivity.getWindow().getDecorView();
        decor.removeView(mVrShell.getContainer());
    }

    private void setupVrModeWindowFlags() {
        if (mRestoreSystemUiVisibilityFlag == -1) {
            mRestoreSystemUiVisibilityFlag = mActivity.getWindow().getDecorView()
                    .getSystemUiVisibility();
        }
        mActivity.getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        mActivity.getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    private void clearVrModeWindowFlags() {
        mActivity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (mRestoreSystemUiVisibilityFlag != -1) {
            mActivity.getWindow().getDecorView()
                    .setSystemUiVisibility(mRestoreSystemUiVisibilityFlag);
        }
        mRestoreSystemUiVisibilityFlag = -1;
    }

    /**
     * Clean up VrShell, and associated native objects.
     */
    public void destroyVrShell() {
        if (mVrShell != null) {
            mVrShell.teardown();
            mVrShell = null;
        }
    }

    /**
     * Whether or not the intent is a Daydream VR Intent.
     */
    public boolean isVrIntent(Intent intent) {
        if (intent == null) return false;
        return intent.getBooleanExtra(DAYDREAM_DON_AUTO_TRANSITION, false)
                || intent.getBooleanExtra(DAYDREAM_VR_EXTRA, false);
    }

    /**
     * Whether or not we are currently in VR.
     */
    public boolean isInVR() {
        return mInVr;
    }

    /**
     * @return Whether or not VR Shell is currently enabled.
     */
    public boolean isVrShellEnabled() {
        if (mVrShellEnabled == null) {
            if (!LibraryLoader.isInitialized()) {
                return false;
            }
            mVrShellEnabled = ChromeFeatureList.isEnabled(ChromeFeatureList.VR_SHELL);
        }
        return mVrShellEnabled;
    }

    /**
     * @return Whether or not VR Shell is currently enabled.
     */
    public boolean isVrInitialized() {
        return mVrDaydreamApi != null;
    }

    /**
     * @return Pointer to the native VrShellDelegate object.
     */
    @CalledByNative
    private long getNativePointer() {
        return mNativeVrShellDelegate;
    }

    private native long nativeInit();
    private native void nativeExitWebVRIfNecessary(long nativeVrShellDelegate);
}

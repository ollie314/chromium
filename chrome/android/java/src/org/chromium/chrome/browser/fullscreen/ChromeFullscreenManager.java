// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.fullscreen;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.ObjectAnimator;
import android.app.Activity;
import android.content.res.Resources;
import android.os.Handler;
import android.os.Message;
import android.os.SystemClock;
import android.util.Property;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewGroup.LayoutParams;
import android.view.Window;
import android.widget.FrameLayout;

import org.chromium.base.ActivityState;
import org.chromium.base.ApplicationStatus;
import org.chromium.base.ApplicationStatus.ActivityStateListener;
import org.chromium.base.BaseChromiumApplication;
import org.chromium.base.BaseChromiumApplication.WindowFocusChangedListener;
import org.chromium.base.ThreadUtils;
import org.chromium.base.TraceEvent;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.library_loader.LibraryLoader;
import org.chromium.chrome.browser.fullscreen.FullscreenHtmlApiHandler.FullscreenHtmlApiDelegate;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tabmodel.TabModel.TabSelectionType;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tabmodel.TabModelSelectorTabModelObserver;
import org.chromium.chrome.browser.util.ViewUtils;
import org.chromium.chrome.browser.widget.ControlContainer;
import org.chromium.content.browser.ContentVideoView;
import org.chromium.content.browser.ContentViewCore;
import org.chromium.content_public.common.BrowserControlsState;

import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.HashSet;

/**
 * A class that manages control and content views to create the fullscreen mode.
 */
public class ChromeFullscreenManager
        extends FullscreenManager implements ActivityStateListener, WindowFocusChangedListener {
    // Minimum showtime of the toolbar (in ms).
    private static final long MINIMUM_SHOW_DURATION_MS = 3000;

    // Maximum length of the slide in/out animation of the toolbar (in ms).
    private static final long MAX_ANIMATION_DURATION_MS = 500;

    private static final int MSG_ID_HIDE_CONTROLS = 1;

    private final HashSet<Integer> mPersistentControlTokens = new HashSet<Integer>();

    private final Activity mActivity;
    private final Window mWindow;
    private final Handler mHandler;
    private final int mTopControlContainerHeight;
    private final int mBottomControlContainerHeight;

    private final TabModelSelector mTabModelSelector;
    private final TabModelSelectorTabModelObserver mTabModelObserver;

    private final ControlContainer mControlContainer;

    private long mMinShowNotificationMs = MINIMUM_SHOW_DURATION_MS;
    private long mMaxAnimationDurationMs = MAX_ANIMATION_DURATION_MS;

    // The shown ratio for the browser controls animated by this class as opposed to what is
    // computed by the renderer.
    private float mBrowserControlShownRatio = Float.NaN;

    private float mRendererTopControlOffset = Float.NaN;
    private float mRendererBottomControlOffset = Float.NaN;
    private float mRendererTopContentOffset;
    private float mPreviousContentOffset = Float.NaN;
    private float mControlOffsetRatio;
    private float mPreviousControlOffset;
    private boolean mIsEnteringPersistentModeState;

    private boolean mInGesture;
    private boolean mContentViewScrolling;

    private int mPersistentControlsCurrentToken;
    private long mCurrentShowTime;
    private int mActivityShowToken = INVALID_TOKEN;

    private ObjectAnimator mControlAnimation;
    private boolean mCurrentAnimationIsShowing;

    private boolean mDisableBrowserOverride;

    private boolean mBrowserControlsPermanentlyHidden;
    private boolean mBrowserControlsAndroidViewHidden;
    private final boolean mSupportsBrowserOverride;

    private boolean mIsBottomControls;

    private final ArrayList<FullscreenListener> mListeners = new ArrayList<FullscreenListener>();

    /**
     * A listener that gets notified of changes to the fullscreen state.
     */
    public interface FullscreenListener {
        /**
         * Called whenever the content's offset changes.
         * @param offset The new offset of the content from the top of the screen.
         */
        public void onContentOffsetChanged(float offset);

        /**
         * Called whenever the content's visible offset changes.
         * @param offset The new offset of the visible content from the top of the screen.
         * @param needsAnimate Whether the caller is driving an animation with further updates.
         */
        public void onVisibleContentOffsetChanged(float offset, boolean needsAnimate);

        /**
         * Called when a ContentVideoView is created/destroyed.
         * @param enabled Whether to enter or leave overlay video mode.
         */
        public void onToggleOverlayVideoMode(boolean enabled);
    }

    private class ControlsOffsetProperty extends Property<ChromeFullscreenManager, Float> {
        public ControlsOffsetProperty() {
            super(Float.class, "controlsOffsetRatio");
        }

        @Override
        public Float get(ChromeFullscreenManager object) {
            return getBrowserControlHiddenRatio();
        }

        @Override
        public void set(ChromeFullscreenManager manager, Float ratio) {
            if (mDisableBrowserOverride) return;
            float browserRatio = ratio.floatValue();
            if (Float.compare(mBrowserControlShownRatio, browserRatio) == 0) return;
            mBrowserControlShownRatio = browserRatio;
            manager.updateControlOffset();
            manager.updateVisuals();
        }
    }

    private final Runnable mUpdateVisibilityRunnable = new Runnable() {
        @Override
        public void run() {
            int visibility = shouldShowAndroidControls() ? View.VISIBLE : View.INVISIBLE;
            if (mControlContainer.getView().getVisibility() == visibility) return;
            // requestLayout is required to trigger a new gatherTransparentRegion(), which
            // only occurs together with a layout and let's SurfaceFlinger trim overlays.
            // This may be almost equivalent to using View.GONE, but we still use View.INVISIBLE
            // since drawing caches etc. won't be destroyed, and the layout may be less expensive.
            mControlContainer.getView().setVisibility(visibility);
            mControlContainer.getView().requestLayout();
        }
    };

    // This static inner class holds a WeakReference to the outer object, to avoid triggering the
    // lint HandlerLeak warning.
    private static class FullscreenHandler extends Handler {
        private final WeakReference<ChromeFullscreenManager> mChromeFullscreenManager;

        public FullscreenHandler(ChromeFullscreenManager chromeFullscreenManager) {
            mChromeFullscreenManager = new WeakReference<ChromeFullscreenManager>(
                    chromeFullscreenManager);
        }

        @Override
        public void handleMessage(Message msg) {
            if (msg == null) return;
            ChromeFullscreenManager chromeFullscreenManager = mChromeFullscreenManager.get();
            if (chromeFullscreenManager == null) return;
            switch (msg.what) {
                case MSG_ID_HIDE_CONTROLS:
                    chromeFullscreenManager.update(false);
                    break;
                default:
                    assert false : "Unexpected message for ID: " + msg.what;
                    break;
            }
        }
    }

    /**
     * Creates an instance of the fullscreen mode manager.
     * @param activity The activity that supports fullscreen.
     * @param controlContainer Container holding the controls (Toolbar).
     * @param modelSelector The tab model selector that will be monitored for tab changes.
     * @param resControlContainerHeight The dimension resource ID for the control container height.
     * @param supportsBrowserOverride Whether we want to disable the token system used by the
     *                                browser.
     * @param isBottomControls Whether or not the browser controls are at the bottom of the screen.
     */
    public ChromeFullscreenManager(Activity activity, ControlContainer controlContainer,
            TabModelSelector modelSelector, int resControlContainerHeight,
            boolean supportsBrowserOverride, boolean isBottomControls) {
        super(activity.getWindow());

        mActivity = activity;
        ApplicationStatus.registerStateListenerForActivity(this, activity);
        ((BaseChromiumApplication) activity.getApplication())
                .registerWindowFocusChangedListener(this);

        mTabModelSelector = modelSelector;
        mTabModelObserver = new TabModelSelectorTabModelObserver(mTabModelSelector) {
            @Override
            public void tabClosureCommitted(Tab tab) {
                setTab(mTabModelSelector.getCurrentTab());
            }

            @Override
            public void allTabsClosureCommitted() {
                setTab(mTabModelSelector.getCurrentTab());
            }

            @Override
            public void tabRemoved(Tab tab) {
                setTab(mTabModelSelector.getCurrentTab());
            }

            @Override
            public void didSelectTab(Tab tab, TabSelectionType type, int lastId) {
                setTab(mTabModelSelector.getCurrentTab());
            }
        };

        mWindow = activity.getWindow();
        mHandler = new FullscreenHandler(this);
        assert controlContainer != null;
        mControlContainer = controlContainer;
        Resources resources = mWindow.getContext().getResources();
        mIsBottomControls = isBottomControls;

        int controlContainerHeight = resources.getDimensionPixelSize(resControlContainerHeight);
        if (mIsBottomControls) {
            mTopControlContainerHeight = 0;
            mBottomControlContainerHeight = controlContainerHeight;
        } else {
            mTopControlContainerHeight = controlContainerHeight;
            mBottomControlContainerHeight = 0;
        }

        mRendererTopContentOffset = mTopControlContainerHeight;
        mSupportsBrowserOverride = supportsBrowserOverride;
        updateControlOffset();
    }

    /**
     * @return Whether or not the browser controls are attached to the bottom of the screen.
     */
    public boolean areBrowserControlsAtBottom() {
        return mIsBottomControls;
    }

    @Override
    public void onActivityStateChange(Activity activity, int newState) {
        if (newState == ActivityState.STOPPED) {
            // Exit fullscreen in onStop to ensure the system UI flags are set correctly when
            // showing again (on JB MR2+ builds, the omnibox would be covered by the
            // notification bar when this was done in onStart()).
            setPersistentFullscreenMode(false);
        } else if (newState == ActivityState.STARTED) {
            // Force the controls to be shown until we get an update from a Tab.  This is a
            // workaround for when the renderer is killed but the Tab is not notified.
            mActivityShowToken = showControlsPersistentAndClearOldToken(mActivityShowToken);
        } else if (newState == ActivityState.DESTROYED) {
            ApplicationStatus.unregisterActivityStateListener(this);
            ((BaseChromiumApplication) mWindow.getContext().getApplicationContext())
                    .unregisterWindowFocusChangedListener(this);

            mTabModelObserver.destroy();
        }
    }

    @Override
    public void onWindowFocusChanged(Activity activity, boolean hasFocus) {
        if (mActivity != activity) return;
        onWindowFocusChanged(hasFocus);
        // {@link ContentVideoView#getContentVideoView} requires native to have been initialized.
        if (!LibraryLoader.isInitialized()) return;
        ContentVideoView videoView = ContentVideoView.getContentVideoView();
        if (videoView != null) {
            videoView.onFullscreenWindowFocused();
        }
    }

    @Override
    protected FullscreenHtmlApiDelegate createApiDelegate() {
        return new FullscreenHtmlApiDelegate() {
            @Override
            public void onEnterFullscreen() {
                Tab tab = getTab();
                if (areBrowserControlsOffScreen()) {
                    // The browser controls are currently hidden.
                    getHtmlApiHandler().enterFullscreen(tab);
                } else {
                    // We should hide browser controls first.
                    mIsEnteringPersistentModeState = true;
                    tab.updateFullscreenEnabledState();
                }
            }

            @Override
            public boolean cancelPendingEnterFullscreen() {
                boolean wasPending = mIsEnteringPersistentModeState;
                mIsEnteringPersistentModeState = false;
                return wasPending;
            }

            @Override
            public void onFullscreenExited(Tab tab) {
                // At this point, browser controls are hidden. Show browser controls only if it's
                // permitted.
                tab.updateBrowserControlsState(BrowserControlsState.SHOWN, true);
            }

            @Override
            public boolean shouldShowNotificationToast() {
                return !isOverlayVideoMode();
            }
        };
    }

    /**
     * Disables the ability for the browser to override the renderer provided browser controls
     * position for testing.
     */
    @VisibleForTesting
    public void disableBrowserOverrideForTest() {
        ThreadUtils.assertOnUiThread();

        mDisableBrowserOverride = true;
        mPersistentControlTokens.clear();
        mHandler.removeMessages(MSG_ID_HIDE_CONTROLS);
        if (mControlAnimation != null) {
            mControlAnimation.cancel();
            mControlAnimation = null;
        }
        mBrowserControlShownRatio = Float.NaN;
        updateVisuals();
    }

    /**
     * Allows tests to override the animation durations for faster tests.
     * @param minShowDuration The minimum time the controls must be shown.
     * @param maxAnimationDuration The maximum animation time to show/hide the controls.
     */
    @VisibleForTesting
    public void setAnimationDurationsForTest(long minShowDuration, long maxAnimationDuration) {
        mMinShowNotificationMs = minShowDuration;
        mMaxAnimationDurationMs = maxAnimationDuration;
    }

    @Override
    public void showControlsTransient() {
        if (!mSupportsBrowserOverride) return;
        if (mPersistentControlTokens.isEmpty()) update(true);
    }

    @Override
    public int showControlsPersistent() {
        if (!mSupportsBrowserOverride) return INVALID_TOKEN;
        int token = mPersistentControlsCurrentToken++;
        mPersistentControlTokens.add(token);
        if (mPersistentControlTokens.size() == 1) update(true);
        return token;
    }

    @Override
    public int showControlsPersistentAndClearOldToken(int oldToken) {
        if (!mSupportsBrowserOverride) return INVALID_TOKEN;
        if (oldToken != INVALID_TOKEN) mPersistentControlTokens.remove(oldToken);
        return showControlsPersistent();
    }

    @Override
    public void hideControlsPersistent(int token) {
        if (!mSupportsBrowserOverride) return;
        if (mPersistentControlTokens.remove(token) && mPersistentControlTokens.isEmpty()) {
            update(false);
        }
    }

    /**
     * @return The ratio that the browser controls are off screen; this will be a number [0,1]
     *         where 1 is completely hidden and 0 is completely shown.
     */
    private float getBrowserControlHiddenRatio() {
        return mControlOffsetRatio;
    }

    /**
     * @return True if the browser controls are completely off screen.
     */
    public boolean areBrowserControlsOffScreen() {
        return getBrowserControlHiddenRatio() == 1.0f;
    }

    /**
     * @param remove Whether or not to forcefully remove the toolbar.
     */
    public void setBrowserControlsPermamentlyHidden(boolean remove) {
        if (remove == mBrowserControlsPermanentlyHidden) return;
        mBrowserControlsPermanentlyHidden = remove;
        updateVisuals();
    }

    /**
     * @return Whether or not the toolbar is forcefully being removed.
     */
    public boolean areBrowserControlsPermanentlyHidden() {
        return mBrowserControlsPermanentlyHidden;
    }

    /**
     * @return Whether the browser controls should be drawn as a texture.
     */
    public boolean drawControlsAsTexture() {
        return getBrowserControlHiddenRatio() > 0;
    }

    @Override
    public int getTopControlsHeight() {
        return mTopControlContainerHeight;
    }

    /**
     * @return The height of the bottom controls in pixels.
     */
    public int getBottomControlsHeight() {
        return mBottomControlContainerHeight;
    }

    @Override
    public float getContentOffset() {
        if (mBrowserControlsPermanentlyHidden) return 0;
        return mRendererTopContentOffset;
    }

    /**
     * @return The offset of the controls from the top of the screen.
     */
    public float getTopControlOffset() {
        if (mBrowserControlsPermanentlyHidden) return -getTopControlsHeight();
        // This is to avoid a problem with -0f in tests.
        if (mControlOffsetRatio == 0f) return 0f;
        return mControlOffsetRatio * -getTopControlsHeight();
    }

    /**
     * @return The offset of the controls from the bottom of the screen.
     */
    public float getBottomControlOffset() {
        if (mBrowserControlsPermanentlyHidden) return getBottomControlsHeight();
        if (mControlOffsetRatio == 0f) return 0f;
        return mControlOffsetRatio * getBottomControlsHeight();

    }

    /**
     * @return The toolbar control container.
     */
    public ControlContainer getControlContainer() {
        return mControlContainer;
    }

    private void updateControlOffset() {
        float topOffsetRatio = 0;

        final boolean isNaNBrowserControlOffset = Float.isNaN(mBrowserControlShownRatio);
        float rendererControlOffset;
        if (mIsBottomControls) {
            rendererControlOffset =
                    Math.abs(mRendererBottomControlOffset / getBottomControlsHeight());
        } else {
            rendererControlOffset = Math.abs(mRendererTopControlOffset / getTopControlsHeight());
        }

        final boolean isNaNRendererControlOffset = Float.isNaN(rendererControlOffset);
        if (!isNaNBrowserControlOffset || !isNaNRendererControlOffset) {
            topOffsetRatio = Math.min(
                    isNaNBrowserControlOffset ? 1 : mBrowserControlShownRatio,
                    isNaNRendererControlOffset ? 1 : rendererControlOffset);
        }
        mControlOffsetRatio = topOffsetRatio;
    }

    @Override
    public void setOverlayVideoMode(boolean enabled) {
        super.setOverlayVideoMode(enabled);

        for (int i = 0; i < mListeners.size(); i++) {
            mListeners.get(i).onToggleOverlayVideoMode(enabled);
        }
    }

    /**
     * @return Whether the browser has a control offset override.
     */
    @VisibleForTesting
    public boolean hasBrowserControlOffsetOverride() {
        return !Float.isNaN(mBrowserControlShownRatio) || mControlAnimation != null
                || !mPersistentControlTokens.isEmpty();
    }

    /**
     * @return The visible offset of the content from the top of the screen.
     */
    public float getTopVisibleContentOffset() {
        return getTopControlsHeight() + getTopControlOffset();
    }

    /**
     * @param listener The {@link FullscreenListener} to be notified of fullscreen changes.
     */
    public void addListener(FullscreenListener listener) {
        if (!mListeners.contains(listener)) mListeners.add(listener);
    }

    /**
     * @param listener The {@link FullscreenListener} to no longer be notified of fullscreen
     *                 changes.
     */
    public void removeListener(FullscreenListener listener) {
        mListeners.remove(listener);
    }

    /**
     * Updates the content view's viewport size to have it render the content correctly.
     *
     * @param viewCore The ContentViewCore to update.
     */
    public void updateContentViewViewportSize(ContentViewCore viewCore) {
        if (viewCore == null) return;
        if (mInGesture || mContentViewScrolling) return;

        // Update content viewport size only when the browser controls are not animating.
        int contentOffset = (int) mRendererTopContentOffset;
        if (contentOffset != 0 && contentOffset != getTopControlsHeight()) return;
        viewCore.setTopControlsHeight(getTopControlsHeight(), contentOffset > 0);
    }

    @Override
    public void updateContentViewChildrenState() {
        ContentViewCore contentViewCore = getActiveContentViewCore();
        if (contentViewCore == null) return;
        ViewGroup view = contentViewCore.getContainerView();

        float topViewsTranslation = getTopVisibleContentOffset();
        applyTranslationToTopChildViews(view, topViewsTranslation);
        applyMarginToFullChildViews(view, topViewsTranslation);
        updateContentViewViewportSize(contentViewCore);
    }

    /**
     * Utility routine for ensuring visibility updates are synchronized with
     * animation, preventing message loop stalls due to untimely invalidation.
     */
    private void scheduleVisibilityUpdate() {
        final int desiredVisibility = shouldShowAndroidControls() ? View.VISIBLE : View.INVISIBLE;
        if (mControlContainer.getView().getVisibility() == desiredVisibility) return;
        mControlContainer.getView().removeCallbacks(mUpdateVisibilityRunnable);
        mControlContainer.getView().postOnAnimation(mUpdateVisibilityRunnable);
    }

    private void updateVisuals() {
        TraceEvent.begin("FullscreenManager:updateVisuals");

        // Use bottom controls height if top controls have no height.
        float offset = getTopControlOffset();
        if (mIsBottomControls) offset = getBottomControlOffset();

        if (Float.compare(mPreviousControlOffset, offset) != 0) {
            mPreviousControlOffset = offset;

            scheduleVisibilityUpdate();
            if (shouldShowAndroidControls()) {
                mControlContainer.getView().setTranslationY(offset);
            }

            // Whether we need the compositor to draw again to update our animation.
            // Should be |false| when the browser controls are only moved through the page
            // scrolling.
            boolean needsAnimate = mControlAnimation != null || shouldShowAndroidControls();
            for (int i = 0; i < mListeners.size(); i++) {
                // Since, in the case of bottom controls, the view is never translated, we don't
                // need to change the information passed into this method.
                // getTopVisibleContentOffset will return 0 which is the expected result.
                mListeners.get(i).onVisibleContentOffsetChanged(
                        getTopVisibleContentOffset(), needsAnimate);
            }
        }

        final Tab tab = getTab();
        if (tab != null && areBrowserControlsOffScreen() && mIsEnteringPersistentModeState) {
            getHtmlApiHandler().enterFullscreen(tab);
            mIsEnteringPersistentModeState = false;
        }

        updateContentViewChildrenState();

        float contentOffset = getContentOffset();
        if (Float.compare(mPreviousContentOffset, contentOffset) != 0) {
            for (int i = 0; i < mListeners.size(); i++) {
                mListeners.get(i).onContentOffsetChanged(contentOffset);
            }
            mPreviousContentOffset = contentOffset;
        }

        TraceEvent.end("FullscreenManager:updateVisuals");
    }

    /**
     * @param hide Whether or not to force the browser controls Android view to hide.  If this is
     *             {@code false} the browser controls Android view will show/hide based on position,
     *             if it is {@code true} the browser controls Android view will always be hidden.
     */
    public void setHideBrowserControlsAndroidView(boolean hide) {
        if (mBrowserControlsAndroidViewHidden == hide) return;
        mBrowserControlsAndroidViewHidden = hide;
        scheduleVisibilityUpdate();
    }

    private boolean shouldShowAndroidControls() {
        if (mBrowserControlsAndroidViewHidden) return false;

        boolean showControls = !drawControlsAsTexture();
        ContentViewCore contentViewCore = getActiveContentViewCore();
        if (contentViewCore == null) return showControls;
        ViewGroup contentView = contentViewCore.getContainerView();

        for (int i = 0; i < contentView.getChildCount(); i++) {
            View child = contentView.getChildAt(i);
            if (!(child.getLayoutParams() instanceof FrameLayout.LayoutParams)) continue;

            FrameLayout.LayoutParams layoutParams =
                    (FrameLayout.LayoutParams) child.getLayoutParams();
            if (Gravity.TOP == (layoutParams.gravity & Gravity.FILL_VERTICAL)) {
                showControls = true;
                break;
            }
        }

        showControls |= !mPersistentControlTokens.isEmpty();

        return showControls;
    }

    private void applyMarginToFullChildViews(ViewGroup contentView, float margin) {
        for (int i = 0; i < contentView.getChildCount(); i++) {
            View child = contentView.getChildAt(i);
            if (!(child.getLayoutParams() instanceof FrameLayout.LayoutParams)) continue;
            FrameLayout.LayoutParams layoutParams =
                    (FrameLayout.LayoutParams) child.getLayoutParams();

            if (layoutParams.height == LayoutParams.MATCH_PARENT
                    && layoutParams.topMargin != (int) margin) {
                layoutParams.topMargin = (int) margin;
                child.requestLayout();
                TraceEvent.instant("FullscreenManager:child.requestLayout()");
            }
        }
    }

    private void applyTranslationToTopChildViews(ViewGroup contentView, float translation) {
        for (int i = 0; i < contentView.getChildCount(); i++) {
            View child = contentView.getChildAt(i);
            if (!(child.getLayoutParams() instanceof FrameLayout.LayoutParams)) continue;

            FrameLayout.LayoutParams layoutParams =
                    (FrameLayout.LayoutParams) child.getLayoutParams();
            if (Gravity.TOP == (layoutParams.gravity & Gravity.FILL_VERTICAL)) {
                child.setTranslationY(translation);
                TraceEvent.instant("FullscreenManager:child.setTranslationY()");
            }
        }
    }

    private ContentViewCore getActiveContentViewCore() {
        Tab tab = getTab();
        return tab != null ? tab.getContentViewCore() : null;
    }

    @Override
    public void setPositionsForTabToNonFullscreen() {
        Tab tab = getTab();
        if (tab == null || tab.isShowingBrowserControlsEnabled()) {
            setPositionsForTab(0, 0, getTopControlsHeight());
        } else {
            setPositionsForTab(-getTopControlsHeight(), getBottomControlsHeight(), 0);
        }
    }

    @Override
    public void setPositionsForTab(float topControlsOffset, float bottomControlsOffset,
            float topContentOffset) {
        // Once we get an update from a tab, clear the activity show token and allow the render
        // to control the positions of the browser controls.
        if (mActivityShowToken != INVALID_TOKEN) {
            hideControlsPersistent(mActivityShowToken);
            mActivityShowToken = INVALID_TOKEN;
        }

        float rendererTopControlOffset =
                Math.round(Math.max(topControlsOffset, -getTopControlsHeight()));
        float rendererBottomControlOffset =
                Math.round(Math.min(bottomControlsOffset, getBottomControlsHeight()));

        float rendererTopContentOffset = Math.min(
                Math.round(topContentOffset), rendererTopControlOffset + getTopControlsHeight());

        if (Float.compare(rendererTopControlOffset, mRendererTopControlOffset) == 0
                && Float.compare(rendererBottomControlOffset, mRendererBottomControlOffset) == 0
                && Float.compare(rendererTopContentOffset, mRendererTopContentOffset) == 0) {
            return;
        }

        mRendererTopControlOffset = rendererTopControlOffset;
        mRendererBottomControlOffset = rendererBottomControlOffset;

        mRendererTopContentOffset = rendererTopContentOffset;
        updateControlOffset();

        if (mControlAnimation == null) updateVisuals();
    }

    /**
     * @param e The dispatched motion event
     * @return Whether or not this motion event is in the top control container area and should be
     *         consumed.
     */
    public boolean onInterceptMotionEvent(MotionEvent e) {
        int bottomPosition;
        int topPosition = 0;
        float offset;

        if (mIsBottomControls) {
            int[] position = new int[2];
            ViewUtils.getRelativeLayoutPosition(mControlContainer.getView().getRootView(),
                    mControlContainer.getView(), position);

            topPosition = position[1];
            bottomPosition = topPosition + getBottomControlsHeight();
            offset = getBottomControlOffset();
        } else {
            bottomPosition = getTopControlsHeight();
            offset = getTopControlOffset();
        }

        return e.getY() < topPosition + offset && e.getY() > bottomPosition + offset
                && !mBrowserControlsAndroidViewHidden;
    }

    /**
     * Notifies the fullscreen manager that a motion event has occurred.
     * @param e The dispatched motion event.
     */
    public void onMotionEvent(MotionEvent e) {
        int eventAction = e.getActionMasked();
        if (eventAction == MotionEvent.ACTION_DOWN
                || eventAction == MotionEvent.ACTION_POINTER_DOWN) {
            mInGesture = true;
            // TODO(qinmin): Probably there is no need to hide the toast as it will go away
            // by itself.
            getHtmlApiHandler().hideNotificationToast();
        } else if (eventAction == MotionEvent.ACTION_CANCEL
                || eventAction == MotionEvent.ACTION_UP) {
            mInGesture = false;
            updateVisuals();
        }
    }

    private void update(boolean show) {
        // On forced show/hide, reset the flags that may suppress ContentView resize.
        // As this method is also called when tab is switched, this also cleanup the scrolling
        // flag set based on the previous ContentView's scrolling state.
        mInGesture = false;
        mContentViewScrolling = false;

        if (show) mCurrentShowTime = SystemClock.uptimeMillis();

        boolean postHideMessage = false;
        if (!show) {
            if (mControlAnimation != null && mCurrentAnimationIsShowing) {
                postHideMessage = true;
            } else {
                long timeDelta = SystemClock.uptimeMillis() - mCurrentShowTime;
                animateIfNecessary(false, Math.max(mMinShowNotificationMs - timeDelta, 0));
            }
        } else {
            animateIfNecessary(true, 0);
            if (mPersistentControlTokens.isEmpty()) postHideMessage = true;
        }

        mHandler.removeMessages(MSG_ID_HIDE_CONTROLS);
        if (postHideMessage) {
            long timeDelta = SystemClock.uptimeMillis() - mCurrentShowTime;
            mHandler.sendEmptyMessageDelayed(
                    MSG_ID_HIDE_CONTROLS, Math.max(mMinShowNotificationMs - timeDelta, 0));
        }
    }

    private void animateIfNecessary(final boolean show, long startDelay) {
        if (mControlAnimation != null) {
            if (!mControlAnimation.isRunning() || mCurrentAnimationIsShowing != show) {
                mControlAnimation.cancel();
                mControlAnimation = null;
            } else {
                return;
            }
        }

        float destination = show ? 0 : 1;
        long duration = (long) (mMaxAnimationDurationMs
                * Math.abs(destination - getBrowserControlHiddenRatio()));
        mControlAnimation = ObjectAnimator.ofFloat(this, new ControlsOffsetProperty(), destination);
        mControlAnimation.addListener(new AnimatorListenerAdapter() {
            private boolean mCanceled = false;

            @Override
            public void onAnimationCancel(Animator anim) {
                mCanceled = true;
            }

            @Override
            public void onAnimationEnd(Animator animation) {
                if (!show && !mCanceled) mBrowserControlShownRatio = Float.NaN;
                mControlAnimation = null;
            }
        });
        mControlAnimation.setStartDelay(startDelay);
        mControlAnimation.setDuration(duration);
        mControlAnimation.start();
        mCurrentAnimationIsShowing = show;
    }

    @Override
    public void onContentViewScrollingStateChanged(boolean scrolling) {
        mContentViewScrolling = scrolling;
        if (!scrolling) updateVisuals();
    }
}

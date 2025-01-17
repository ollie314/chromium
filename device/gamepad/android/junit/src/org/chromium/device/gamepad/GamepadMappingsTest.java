// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.device.gamepad;

import android.view.KeyEvent;
import android.view.MotionEvent;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.robolectric.annotation.Config;

import org.chromium.base.test.util.Feature;
import org.chromium.testing.local.LocalRobolectricTestRunner;

import java.util.Arrays;
import java.util.BitSet;

/**
 * Verify no regressions in gamepad mappings.
 */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class GamepadMappingsTest {
    private static final float ERROR_TOLERANCE = 0.000001f;
    /**
     * Set bits indicate that we don't expect the button at mMappedButtons[index] to be mapped.
     */
    private BitSet mUnmappedButtons = new BitSet(CanonicalButtonIndex.COUNT);
    /**
     * Set bits indicate that we don't expect the axis at mMappedAxes[index] to be mapped.
     */
    private BitSet mUnmappedAxes = new BitSet(CanonicalAxisIndex.COUNT);
    private float[] mMappedButtons = new float[CanonicalButtonIndex.COUNT];
    private float[] mMappedAxes = new float[CanonicalAxisIndex.COUNT];
    private float[] mRawButtons = new float[GamepadDevice.MAX_RAW_BUTTON_VALUES];
    private float[] mRawAxes = new float[GamepadDevice.MAX_RAW_AXIS_VALUES];

    @Before
    public void setUp() throws Exception {
        // By default, we expect every button and axis to be mapped.
        mUnmappedButtons.clear();
        mUnmappedAxes.clear();

        // Start with all the mapped values as unmapped.
        Arrays.fill(mMappedButtons, Float.NaN);
        Arrays.fill(mMappedAxes, Float.NaN);

        // Set each raw value to something unique.
        for (int i = 0; i < GamepadDevice.MAX_RAW_AXIS_VALUES; i++) {
            mRawAxes[i] = -i - 1.0f;
        }
        for (int i = 0; i < GamepadDevice.MAX_RAW_BUTTON_VALUES; i++) {
            mRawButtons[i] = i + 1.0f;
        }
    }

    @Test
    @Feature({"Gamepad"})
    public void testShieldGamepadMappings() throws Exception {
        GamepadMappings mappings = GamepadMappings.getMappings(
                GamepadMappings.NVIDIA_SHIELD_DEVICE_NAME_PREFIX, null);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertShieldGamepadMappings();
    }

    @Test
    @Feature({"Gamepad"})
    public void testXBox360GamepadMappings() throws Exception {
        GamepadMappings mappings = GamepadMappings.getMappings(
                GamepadMappings.MICROSOFT_XBOX_PAD_DEVICE_NAME, null);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertShieldGamepadMappings();
    }

    @Test
    @Feature({"Gamepad"})
    public void testPS3SixAxisGamepadMappings() throws Exception {
        GamepadMappings mappings = GamepadMappings.getMappings(
                GamepadMappings.PS3_SIXAXIS_DEVICE_NAME, null);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.PRIMARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_X], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.SECONDARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_Y], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.TERTIARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_A], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.QUATERNARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_B], ERROR_TOLERANCE);

        assertMappedTriggerButtonsToTopShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonDpadButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedTriggerAxesToBottomShoulder();
        assertMappedXYAxes();
        assertMappedZAndRZAxesToRightStick();

        assertMapping();
    }

    @Test
    @Feature({"Gamepad"})
    public void testSamsungEIGP20GamepadMappings() throws Exception {
        GamepadMappings mappings = GamepadMappings.getMappings(
                GamepadMappings.SAMSUNG_EI_GP20_DEVICE_NAME, null);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertMappedCommonXYABButtons();
        assertMappedUpperTriggerButtonsToBottomShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedHatAxisToDpadButtons();
        assertMappedXYAxes();
        assertMappedRXAndRYAxesToRightStick();

        expectNoShoulderButtons();
        assertMapping();
    }

    @Test
    @Feature({"Gamepad"})
    public void testAmazonFireGamepadMappings() throws Exception {
        GamepadMappings mappings = GamepadMappings.getMappings(
                GamepadMappings.AMAZON_FIRE_DEVICE_NAME, null);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertMappedCommonXYABButtons();
        assertMappedPedalAxesToBottomShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedTriggerButtonsToTopShoulder();
        assertMappedHatAxisToDpadButtons();
        assertMappedXYAxes();
        assertMappedZAndRZAxesToRightStick();

        assertMapping();
    }

    @Test
    @Feature({"Gamepad"})
    public void testXboxWirelessGamepadTriggerIdle() throws Exception {
        GamepadMappings mappings = GamepadMappings.getMappings(
                GamepadMappings.MICROSOFT_XBOX_WIRELESS_DEVICE_NAME, null);

        // Both triggers are inactive, because they haven't seen input yet.
        mRawAxes[MotionEvent.AXIS_Z] = 0.f;
        mRawAxes[MotionEvent.AXIS_RZ] = 0.f;
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                0.f, ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                0.f, ERROR_TOLERANCE);

        // This "activates" the left trigger, so it will begin reporting input.
        // It will still be 0 because the triggers idle at -1.
        mRawAxes[MotionEvent.AXIS_Z] = -1.f;
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                0.f, ERROR_TOLERANCE);

        // Since the left trigger is active, 0 should be reported as 0.5.
        mRawAxes[MotionEvent.AXIS_Z] = 0.f;
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                0.5f, ERROR_TOLERANCE);

        // The right trigger should still read 0 because it's not active yet.
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                0.f, ERROR_TOLERANCE);

        // Activate the right trigger
        mRawAxes[MotionEvent.AXIS_RZ] = -1.f;
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                0.f, ERROR_TOLERANCE);

        // Now the right trigger should also report 0 as 0.5.
        mRawAxes[MotionEvent.AXIS_RZ] = 0.f;
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                0.5f, ERROR_TOLERANCE);
    }

    @Test
    @Feature({"Gamepad"})
    public void testXboxWirelessGamepadMappings() throws Exception {
        GamepadMappings mappings = GamepadMappings.getMappings(
                GamepadMappings.MICROSOFT_XBOX_WIRELESS_DEVICE_NAME, null);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.PRIMARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_A], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.SECONDARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_B], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.TERTIARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_C], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.QUATERNARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_X], ERROR_TOLERANCE);

        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_SHOULDER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_Y], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_SHOULDER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_Z], ERROR_TOLERANCE);

        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.BACK_SELECT],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_L1], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.START],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_R1], ERROR_TOLERANCE);

        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_THUMBSTICK],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_L2], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_THUMBSTICK],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_R2], ERROR_TOLERANCE);

        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                (mRawAxes[MotionEvent.AXIS_Z] + 1) / 2, ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                (mRawAxes[MotionEvent.AXIS_RZ] + 1) / 2, ERROR_TOLERANCE);

        assertMappedHatAxisToDpadButtons();
        assertMappedXYAxes();
        assertMappedRXAndRYAxesToRightStick();

        expectNoMetaButton();
        assertMapping();
    }

    @Test
    @Feature({"Gamepad"})
    public void testUnknownXBox360GamepadMappings() throws Exception {
        int[] axes = new int[] {
            MotionEvent.AXIS_X,
            MotionEvent.AXIS_Y,
            MotionEvent.AXIS_Z,
            MotionEvent.AXIS_RZ,
            MotionEvent.AXIS_LTRIGGER,
            MotionEvent.AXIS_RTRIGGER,
            MotionEvent.AXIS_HAT_X,
            MotionEvent.AXIS_HAT_Y
        };

        GamepadMappings mappings = GamepadMappings.getMappings("", axes);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertMappedCommonXYABButtons();
        assertMappedTriggerButtonsToTopShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedTriggerAxesToBottomShoulder();
        assertMappedHatAxisToDpadButtons();
        assertMappedXYAxes();
        assertMappedZAndRZAxesToRightStick();

        assertMapping();
    }

    @Test
    @Feature({"Gamepad"})
    public void testUnknownMogaProGamepadMappings() throws Exception {
        int[] axes = new int[] {
            MotionEvent.AXIS_X,
            MotionEvent.AXIS_Y,
            MotionEvent.AXIS_Z,
            MotionEvent.AXIS_RZ,
            MotionEvent.AXIS_BRAKE,
            MotionEvent.AXIS_GAS,
            MotionEvent.AXIS_HAT_X,
            MotionEvent.AXIS_HAT_Y
        };

        GamepadMappings mappings = GamepadMappings.getMappings("", axes);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertMappedCommonXYABButtons();
        assertMappedTriggerButtonsToTopShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedPedalAxesToBottomShoulder();
        assertMappedHatAxisToDpadButtons();
        assertMappedXYAxes();
        assertMappedZAndRZAxesToRightStick();

        assertMapping();
    }

    @Test
    @Feature({"Gamepad"})
    public void testUnknownXiaomiGamepadMappings() throws Exception {
        int[] axes = new int[] {
            MotionEvent.AXIS_X,
            MotionEvent.AXIS_Y,
            MotionEvent.AXIS_RX,
            MotionEvent.AXIS_RY,
            MotionEvent.AXIS_BRAKE,
            MotionEvent.AXIS_THROTTLE,
            MotionEvent.AXIS_HAT_X,
            MotionEvent.AXIS_HAT_Y
        };

        GamepadMappings mappings = GamepadMappings.getMappings("", axes);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertMappedCommonXYABButtons();
        assertMappedTriggerButtonsToTopShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedAltPedalAxesToBottomShoulder();
        assertMappedHatAxisToDpadButtons();
        assertMappedXYAxes();
        assertMappedRXAndRYAxesToRightStick();

        assertMapping();
    }

    @Test
    @Feature({"Gamepad"})
    public void testUnknownGpdXdGamepadMappings() throws Exception {
        int[] axes = new int[] {
            MotionEvent.AXIS_X,
            MotionEvent.AXIS_Y,
            MotionEvent.AXIS_Z,
            MotionEvent.AXIS_RZ
        };

        GamepadMappings mappings = GamepadMappings.getMappings("", axes);
        mappings.mapToStandardGamepad(mMappedAxes, mMappedButtons, mRawAxes, mRawButtons);

        assertMappedCommonXYABButtons();
        assertMappedTriggerButtonsToTopShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedLowerTriggerButtonsToBottomShoulder();
        assertMappedCommonDpadButtons();
        assertMappedXYAxes();
        assertMappedZAndRZAxesToRightStick();

        assertMapping();
    }

    /**
     * Asserts that the current gamepad mapping being tested matches the shield mappings.
     */
    public void assertShieldGamepadMappings() {
        assertMappedCommonXYABButtons();
        assertMappedTriggerButtonsToTopShoulder();
        assertMappedCommonThumbstickButtons();
        assertMappedCommonStartSelectMetaButtons();
        assertMappedTriggerAxesToBottomShoulder();
        assertMappedHatAxisToDpadButtons();
        assertMappedXYAxes();
        assertMappedZAndRZAxesToRightStick();

        assertMapping();
    }

    public void expectNoShoulderButtons() {
        mUnmappedButtons.set(CanonicalButtonIndex.LEFT_SHOULDER);
        mUnmappedButtons.set(CanonicalButtonIndex.RIGHT_SHOULDER);
    }

    public void expectNoMetaButton() {
        mUnmappedButtons.set(CanonicalButtonIndex.META);
    }

    public void assertMapping() {
        for (int i = 0; i < mMappedAxes.length; i++) {
            if (mUnmappedAxes.get(i)) {
                Assert.assertTrue(
                        "An unexpected axis was mapped at index " + i, Float.isNaN(mMappedAxes[i]));
            } else {
                Assert.assertFalse(
                        "An axis was not mapped at index " + i, Float.isNaN(mMappedAxes[i]));
            }
        }
        for (int i = 0; i < mMappedButtons.length; i++) {
            if (mUnmappedButtons.get(i)) {
                Assert.assertTrue("An unexpected button was mapped at index " + i,
                        Float.isNaN(mMappedButtons[i]));
            } else {
                Assert.assertFalse(
                        "A button was not mapped at index " + i, Float.isNaN(mMappedButtons[i]));
            }
        }
    }

    private void assertMappedUpperTriggerButtonsToBottomShoulder() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_L1], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_R1], ERROR_TOLERANCE);
    }

    private void assertMappedLowerTriggerButtonsToBottomShoulder() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_L2], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_R2], ERROR_TOLERANCE);
    }

    private void assertMappedCommonDpadButtons() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_DOWN],
                mRawButtons[KeyEvent.KEYCODE_DPAD_DOWN], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_UP],
                mRawButtons[KeyEvent.KEYCODE_DPAD_UP], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_LEFT],
                mRawButtons[KeyEvent.KEYCODE_DPAD_LEFT], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_RIGHT],
                mRawButtons[KeyEvent.KEYCODE_DPAD_RIGHT], ERROR_TOLERANCE);
    }

    private void assertMappedTriggerButtonsToTopShoulder() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_SHOULDER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_L1], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_SHOULDER],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_R1], ERROR_TOLERANCE);
    }

    private void assertMappedCommonXYABButtons() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.PRIMARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_A], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.SECONDARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_B], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.TERTIARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_X], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.QUATERNARY],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_Y], ERROR_TOLERANCE);
    }

    private void assertMappedCommonThumbstickButtons() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_THUMBSTICK],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_THUMBL], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_THUMBSTICK],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_THUMBR], ERROR_TOLERANCE);
    }

    private void assertMappedCommonStartSelectMetaButtons() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.START],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_START], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.BACK_SELECT],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_SELECT], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.META],
                mRawButtons[KeyEvent.KEYCODE_BUTTON_MODE], ERROR_TOLERANCE);
    }

    private void assertMappedPedalAxesToBottomShoulder() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                mRawAxes[MotionEvent.AXIS_BRAKE], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                mRawAxes[MotionEvent.AXIS_GAS], ERROR_TOLERANCE);
    }

    private void assertMappedAltPedalAxesToBottomShoulder() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                mRawAxes[MotionEvent.AXIS_BRAKE], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                mRawAxes[MotionEvent.AXIS_THROTTLE], ERROR_TOLERANCE);
    }

    private void assertMappedTriggerAxesToBottomShoulder() {
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.LEFT_TRIGGER],
                mRawAxes[MotionEvent.AXIS_LTRIGGER], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.RIGHT_TRIGGER],
                mRawAxes[MotionEvent.AXIS_RTRIGGER], ERROR_TOLERANCE);
    }

    private void assertMappedHatAxisToDpadButtons() {
        float hatX = mRawAxes[MotionEvent.AXIS_HAT_X];
        float hatY = mRawAxes[MotionEvent.AXIS_HAT_Y];
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_LEFT],
                GamepadMappings.negativeAxisValueAsButton(hatX), ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_RIGHT],
                GamepadMappings.positiveAxisValueAsButton(hatX), ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_UP],
                GamepadMappings.negativeAxisValueAsButton(hatY), ERROR_TOLERANCE);
        Assert.assertEquals(mMappedButtons[CanonicalButtonIndex.DPAD_DOWN],
                GamepadMappings.positiveAxisValueAsButton(hatY), ERROR_TOLERANCE);
    }

    private void assertMappedXYAxes() {
        Assert.assertEquals(mMappedAxes[CanonicalAxisIndex.LEFT_STICK_X],
                mRawAxes[MotionEvent.AXIS_X], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedAxes[CanonicalAxisIndex.LEFT_STICK_Y],
                mRawAxes[MotionEvent.AXIS_Y], ERROR_TOLERANCE);
    }

    private void assertMappedRXAndRYAxesToRightStick() {
        Assert.assertEquals(mMappedAxes[CanonicalAxisIndex.RIGHT_STICK_X],
                mRawAxes[MotionEvent.AXIS_RX], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedAxes[CanonicalAxisIndex.RIGHT_STICK_Y],
                mRawAxes[MotionEvent.AXIS_RY], ERROR_TOLERANCE);
    }

    private void assertMappedZAndRZAxesToRightStick() {
        Assert.assertEquals(mMappedAxes[CanonicalAxisIndex.RIGHT_STICK_X],
                mRawAxes[MotionEvent.AXIS_Z], ERROR_TOLERANCE);
        Assert.assertEquals(mMappedAxes[CanonicalAxisIndex.RIGHT_STICK_Y],
                mRawAxes[MotionEvent.AXIS_RZ], ERROR_TOLERANCE);
    }
}

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/metrics/desktop_session_duration/desktop_session_duration_tracker.h"

#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/test/histogram_tester.h"
#include "base/test/test_mock_time_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"

// Mock class for |DesktopSessionDurationTracker| for testing.
class MockDesktopSessionDurationTracker
    : public metrics::DesktopSessionDurationTracker {
 public:
  MockDesktopSessionDurationTracker() {}

  bool is_timeout() const { return time_out_; }

  using metrics::DesktopSessionDurationTracker::OnAudioStart;
  using metrics::DesktopSessionDurationTracker::OnAudioEnd;

 protected:
  void OnTimerFired() override {
    DesktopSessionDurationTracker::OnTimerFired();
    time_out_ = true;
  }

 private:
  bool time_out_ = false;

  DISALLOW_COPY_AND_ASSIGN(MockDesktopSessionDurationTracker);
};

TEST(DesktopSessionDurationTrackerTest, TestVisibility) {
  base::MessageLoop loop(base::MessageLoop::TYPE_DEFAULT);
  base::HistogramTester histogram_tester;

  MockDesktopSessionDurationTracker instance;

  // The browser becomes visible but it shouldn't start the session.
  instance.OnVisibilityChanged(true);
  EXPECT_FALSE(instance.in_session());
  EXPECT_TRUE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  instance.OnUserEvent();
  EXPECT_TRUE(instance.in_session());
  EXPECT_TRUE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  // Even if there is a recent user event visibility change should end the
  // session.
  instance.OnUserEvent();
  instance.OnUserEvent();
  instance.OnVisibilityChanged(false);
  EXPECT_FALSE(instance.in_session());
  EXPECT_FALSE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 1);

  // For the second time only visibility change should start the session.
  instance.OnVisibilityChanged(true);
  EXPECT_TRUE(instance.in_session());
  EXPECT_TRUE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 1);
  instance.OnVisibilityChanged(false);
  EXPECT_FALSE(instance.in_session());
  EXPECT_FALSE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 2);
}

TEST(DesktopSessionDurationTrackerTest, TestUserEvent) {
  base::MessageLoop loop(base::MessageLoop::TYPE_DEFAULT);
  base::HistogramTester histogram_tester;

  MockDesktopSessionDurationTracker instance;
  instance.SetInactivityTimeoutForTesting(1);

  EXPECT_FALSE(instance.in_session());
  EXPECT_FALSE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  // User event doesn't go through if nothing is visible.
  instance.OnUserEvent();
  EXPECT_FALSE(instance.in_session());
  EXPECT_FALSE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  instance.OnVisibilityChanged(true);
  instance.OnUserEvent();
  EXPECT_TRUE(instance.in_session());
  EXPECT_TRUE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  // Wait until the session expires.
  while (!instance.is_timeout()) {
    base::RunLoop().RunUntilIdle();
  }

  EXPECT_FALSE(instance.in_session());
  EXPECT_TRUE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 1);
}

TEST(DesktopSessionDurationTrackerTest, TestAudioEvent) {
  base::MessageLoop loop(base::MessageLoop::TYPE_DEFAULT);
  base::HistogramTester histogram_tester;

  MockDesktopSessionDurationTracker instance;
  instance.SetInactivityTimeoutForTesting(1);

  instance.OnVisibilityChanged(true);
  instance.OnAudioStart();
  EXPECT_TRUE(instance.in_session());
  EXPECT_TRUE(instance.is_visible());
  EXPECT_TRUE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  instance.OnVisibilityChanged(false);
  EXPECT_TRUE(instance.in_session());
  EXPECT_FALSE(instance.is_visible());
  EXPECT_TRUE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  instance.OnAudioEnd();
  EXPECT_TRUE(instance.in_session());
  EXPECT_FALSE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  // Wait until the session expires.
  while (!instance.is_timeout()) {
    base::RunLoop().RunUntilIdle();
  }

  EXPECT_FALSE(instance.in_session());
  EXPECT_FALSE(instance.is_visible());
  EXPECT_FALSE(instance.is_audio_playing());
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 1);
}

TEST(DesktopSessionDurationTrackerTest, TestTimeoutDiscount) {
  base::MessageLoop loop(base::MessageLoop::TYPE_DEFAULT);
  base::HistogramTester histogram_tester;
  MockDesktopSessionDurationTracker instance;

  int inactivity_interval_seconds = 2;
  instance.SetInactivityTimeoutForTesting(inactivity_interval_seconds);

  instance.OnVisibilityChanged(true);
  base::TimeTicks before_session_start = base::TimeTicks::Now();
  instance.OnUserEvent();  // This should start the session
  histogram_tester.ExpectTotalCount("Session.TotalDuration", 0);

  // Wait until the session expires.
  while (!instance.is_timeout()) {
    base::RunLoop().RunUntilIdle();
  }
  base::TimeTicks after_session_end = base::TimeTicks::Now();

  histogram_tester.ExpectTotalCount("Session.TotalDuration", 1);
  base::Bucket bucket =
      histogram_tester.GetAllSamples("Session.TotalDuration")[0];
  int max_expected_value =
      (after_session_end - before_session_start -
       base::TimeDelta::FromSeconds(inactivity_interval_seconds))
          .InMilliseconds();
  EXPECT_LE(bucket.min, max_expected_value);
}

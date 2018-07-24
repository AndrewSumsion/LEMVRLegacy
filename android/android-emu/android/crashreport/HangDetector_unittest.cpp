// Copyright 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/crashreport/HangDetector.h"

#include "android/base/async/ThreadLooper.h"
#include "android/base/testing/TestSystem.h"
#include "android/globals.h"

#include <gtest/gtest.h>

using namespace android::base;
using namespace android::crashreport;

class HangDetectorTest : public ::testing::Test {
public:
    HangDetectorTest()
        : mHangDetector(
                  [this](StringView msg) {
                      android::base::AutoLock lock(mLock);
                      mHangDetected = true;
                      mHangCondition.signalAndUnlock(&lock);
                  },
                  {
                          .hangLoopIterationTimeoutMs = 1,
                          .taskProcessingTimeoutMs = 1,
                          .hangCheckTimeoutMs = 1,
                  }) {
        android_avdInfo = nullptr;
    }

    void TearDown() override { mHangDetector.stop(); }

    void waitUntilUnlocked() {
        android::base::AutoLock lock(mLock);
        auto waitUntil = System::get()->getUnixTimeUs() + kMaxBlockingTimeUs;
        while (!mHangDetected && System::get()->getUnixTimeUs() < waitUntil) {
            mHangCondition.timedWait(&mLock, waitUntil);
        }
        EXPECT_TRUE(mHangDetected);
    }

protected:
    const System::Duration kMaxBlockingTimeUs = 5 * 60 * 1000 * 1000;
    Lock mLock;
    android::base::ConditionVariable mHangCondition;
    bool mHangDetected{false};
    HangDetector mHangDetector;
};

TEST_F(HangDetectorTest, PredicateTriggersHang) {
    mHangDetector.addPredicateCheck([] { return true; }, "Always dead");
    waitUntilUnlocked();
}

TEST_F(HangDetectorTest, BlockedLooperTriggersHang) {
    // Note that the looper is not running!
    auto looper = ThreadLooper::get();
    mHangDetector.addWatchedLooper(looper);
    waitUntilUnlocked();
}

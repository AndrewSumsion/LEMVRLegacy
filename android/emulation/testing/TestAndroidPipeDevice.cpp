// Copyright 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/emulation/testing/TestAndroidPipeDevice.h"

#include "android/base/Log.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

namespace android {

namespace {

class TestGuest : public TestAndroidPipeDevice::Guest {
public:
    TestGuest() : mClosed(true), mWakes(0u), mPipe(nullptr) {
        mPipe = android_pipe_new(this);
        if (!mPipe) {
            LOG(ERROR) << "Could not create new "
                          "TestAndroidPipeDevice::Guest instance!";
        }
    }

    virtual ~TestGuest() {
        if (mPipe) {
            android_pipe_free(mPipe);
        }
    }

    virtual int connect(const char* name) override {
        std::string handshake("pipe:");
        handshake += name;
        int len = static_cast<int>(handshake.size()) + 1;
        mClosed = false;
        int ret = write(handshake.c_str(), len);
        if (ret != len) {
            LOG(ERROR) << "Could not connect to service " << name
                       << " ret=" << ret << " expected len=" << len;
            mClosed = true;
            return -EINVAL;
        }
        return 0;
    }

    virtual ssize_t read(void* buffer, size_t len) override {
        if (mClosed) {
            return 0;
        }
        AndroidPipeBuffer buf = { static_cast<uint8_t*>(buffer), len };
        return android_pipe_recv(mPipe, &buf, 1);
    }

    virtual ssize_t write(const void* buffer, size_t len) override {
        if (mClosed) {
            return 0;
        }
        AndroidPipeBuffer buf = {
                (uint8_t*)buffer, len };
        return android_pipe_send(mPipe, &buf, 1);
    }

    virtual unsigned poll() const override {
        if (mClosed) {
            return PIPE_POLL_HUP;
        }
        return android_pipe_poll(mPipe);
    }

    void closeFromHost() {
        mClosed = true;
    }

    void signalWake(int wakes) {
        // NOTE: Update the flags, but for now don't do anything
        //       about them.
        mWakes |= wakes;
    }

private:
    bool mClosed;
    unsigned mWakes;
    void* mPipe;
};

}  // namespace

TestAndroidPipeDevice::TestAndroidPipeDevice()
        : mOldHwFuncs(android_pipe_set_hw_funcs(&sHwFuncs)) {
    android_pipe_reset_services();
}

TestAndroidPipeDevice::~TestAndroidPipeDevice() {
    android_pipe_set_hw_funcs(mOldHwFuncs);
    android_pipe_reset_services();
}

// static
const AndroidPipeHwFuncs TestAndroidPipeDevice::sHwFuncs = {
    &TestAndroidPipeDevice::closeFromHost,
    &TestAndroidPipeDevice::signalWake,
};

// static
void TestAndroidPipeDevice::closeFromHost(void* hwpipe) {
    auto guest = reinterpret_cast<TestGuest*>(hwpipe);
    guest->closeFromHost();
}

// static
void TestAndroidPipeDevice::signalWake(void* hwpipe, unsigned wakes) {
    auto guest = reinterpret_cast<TestGuest*>(hwpipe);
    guest->signalWake(wakes);
}

// static
TestAndroidPipeDevice::Guest* TestAndroidPipeDevice::Guest::create() {
    return new TestGuest();
}

}  // namespace android

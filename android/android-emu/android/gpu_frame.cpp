// Copyright (C) 2015 The Android Open Source Project
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

#include "android/gpu_frame.h"

#include <atomic>

#include "android/base/Log.h"
#include "android/base/memory/LazyInstance.h"
#include "android/opengl/GpuFrameBridge.h"
#include "android/opengles.h"

// Standard values from Khronos.
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

using android::opengl::GpuFrameBridge;

static GpuFrameBridge* sBridge = NULL;
// We need some way to disable the post() if only the recording is using that
// path and it is not in use because glReadPixels will slow down everything.
static bool sIsGuestMode = false;


static FrameAvailableCallback sFrameReceiver;
static void* sFrameReceiverOpaque;

// Used to keep track of how many recorders we have active.
// Frame forwarding will stop if this hits 0.
static std::atomic<int> sRecordCounter(0);

// Called from an EmuGL thread to transfer a new frame of the GPU display
// to the main loop.

typedef void (*on_new_gpu_frame_t)(void* opaque, int width, int height,
                                   int ydir, int format, int type,
                                   unsigned char* pixels);
// Guest mode:
static void onNewGpuFrame_guest(void* opaque, int width, int height,
                                int ydir, int format, int type,
                                unsigned char* pixels) {
    DCHECK(ydir == -1);
    DCHECK(format == GL_RGBA);
    DCHECK(type == GL_UNSIGNED_BYTE);

    GpuFrameBridge* bridge = reinterpret_cast<GpuFrameBridge*>(opaque);
    bridge->postFrame(width, height, pixels);
}

// Recording (synchronous):
static void onNewGpuFrame_record(void* opaque, int width, int height,
                                int ydir, int format, int type,
                                unsigned char* pixels) {
    DCHECK(ydir == -1);
    DCHECK(format == GL_RGBA);
    DCHECK(type == GL_UNSIGNED_BYTE);

    GpuFrameBridge* bridge = reinterpret_cast<GpuFrameBridge*>(opaque);
    bridge->postRecordFrame(width, height, pixels);
}

// Recording (asynchronous):
static void onNewGpuFrame_recordAsync(void* opaque, int width, int height,
                                      int ydir, int format, int type,
                                      unsigned char* pixels) {
    DCHECK(ydir == -1);
    DCHECK(format == GL_RGBA);
    DCHECK(type == GL_UNSIGNED_BYTE);

    GpuFrameBridge* bridge = reinterpret_cast<GpuFrameBridge*>(opaque);
    bridge->postRecordFrameAsync(width, height, pixels);
}

static on_new_gpu_frame_t choose_on_new_gpu_frame() {
    if (sIsGuestMode) {
        return onNewGpuFrame_guest;
    } else {
        if (android_asyncReadbackSupported()) {
            return onNewGpuFrame_recordAsync;
        } else {
            return onNewGpuFrame_record;
        }
    }
}

static void gpu_frame_set_post(bool on) {
    CHECK(sBridge);

    if (on) {
        android_setPostCallback(choose_on_new_gpu_frame(), sBridge, true /* BGRA readback */);
    } else {
        android_setPostCallback(nullptr, nullptr, true /* BGRA readback */);
    }
}

static void frame_callback() {
    if (sFrameReceiver) {
        sFrameReceiver(sFrameReceiverOpaque);
    }
}
void gpu_frame_set_post_callback(Looper* looper,
                                 void* context,
                                 on_post_callback_t callback) {
    DCHECK(!sBridge);
    sBridge = android::opengl::GpuFrameBridge::create(
            reinterpret_cast<android::base::Looper*>(looper), callback,
            context);
    CHECK(sBridge);
    sBridge->setFrameReceiver(sFrameReceiver, sFrameReceiverOpaque);
    android_setPostCallback(choose_on_new_gpu_frame(), sBridge, true /* BGRA readback */);
    sIsGuestMode = true;
}

bool gpu_frame_set_record_mode(bool on) {
    // Assumption: gpu_frame_set_post_callback() is called before this one, so
    // we can determine if we are in host mode based on if sBridge is set.
    if (sIsGuestMode) {
        return false;
    }

    // Note that we can have multiple recorders active at the same time:
    // 1. The WebRTC module might want to expose the shared region
    // 2. A Java View might want expose the shared region inside android studio
    // 3. The ffmpeg based video recorder might want to receive frames.
    // The updates are atomic operations.
    sRecordCounter += on ? 1 : -1;

    if (!sBridge) {
        sBridge = android::opengl::GpuFrameBridge::create(nullptr, nullptr,
                                                          nullptr);
        sBridge->setFrameReceiver(sFrameReceiver, sFrameReceiverOpaque);
    }
    CHECK(sBridge);

    // We need frames if we have at least one recorder.
    gpu_frame_set_post(sRecordCounter > 0);

    // Need to invalidate the recording buffers in GpuFrameBridge so on the next
    // recording we only read the new data and not data from the previous
    // recording. The buffers will be valid again once new data has been posted.
    if (!on) {
        sBridge->invalidateRecordingBuffers();
    }
    return true;
}

void* gpu_frame_get_record_frame() {
    CHECK(sBridge);
    if (android_asyncReadbackSupported()) {
        return sBridge->getRecordFrameAsync();
    } else {
        return sBridge->getRecordFrame();
    }
}

void gpu_set_shared_memory_callback(FrameAvailableCallback frameAvailable, void* opaque) {
        sFrameReceiver = frameAvailable;
        sFrameReceiverOpaque = opaque;

    if (sBridge) {
        sBridge->setFrameReceiver(sFrameReceiver, sFrameReceiverOpaque);
    }
}

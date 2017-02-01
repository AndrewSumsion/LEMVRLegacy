/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "RenderThread.h"

#include "ChannelStream.h"
#include "ErrorLog.h"
#include "FrameBuffer.h"
#include "ReadBuffer.h"
#include "RenderControl.h"
#include "RendererImpl.h"
#include "RenderChannelImpl.h"
#include "RenderThreadInfo.h"

#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "OpenGLESDispatch/GLESv1Dispatch.h"
#include "../../../shared/OpenglCodecCommon/ChecksumCalculatorThreadInfo.h"

#include "android/base/system/System.h"
#include "android/base/files/StreamSerializing.h"

#define EMUGL_DEBUG_LEVEL 0
#include "emugl/common/debug.h"

#include <assert.h>

using android::base::AutoLock;

namespace emugl {

struct RenderThread::SnapshotObjects {
    RenderThreadInfo* threadInfo;
    ChecksumCalculator* checksumCalc;
    ChannelStream* channelStream;
    ReadBuffer* readBuffer;
};

// Start with a smaller buffer to not waste memory on a low-used render threads.
static constexpr int kStreamBufferSize = 128 * 1024;

RenderThread::RenderThread(RenderChannelImpl* channel,
                           android::base::Stream* loadStream)
    : emugl::Thread(android::base::ThreadFlags::MaskSignals, 2 * 1024 * 1024),
      mChannel(channel) {
    if (loadStream) {
        mStream.emplace(0);
        android::base::loadStream(loadStream, &*mStream);
        mState = SnapshotState::StartLoading;
    }
}

RenderThread::~RenderThread() = default;

void RenderThread::pausePreSnapshot() {
    AutoLock lock(mLock);
    assert(mState == SnapshotState::Empty);
    mStream.emplace();
    mState = SnapshotState::StartSaving;
    mChannel->pausePreSnapshot();
    mCondVar.broadcastAndUnlock(&lock);
}

void RenderThread::resume() {
    AutoLock lock(mLock);
    // This function can be called for a thread from pre-snapshot loading
    // state; it doesn't need to do anything.
    if (mState != SnapshotState::InProgress &&
        mState != SnapshotState::Finished &&
        mState != SnapshotState::StartLoading) {
        return;
    }
    waitForSnapshotCompletion(&lock);
    mStream.clear();
    mState = SnapshotState::Empty;
    mChannel->resume();
    mCondVar.broadcastAndUnlock(&lock);
}

void RenderThread::save(android::base::Stream* stream) {
    {
        AutoLock lock(mLock);
        assert(mState == SnapshotState::StartSaving ||
               mState == SnapshotState::InProgress ||
               mState == SnapshotState::Finished);
        waitForSnapshotCompletion(&lock);
    }

    assert(mStream);
    android::base::saveStream(stream, *mStream);
}

void RenderThread::waitForSnapshotCompletion(AutoLock* lock) {
    while (mState != SnapshotState::Finished) {
        mCondVar.wait(lock);
    }
}

template <class OpImpl>
void RenderThread::snapshotOperation(AutoLock* lock, OpImpl&& implFunc) {
    assert(isPausedForSnapshotLocked());
    mState = SnapshotState::InProgress;
    mCondVar.broadcastAndUnlock(lock);

    implFunc();

    lock->lock();

    mState = SnapshotState::Finished;
    mCondVar.broadcast();

    // Only return after we're allowed to proceed.
    while (isPausedForSnapshotLocked()) {
        mCondVar.wait(lock);
    }
}

void RenderThread::loadImpl(AutoLock* lock, const SnapshotObjects& objects) {
    snapshotOperation(lock, [this, &objects] {
        objects.readBuffer->onLoad(&*mStream);
        objects.channelStream->load(&*mStream);
        // TODO: load objects.checksumCalc
        // TODO: load objects.threadInfo
    });
}

void RenderThread::saveImpl(AutoLock* lock, const SnapshotObjects& objects) {
    snapshotOperation(lock, [this, &objects] {
        objects.readBuffer->onSave(&*mStream);
        objects.channelStream->save(&*mStream);
        // TODO: save objects.checksumCalc
        // TODO: save objects.threadInfo
    });
}

bool RenderThread::isPausedForSnapshotLocked() const {
    return mState != SnapshotState::Empty;
}

bool RenderThread::doSnapshotOperation(const SnapshotObjects& objects,
                                       SnapshotState state) {
    AutoLock lock(mLock);
    if (mState == state) {
        switch (state) {
            case SnapshotState::StartLoading:
                loadImpl(&lock, objects);
                return true;
            case SnapshotState::StartSaving:
                saveImpl(&lock, objects);
                return true;
            default:
                return false;
        }
    }
    return false;
}

intptr_t RenderThread::main() {
    RenderThreadInfo tInfo;
    ChecksumCalculatorThreadInfo tChecksumInfo;
    ChecksumCalculator& checksumCalc = tChecksumInfo.get();

    //
    // initialize decoders
    //
    tInfo.m_glDec.initGL(gles1_dispatch_get_proc_func, nullptr);
    tInfo.m_gl2Dec.initGL(gles2_dispatch_get_proc_func, nullptr);
    initRenderControlContext(&tInfo.m_rcDec);

    ChannelStream stream(mChannel, RenderChannel::Buffer::kSmallSize);
    ReadBuffer readBuf(kStreamBufferSize);

    const SnapshotObjects snapshotObjects = {
        &tInfo, &checksumCalc, &stream, &readBuf
    };

    // This is the only place where we try loading from snapshot.
    if (doSnapshotOperation(snapshotObjects, SnapshotState::StartLoading)) {
        DBG("Loaded RenderThread @%p from snapshot\n", this);
    } else {
        // Not loading from a snapshot: continue regular startup, read
        // the |flags|.
        uint32_t flags = 0;
        while (stream.read(&flags, sizeof(flags)) != sizeof(flags)) {
            // Stream read may fail because of a pending snapshot.
            if (!doSnapshotOperation(snapshotObjects, SnapshotState::StartSaving)) {
                return 0;
            }
        }

        // |flags| used to mean something, now they're not used.
        (void)flags;
    }

    int stats_totalBytes = 0;
    auto stats_t0 = android::base::System::get()->getHighResTimeUs() / 1000;

    //
    // open dump file if RENDER_DUMP_DIR is defined
    //
    const char* dump_dir = getenv("RENDERER_DUMP_DIR");
    FILE* dumpFP = nullptr;
    if (dump_dir) {
        size_t bsize = strlen(dump_dir) + 32;
        char* fname = new char[bsize];
        snprintf(fname, bsize, "%s/stream_%p", dump_dir, this);
        dumpFP = fopen(fname, "wb");
        if (!dumpFP) {
            fprintf(stderr, "Warning: stream dump failed to open file %s\n",
                    fname);
        }
        delete[] fname;
    }

    while (1) {
        // Let's make sure we read enough data for at least some processing.
        int packetSize;
        if (readBuf.validData() >= 8) {
            // We know that packet size is the second int32_t from the start.
            packetSize = *(const int32_t*)(readBuf.buf() + 4);
        } else {
            // Read enough data to at least be able to get the packet size next
            // time.
            packetSize = 8;
        }

        int stat = 0;
        if (packetSize > (int)readBuf.validData()) {
            stat = readBuf.getData(&stream, packetSize);
            if (stat <= 0) {
                if (doSnapshotOperation(snapshotObjects, SnapshotState::StartSaving)) {
                    continue;
                } else {
                    D("Warning: render thread could not read data from stream");
                    break;
                }
            }
        }

        DD("render thread read %d bytes, op %d, packet size %d",
           (int)readBuf.validData(), *(int32_t*)readBuf.buf(),
           *(int32_t*)(readBuf.buf() + 4));

        //
        // log received bandwidth statistics
        //
        stats_totalBytes += readBuf.validData();
        auto dt = android::base::System::get()->getHighResTimeUs() / 1000 - stats_t0;
        if (dt > 1000) {
            // float dts = (float)dt / 1000.0f;
            // printf("Used Bandwidth %5.3f MB/s\n", ((float)stats_totalBytes /
            // dts) / (1024.0f*1024.0f));
            stats_totalBytes = 0;
            stats_t0 = android::base::System::get()->getHighResTimeUs() / 1000;
        }

        //
        // dump stream to file if needed
        //
        if (dumpFP) {
            int skip = readBuf.validData() - stat;
            fwrite(readBuf.buf() + skip, 1, readBuf.validData() - skip, dumpFP);
            fflush(dumpFP);
        }

        bool progress;
        do {
            progress = false;

            // try to process some of the command buffer using the GLESv1
            // decoder
            //
            // DRIVER WORKAROUND:
            // On Linux with NVIDIA GPU's at least, we need to avoid performing
            // GLES ops while someone else holds the FrameBuffer write lock.
            //
            // To be more specific, on Linux with NVIDIA Quadro K2200 v361.xx,
            // we get a segfault in the NVIDIA driver when glTexSubImage2D
            // is called at the same time as glXMake(Context)Current.
            //
            // To fix, this driver workaround avoids calling
            // any sort of GLES call when we are creating/destroying EGL
            // contexts.
            FrameBuffer::getFB()->lockContextStructureRead();
            size_t last = tInfo.m_glDec.decode(
                    readBuf.buf(), readBuf.validData(), &stream, &checksumCalc);
            if (last > 0) {
                progress = true;
                readBuf.consume(last);
            }

            //
            // try to process some of the command buffer using the GLESv2
            // decoder
            //
            last = tInfo.m_gl2Dec.decode(readBuf.buf(), readBuf.validData(),
                                         &stream, &checksumCalc);
            FrameBuffer::getFB()->unlockContextStructureRead();

            if (last > 0) {
                progress = true;
                readBuf.consume(last);
            }

            //
            // try to process some of the command buffer using the
            // renderControl decoder
            //
            last = tInfo.m_rcDec.decode(readBuf.buf(), readBuf.validData(),
                                        &stream, &checksumCalc);
            if (last > 0) {
                readBuf.consume(last);
                progress = true;
            }
        } while (progress);
    }

    if (dumpFP) {
        fclose(dumpFP);
    }

    // Don't check for snapshots here: if we're already exiting then snapshot
    // should not contain this thread information at all.

    // exit sync thread, if any.
    SyncThread::destroySyncThread();

    // Release references to the current thread's context/surfaces if any
    FrameBuffer::getFB()->bindContext(0, 0, 0);
    if (tInfo.currContext || tInfo.currDrawSurf || tInfo.currReadSurf) {
        fprintf(stderr,
                "ERROR: RenderThread exiting with current context/surfaces\n");
    }

    FrameBuffer::getFB()->drainWindowSurface();
    FrameBuffer::getFB()->drainRenderContext();

    DBG("Exited a RenderThread @%p\n", this);

    return 0;
}

}  // namespace emugl

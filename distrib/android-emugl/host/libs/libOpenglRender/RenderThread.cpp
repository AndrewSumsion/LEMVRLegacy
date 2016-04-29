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
#include "TimeUtils.h"

#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "OpenGLESDispatch/GLESv1Dispatch.h"
#include "../../../shared/OpenglCodecCommon/ChecksumCalculatorThreadInfo.h"

#include <memory>

#define STREAM_BUFFER_SIZE 4 * 1024 * 1024

RenderThread::RenderThread(std::weak_ptr<emugl::RendererImpl> renderer,
                           std::shared_ptr<emugl::RenderChannelImpl> channel,
                           emugl::Mutex* lock)
    : m_lock(lock), mChannel(channel), mRenderer(renderer) {}

RenderThread::~RenderThread() = default;

// static
std::unique_ptr<RenderThread> RenderThread::create(
        std::weak_ptr<emugl::RendererImpl> renderer,
        std::shared_ptr<emugl::RenderChannelImpl> channel,
        emugl::Mutex* lock) {
    return std::unique_ptr<RenderThread>(
            new RenderThread(renderer, channel, lock));
}

intptr_t RenderThread::main() {
    uint32_t flags = 0;
    if (mChannel->readFromGuest(reinterpret_cast<char*>(&flags),
                                sizeof(flags), true) != sizeof(flags)) {
        return 0;
    }

    if ((flags & IOSTREAM_CLIENT_EXIT_SERVER) == IOSTREAM_CLIENT_EXIT_SERVER) {
        // The old code had a separate server thread, this flag meant 'exit the
        // server thread'. It's not used anymore, but let's just make sure...
        if (auto renderer = mRenderer.lock()) {
            renderer->stop();
        }
        return 0;
    }

    std::unique_ptr<IOStream> stream(new ChannelStream(mChannel, 384));
    if (!stream) {
        return 0;
    }

    RenderThreadInfo tInfo;
    ChecksumCalculatorThreadInfo tChecksumInfo;

    //
    // initialize decoders
    //
    tInfo.m_glDec.initGL(gles1_dispatch_get_proc_func, NULL);
    tInfo.m_gl2Dec.initGL(gles2_dispatch_get_proc_func, NULL);
    initRenderControlContext(&tInfo.m_rcDec);

    ReadBuffer readBuf(STREAM_BUFFER_SIZE);

    int stats_totalBytes = 0;
    long long stats_t0 = GetCurrentTimeMS();

    //
    // open dump file if RENDER_DUMP_DIR is defined
    //
    const char* dump_dir = getenv("RENDERER_DUMP_DIR");
    FILE* dumpFP = NULL;
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
        int stat = readBuf.getData(stream.get());
        if (stat <= 0) {
            break;
        }

        //
        // log received bandwidth statistics
        //
        stats_totalBytes += readBuf.validData();
        long long dt = GetCurrentTimeMS() - stats_t0;
        if (dt > 1000) {
            // float dts = (float)dt / 1000.0f;
            // printf("Used Bandwidth %5.3f MB/s\n", ((float)stats_totalBytes /
            // dts) / (1024.0f*1024.0f));
            stats_totalBytes = 0;
            stats_t0 = GetCurrentTimeMS();
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

            m_lock->lock();
            //
            // try to process some of the command buffer using the GLESv1
            // decoder
            //
            size_t last = tInfo.m_glDec.decode(
                    readBuf.buf(), readBuf.validData(), stream.get());
            if (last > 0) {
                progress = true;
                readBuf.consume(last);
            }

            //
            // try to process some of the command buffer using the GLESv2
            // decoder
            //
            last = tInfo.m_gl2Dec.decode(readBuf.buf(), readBuf.validData(),
                                         stream.get());
            if (last > 0) {
                progress = true;
                readBuf.consume(last);
            }

            //
            // try to process some of the command buffer using the
            // renderControl decoder
            //
            last = tInfo.m_rcDec.decode(readBuf.buf(), readBuf.validData(),
                                        stream.get());
            if (last > 0) {
                readBuf.consume(last);
                progress = true;
            }

            m_lock->unlock();

        } while (progress);
    }

    if (dumpFP) {
        fclose(dumpFP);
    }

    //
    // Release references to the current thread's context/surfaces if any
    //
    FrameBuffer::getFB()->bindContext(0, 0, 0);
    if (tInfo.currContext || tInfo.currDrawSurf || tInfo.currReadSurf) {
        fprintf(stderr,
                "ERROR: RenderThread exiting with current context/surfaces\n");
    }

    FrameBuffer::getFB()->drainWindowSurface();

    FrameBuffer::getFB()->drainRenderContext();

    DBG("Exited a RenderThread\n");

    return 0;
}

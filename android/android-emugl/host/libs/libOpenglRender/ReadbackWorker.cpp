
#include "ReadbackWorker.h"

#include "ColorBuffer.h"
#include "DispatchTables.h"
#include "FrameBuffer.h"
#include "RenderThreadInfo.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"


ReadbackWorker::ReadbackWorker(uint32_t width, uint32_t height) :
    mFb(FrameBuffer::getFB()),
    mBufferSize(4 * width * height /* RGBA8 (4 bpp) */),
    mBuffers(3 /* mailbox */, 0) {
}

void ReadbackWorker::initGL() {
    mFb->createAndBindTrivialSharedContext(&mContext, &mSurf);
    s_gles2.glGenBuffers(mBuffers.size(), &mBuffers[0]);
    for (auto buffer : mBuffers) {
        s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        s_gles2.glBufferData(GL_PIXEL_PACK_BUFFER, mBufferSize,
                             0 /* init, with no data */,
                             GL_STREAM_READ);
    }
    s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

ReadbackWorker::~ReadbackWorker() {
    s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    s_gles2.glBindBuffer(GL_COPY_READ_BUFFER, 0);
    s_gles2.glDeleteBuffers(mBuffers.size(), &mBuffers[0]);
    mFb->unbindAndDestroyTrivialSharedContext(mContext, mSurf);
}

void ReadbackWorker::doNextReadback(ColorBuffer* cb, void* fbImage) {
    // Mailbox-style triple buffering setup:
    // We want to avoid glReadPixels while in the middle of doing
    // memcpy to the consumer, but also want to avoid latency while
    // that is going on.
    //
    // There are 3 buffer ids, A, B, and C.
    // If we are not in the middle of copying out a frame,
    // set glReadPixels to write to buffer A and copy from buffer B in
    // alternation, so the consumer always gets the latest frame
    // +1 frame of lag in order to not cause blocking on
    // glReadPixels / glMapBufferRange.
    // If we are in the middle of copying out a frame, reset A and B to
    // not be the buffer being copied out, and continue glReadPixels to
    // buffer A and B as before.
    //
    // The resulting invariants are:
    // - glReadPixels is called on a different buffer every time
    //   so we avoid introducing sync points there.
    // - At no time are we mapping/copying a buffer and also doing
    //   glReadPixels on it (avoid simultaneous map + glReadPixels)
    // - glReadPixels and then immediately map/copy the same buffer
    //   doesn't happen either (avoid sync point in glMapBufferRange)
    android::base::AutoLock lock(mLock);
    if (mIsCopying) {
        switch (mMapCopyIndex) {
        // To keep double buffering effect on
        // glReadPixels, need to keep even/oddness of
        // mReadPixelsIndexEven and mReadPixelsIndexOdd.
        case 0:
            mReadPixelsIndexEven = 2;
            mReadPixelsIndexOdd = 1;
            break;
        case 1:
            mReadPixelsIndexEven = 0;
            mReadPixelsIndexOdd = 2;
            break;
        case 2:
            mReadPixelsIndexEven = 0;
            mReadPixelsIndexOdd = 1;
            break;
        }
    } else {
        mReadPixelsIndexEven = 0;
        mReadPixelsIndexOdd = 1;
        mMapCopyIndex = mPrevReadPixelsIndex;
    }

    // Double buffering on buffer A / B part
    uint32_t readAt;
    if (m_readbackCount % 2 == 0) {
        readAt = mReadPixelsIndexEven;
    } else {
        readAt = mReadPixelsIndexOdd;
    }
    lock.unlock();

    cb->readbackAsync(mBuffers[readAt]);
    mFb->doPostCallback(fbImage);
    m_readbackCount++;

    mPrevReadPixelsIndex = readAt;
}

void ReadbackWorker::getPixels(void* buf, uint32_t bytes) {
    android::base::AutoLock lock(mLock);
    mIsCopying = true;
    lock.unlock();

    GLuint buffer = mBuffers[mMapCopyIndex];
    s_gles2.glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    void* pixels = s_gles2.glMapBufferRange(GL_COPY_READ_BUFFER, 0, bytes,
                                            GL_MAP_READ_BIT);
    memcpy(buf, pixels, bytes);
    s_gles2.glUnmapBuffer(GL_COPY_READ_BUFFER);

    lock.lock();
    mIsCopying = false;
    lock.unlock();
}

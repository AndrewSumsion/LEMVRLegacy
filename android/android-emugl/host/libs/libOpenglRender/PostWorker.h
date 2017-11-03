#pragma once

#include "android/base/Compiler.h"
#include "android/base/synchronization/Lock.h"

#include <EGL/egl.h>

#include <functional>
#include <vector>

class ColorBuffer;
class FrameBuffer;
class RenderThreadInfo;

class PostWorker {
public:
    using BindSubwinCallback = std::function<bool(void)>;

    PostWorker(BindSubwinCallback&& cb);
    ~PostWorker();

    // post: posts the next color buffer.
    // Assumes framebuffer lock is held.
    void post(ColorBuffer* cb);
    // viewport: (re)initializes viewport dimensions.
    // Assumes framebuffer lock is held.
    void viewport(int width, int height);

private:
    EGLContext mContext;
    EGLSurface mSurf;
    RenderThreadInfo* mTLS;
    FrameBuffer* mFb;

    std::function<bool(void)> mBindSubwin;

    bool m_initialized = false;
    int m_viewportWidth = 0;
    int m_viewportHeight = 0;
    DISALLOW_COPY_AND_ASSIGN(PostWorker);
};

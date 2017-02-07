/*
* Copyright (C) 2011-2015 The Android Open Source Project
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

#include "FrameBuffer.h"

#include "DispatchTables.h"
#include "NativeSubWindow.h"
#include "RenderThreadInfo.h"
#include "gles2_dec.h"

#include "OpenGLESDispatch/EGLDispatch.h"

#include "android/base/containers/Lookup.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/base/system/System.h"
#include "emugl/common/logging.h"

#include <stdio.h>
#include <string.h>

namespace {

// Helper class to call the bind_locked() / unbind_locked() properly.
class ScopedBind {
public:
    // Constructor will call bind_locked() on |fb|.
    // Use isValid() to check for errors.
    ScopedBind(FrameBuffer* fb) : mFb(fb) {
        if (!mFb->bind_locked()) {
            mFb = NULL;
        }
    }

    // Returns true if contruction bound the framebuffer context properly.
    bool isValid() const { return mFb != NULL; }

    // Unbound the framebuffer explictly. This is also called by the
    // destructor.
    void release() {
        if (mFb) {
            mFb->unbind_locked();
            mFb = NULL;
        }
    }

    // Destructor will call release().
    ~ScopedBind() { release(); }

private:
    FrameBuffer* mFb;
};

// Implementation of a ColorBuffer::Helper instance that redirects calls
// to a FrameBuffer instance.
class ColorBufferHelper : public ColorBuffer::Helper {
public:
    ColorBufferHelper(FrameBuffer* fb) : mFb(fb) {}

    virtual bool setupContext() { return mFb->bind_locked(); }

    virtual void teardownContext() { mFb->unbind_locked(); }

    virtual TextureDraw* getTextureDraw() const {
        return mFb->getTextureDraw();
    }

private:
    FrameBuffer* mFb;
};

}  // namespace

FrameBuffer* FrameBuffer::s_theFrameBuffer = NULL;
HandleType FrameBuffer::s_nextHandle = 0;

static char* getGLES2ExtensionString(EGLDisplay p_dpy) {
    EGLConfig config;
    EGLSurface surface;

    static const GLint configAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                          EGL_RENDERABLE_TYPE,
                                          EGL_OPENGL_ES2_BIT, EGL_NONE};

    int n;
    if (!s_egl.eglChooseConfig(p_dpy, configAttribs, &config, 1, &n) ||
        n == 0) {
        ERR("%s: Could not find GLES 2.x config!\n", __FUNCTION__);
        return NULL;
    }

    static const EGLint pbufAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

    surface = s_egl.eglCreatePbufferSurface(p_dpy, config, pbufAttribs);
    if (surface == EGL_NO_SURFACE) {
        ERR("%s: Could not create GLES 2.x Pbuffer!\n", __FUNCTION__);
        return NULL;
    }

    static const GLint gles2ContextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                                EGL_NONE};

    EGLContext ctx = s_egl.eglCreateContext(p_dpy, config, EGL_NO_CONTEXT,
                                            gles2ContextAttribs);
    if (ctx == EGL_NO_CONTEXT) {
        ERR("%s: Could not create GLES 2.x Context!\n", __FUNCTION__);
        s_egl.eglDestroySurface(p_dpy, surface);
        return NULL;
    }

    if (!s_egl.eglMakeCurrent(p_dpy, surface, surface, ctx)) {
        ERR("%s: Could not make GLES 2.x context current!\n", __FUNCTION__);
        s_egl.eglDestroySurface(p_dpy, surface);
        s_egl.eglDestroyContext(p_dpy, ctx);
        return NULL;
    }

    // the string pointer may become invalid when the context is destroyed
    const char* s = (const char*)s_gles2.glGetString(GL_EXTENSIONS);
    char* extString = strdup(s ? s : "");

    // It is rare but some drivers actually fail this...
    if (!s_egl.eglMakeCurrent(p_dpy, NULL, NULL, NULL)) {
        ERR("%s: Could not unbind context. Please try updating graphics card "
            "driver!\n",
            __FUNCTION__);
        free(extString);
        extString = NULL;
    }
    s_egl.eglDestroyContext(p_dpy, ctx);
    s_egl.eglDestroySurface(p_dpy, surface);

    return extString;
}

void FrameBuffer::finalize() {
    m_colorbuffers.clear();
    if (m_useSubWindow) {
        removeSubWindow_locked();
    }
    m_windows.clear();
    m_contexts.clear();
    if (m_eglDisplay != EGL_NO_DISPLAY) {
        s_egl.eglMakeCurrent(m_eglDisplay, NULL, NULL, NULL);
        if (m_eglContext != EGL_NO_CONTEXT) {
            s_egl.eglDestroyContext(m_eglDisplay, m_eglContext);
            m_eglContext = EGL_NO_CONTEXT;
        }
        if (m_pbufContext != EGL_NO_CONTEXT) {
            s_egl.eglDestroyContext(m_eglDisplay, m_pbufContext);
            m_pbufContext = EGL_NO_CONTEXT;
        }
        if (m_pbufSurface != EGL_NO_SURFACE) {
            s_egl.eglDestroySurface(m_eglDisplay, m_pbufSurface);
            m_pbufSurface = EGL_NO_SURFACE;
        }
        m_eglDisplay = EGL_NO_DISPLAY;
    }
}

bool FrameBuffer::initialize(int width, int height, bool useSubWindow) {
    GL_LOG("FrameBuffer::initialize");
    if (s_theFrameBuffer != NULL) {
        return true;
    }

    //
    // allocate space for the FrameBuffer object
    //
    std::unique_ptr<FrameBuffer> fb(
            new FrameBuffer(width, height, useSubWindow));
    if (!fb) {
        ERR("Failed to create fb\n");
        return false;
    }

    //
    // Initialize backend EGL display
    //
    fb->m_eglDisplay = s_egl.eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (fb->m_eglDisplay == EGL_NO_DISPLAY) {
        ERR("Failed to Initialize backend EGL display\n");
        return false;
    }

    GL_LOG("call eglInitialize");
    if (!s_egl.eglInitialize(fb->m_eglDisplay, &fb->m_caps.eglMajor,
                             &fb->m_caps.eglMinor)) {
        ERR("Failed to eglInitialize\n");
        GL_LOG("Failed to eglInitialize");
        return false;
    }

    DBG("egl: %d %d\n", fb->m_caps.eglMajor, fb->m_caps.eglMinor);
    GL_LOG("egl: %d %d", fb->m_caps.eglMajor, fb->m_caps.eglMinor);
    s_egl.eglBindAPI(EGL_OPENGL_ES_API);

    //
    // if GLES2 plugin has loaded - try to make GLES2 context and
    // get GLES2 extension string
    //
    android::base::ScopedCPtr<char> gles2Extensions(
            getGLES2ExtensionString(fb->m_eglDisplay));
    if (!gles2Extensions) {
        // Could not create GLES2 context - drop GL2 capability
        ERR("Failed to obtain GLES 2.x extensions string!\n");
        return false;
    }

    //
    // Create EGL context for framebuffer post rendering.
    //
    GLint surfaceType = (useSubWindow ? EGL_WINDOW_BIT : 0) | EGL_PBUFFER_BIT;

    // On Linux, we need RGB888 exactly, or eglMakeCurrent will fail,
    // as glXMakeContextCurrent needs to match the format of the
    // native pixmap.
    EGLint wantedRedSize = 8;
    EGLint wantedGreenSize = 8;
    EGLint wantedBlueSize = 8;

    const GLint configAttribs[] = {
            EGL_RED_SIZE,       wantedRedSize, EGL_GREEN_SIZE,
            wantedGreenSize,    EGL_BLUE_SIZE, wantedBlueSize,
            EGL_SURFACE_TYPE,   surfaceType,   EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES2_BIT, EGL_NONE};

    EGLint total_num_configs = 0;
    s_egl.eglGetConfigs(fb->m_eglDisplay, NULL, 0, &total_num_configs);

    std::vector<EGLConfig> all_configs(total_num_configs);
    EGLint total_egl_compatible_configs = 0;
    s_egl.eglChooseConfig(fb->m_eglDisplay, configAttribs, &all_configs[0],
                          total_num_configs, &total_egl_compatible_configs);

    EGLint exact_match_index = -1;
    for (EGLint i = 0; i < total_egl_compatible_configs; i++) {
        EGLint r, g, b;
        EGLConfig c = all_configs[i];
        s_egl.eglGetConfigAttrib(fb->m_eglDisplay, c, EGL_RED_SIZE, &r);
        s_egl.eglGetConfigAttrib(fb->m_eglDisplay, c, EGL_GREEN_SIZE, &g);
        s_egl.eglGetConfigAttrib(fb->m_eglDisplay, c, EGL_BLUE_SIZE, &b);

        if (r == wantedRedSize && g == wantedGreenSize && b == wantedBlueSize) {
            exact_match_index = i;
            break;
        }
    }

    if (exact_match_index < 0) {
        ERR("Failed on eglChooseConfig\n");
        return false;
    }

    fb->m_eglConfig = all_configs[exact_match_index];

    static const GLint glContextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                             EGL_NONE};

    GL_LOG("attempting to create egl context");
    fb->m_eglContext = s_egl.eglCreateContext(fb->m_eglDisplay, fb->m_eglConfig,
                                              EGL_NO_CONTEXT, glContextAttribs);
    if (fb->m_eglContext == EGL_NO_CONTEXT) {
        ERR("Failed to create context 0x%x\n", s_egl.eglGetError());
        return false;
    }

    GL_LOG("attempting to create egl pbuffer context");
    //
    // Create another context which shares with the eglContext to be used
    // when we bind the pbuffer. That prevent switching drawable binding
    // back and forth on framebuffer context.
    // The main purpose of it is to solve a "blanking" behaviour we see on
    // on Mac platform when switching binded drawable for a context however
    // it is more efficient on other platforms as well.
    //
    fb->m_pbufContext =
            s_egl.eglCreateContext(fb->m_eglDisplay, fb->m_eglConfig,
                                   fb->m_eglContext, glContextAttribs);
    if (fb->m_pbufContext == EGL_NO_CONTEXT) {
        ERR("Failed to create Pbuffer Context 0x%x\n", s_egl.eglGetError());
        return false;
    }

    GL_LOG("context creation successful");
    //
    // create a 1x1 pbuffer surface which will be used for binding
    // the FB context.
    // The FB output will go to a subwindow, if one exist.
    //
    static const EGLint pbufAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

    fb->m_pbufSurface = s_egl.eglCreatePbufferSurface(
            fb->m_eglDisplay, fb->m_eglConfig, pbufAttribs);
    if (fb->m_pbufSurface == EGL_NO_SURFACE) {
        ERR("Failed to create pbuf surface for FB 0x%x\n", s_egl.eglGetError());
        return false;
    }

    GL_LOG("attempting to make context current");
    // Make the context current
    ScopedBind bind(fb.get());
    if (!bind.isValid()) {
        ERR("Failed to make current\n");
        return false;
    }
    GL_LOG("context-current successful");

    //
    // Initilize framebuffer capabilities
    //
    const bool has_gl_oes_image =
            strstr(gles2Extensions.get(), "GL_OES_EGL_image") != NULL;
    gles2Extensions.reset();

    fb->m_caps.has_eglimage_texture_2d = false;
    fb->m_caps.has_eglimage_renderbuffer = false;
    if (has_gl_oes_image) {
        const char* const eglExtensions =
                s_egl.eglQueryString(fb->m_eglDisplay, EGL_EXTENSIONS);
        if (eglExtensions != nullptr) {
            fb->m_caps.has_eglimage_texture_2d =
                    strstr(eglExtensions, "EGL_KHR_gl_texture_2D_image") !=
                    NULL;
            fb->m_caps.has_eglimage_renderbuffer =
                    strstr(eglExtensions, "EGL_KHR_gl_renderbuffer_image") !=
                    NULL;
        }
    }

    //
    // Fail initialization if not all of the following extensions
    // exist:
    //     EGL_KHR_gl_texture_2d_image
    //     GL_OES_EGL_IMAGE (by both GLES implementations [1 and 2])
    //
    if (!fb->m_caps.has_eglimage_texture_2d) {
        ERR("Failed: Missing egl_image related extension(s)\n");
        return false;
    }

    GL_LOG("host system has enough extensions");
    //
    // Initialize set of configs
    //
    fb->m_configs = new FbConfigList(fb->m_eglDisplay);
    if (fb->m_configs->empty()) {
        ERR("Failed: Initialize set of configs\n");
        return false;
    }

    //
    // Check that we have config for each GLES and GLES2
    //
    size_t nConfigs = fb->m_configs->size();
    int nGLConfigs = 0;
    int nGL2Configs = 0;
    for (size_t i = 0; i < nConfigs; ++i) {
        GLint rtype = fb->m_configs->get(i)->getRenderableType();
        if (0 != (rtype & EGL_OPENGL_ES_BIT)) {
            nGLConfigs++;
        }
        if (0 != (rtype & EGL_OPENGL_ES2_BIT)) {
            nGL2Configs++;
        }
    }

    //
    // Don't fail initialization if no GLES configs exist
    //

    //
    // If no configs at all, exit
    //
    if (nGLConfigs + nGL2Configs == 0) {
        ERR("Failed: No GLES 2.x configs found!\n");
        return false;
    }

    GL_LOG("There are sufficient EGLconfigs available");

    //
    // Cache the GL strings so we don't have to think about threading or
    // current-context when asked for them.
    //
    fb->m_glVendor = (const char*)s_gles2.glGetString(GL_VENDOR);
    fb->m_glRenderer = (const char*)s_gles2.glGetString(GL_RENDERER);
    fb->m_glVersion = (const char*)s_gles2.glGetString(GL_VERSION);

    fb->m_textureDraw = new TextureDraw();
    if (!fb->m_textureDraw) {
        ERR("Failed: creation of TextureDraw instance\n");
        return false;
    }

    //
    // Keep the singleton framebuffer pointer
    //
    s_theFrameBuffer = fb.release();
    GL_LOG("basic EGL initialization successful");
    return true;
}

FrameBuffer::FrameBuffer(int p_width, int p_height, bool useSubWindow)
    : m_framebufferWidth(p_width),
      m_framebufferHeight(p_height),
      m_windowWidth(p_width),
      m_windowHeight(p_height),
      m_useSubWindow(useSubWindow),
      m_fpsStats(getenv("SHOW_FPS_STATS") != nullptr),
      m_colorBufferHelper(new ColorBufferHelper(this)) {}

FrameBuffer::~FrameBuffer() {
    finalize();

    delete m_textureDraw;
    delete m_configs;
    delete m_colorBufferHelper;
    free(m_fbImage);
}

void FrameBuffer::setPostCallback(emugl::Renderer::OnPostCallback onPost,
                                  void* onPostContext) {
    emugl::Mutex::AutoLock mutex(m_lock);
    m_onPost = onPost;
    m_onPostContext = onPostContext;
    if (m_onPost && !m_fbImage) {
        m_fbImage = (unsigned char*)malloc(4 * m_framebufferWidth *
                                           m_framebufferHeight);
        if (!m_fbImage) {
            ERR("out of memory, cancelling OnPost callback");
            m_onPost = NULL;
            m_onPostContext = NULL;
            return;
        }
    }
}

static void subWindowRepaint(void* param) {
    auto fb = static_cast<FrameBuffer*>(param);
    fb->repost();
}

bool FrameBuffer::setupSubWindow(FBNativeWindowType p_window,
                                 int wx,
                                 int wy,
                                 int ww,
                                 int wh,
                                 int fbw,
                                 int fbh,
                                 float dpr,
                                 float zRot) {
    bool success = false;

    if (!m_useSubWindow) {
        ERR("%s: Cannot create native sub-window in this configuration\n",
            __FUNCTION__);
        return false;
    }

    emugl::Mutex::AutoLock mutex(m_lock);

    // If the subwindow doesn't exist, create it with the appropriate dimensions
    if (!m_subWin) {
        // Create native subwindow for FB display output
        m_x = wx;
        m_y = wy;
        m_windowWidth = ww;
        m_windowHeight = wh;

        m_subWin = createSubWindow(p_window, m_x, m_y, m_windowWidth,
                                   m_windowHeight, subWindowRepaint, this);
        if (m_subWin) {
            m_nativeWindow = p_window;

            // create EGLSurface from the generated subwindow
            m_eglSurface = s_egl.eglCreateWindowSurface(
                    m_eglDisplay, m_eglConfig, m_subWin, NULL);

            if (m_eglSurface == EGL_NO_SURFACE) {
                // NOTE: This can typically happen with software-only renderers
                // like OSMesa.
                destroySubWindow(m_subWin);
                m_subWin = (EGLNativeWindowType)0;
            } else {
                m_px = 0;
                m_py = 0;

                success = true;
            }
        }
    }

    // At this point, if the subwindow doesn't exist, it is because it either
    // couldn't be created
    // in the first place or the EGLSurface couldn't be created.
    if (m_subWin && bindSubwin_locked()) {
        // Only attempt to update window geometry if anything has actually
        // changed.
        bool updateSubWindow = !(m_x == wx && m_y == wy &&
                                 m_windowWidth == ww && m_windowHeight == wh);

// On Mac, since window coordinates are Y-up and not Y-down, the
// subwindow may not change dimensions, but because the main window
// did, the subwindow technically needs to be re-positioned. This
// can happen on rotation, so a change in Z-rotation can be checked
// for this case. However, this *should not* be done on Windows/Linux,
// because the functions used to resize a native window on those hosts
// will block if the shape doesn't actually change, freezing the
// emulator.
#if defined(__APPLE__)
        updateSubWindow |= (m_zRot != zRot);
#endif
        if (updateSubWindow) {
            m_x = wx;
            m_y = wy;
            m_windowWidth = ww;
            m_windowHeight = wh;

            success = ::moveSubWindow(m_nativeWindow, m_subWin, m_x, m_y,
                                      m_windowWidth, m_windowHeight);

            // Otherwise, ensure that at least viewport parameters are properly
            // updated.
        } else {
            success = true;
        }

        if (success) {
            // Subwin creation or movement was successful,
            // update viewport and z rotation and draw
            // the last posted color buffer.
            s_gles2.glViewport(0, 0, fbw * dpr, fbh * dpr);
            m_dpr = dpr;
            m_zRot = zRot;
            if (m_lastPostedColorBuffer) {
                post(m_lastPostedColorBuffer, false);
            } else {
                s_gles2.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                                GL_STENCIL_BUFFER_BIT);
                s_egl.eglSwapBuffers(m_eglDisplay, m_eglSurface);
            }
        }
        unbind_locked();
    }

    return success;
}

bool FrameBuffer::removeSubWindow() {
    if (!m_useSubWindow) {
        ERR("%s: Cannot remove native sub-window in this configuration\n",
            __FUNCTION__);
        return false;
    }
    emugl::Mutex::AutoLock mutex(m_lock);
    return removeSubWindow_locked();
}

bool FrameBuffer::removeSubWindow_locked() {
    if (!m_useSubWindow) {
        ERR("%s: Cannot remove native sub-window in this configuration\n",
            __FUNCTION__);
        return false;
    }
    bool removed = false;
    if (m_subWin) {
        s_egl.eglMakeCurrent(m_eglDisplay, NULL, NULL, NULL);
        s_egl.eglDestroySurface(m_eglDisplay, m_eglSurface);
        destroySubWindow(m_subWin);

        m_eglSurface = EGL_NO_SURFACE;
        m_subWin = (EGLNativeWindowType)0;
        removed = true;
    }
    return removed;
}

HandleType FrameBuffer::genHandle() {
    HandleType id;
    do {
        id = ++s_nextHandle;
    } while (id == 0 || m_contexts.find(id) != m_contexts.end() ||
             m_windows.find(id) != m_windows.end());

    return id;
}

HandleType FrameBuffer::createColorBuffer(int p_width,
                                          int p_height,
                                          GLenum p_internalFormat,
                                          FrameworkFormat p_frameworkFormat) {
    emugl::Mutex::AutoLock mutex(m_lock);
    HandleType ret = 0;

    ColorBufferPtr cb(ColorBuffer::create(getDisplay(), p_width, p_height,
                                          p_internalFormat, p_frameworkFormat,
                                          getCaps().has_eglimage_texture_2d,
                                          m_colorBufferHelper));
    if (cb.get() != NULL) {
        ret = genHandle();
        m_colorbuffers[ret].cb = cb;
        m_colorbuffers[ret].refcount = 1;

        RenderThreadInfo* tInfo = RenderThreadInfo::get();
        uint64_t puid = tInfo->m_puid;
        if (puid) {
            m_procOwnedColorBuffers[puid].insert(ret);
        }
    }
    return ret;
}

HandleType FrameBuffer::createRenderContext(int p_config,
                                            HandleType p_share,
                                            GLESApi version) {
    emugl::Mutex::AutoLock mutex(m_lock);
    emugl::ReadWriteMutex::AutoWriteLock contextLock(m_contextStructureLock);
    HandleType ret = 0;

    const FbConfig* config = getConfigs()->get(p_config);
    if (!config) {
        return ret;
    }

    RenderContextPtr share;
    if (p_share != 0) {
        RenderContextMap::iterator s(m_contexts.find(p_share));
        if (s == m_contexts.end()) {
            return ret;
        }
        share = (*s).second;
    }
    EGLContext sharedContext =
            share.get() ? share->getEGLContext() : EGL_NO_CONTEXT;

    ret = genHandle();
    RenderContextPtr rctx(RenderContext::create(
            m_eglDisplay, config->getEglConfig(), sharedContext, ret, version));
    if (rctx.get() != NULL) {
        m_contexts[ret] = rctx;
        RenderThreadInfo* tinfo = RenderThreadInfo::get();
        uint64_t puid = tinfo->m_puid;
        // The new emulator manages render contexts per guest process.
        // Fall back to per-thread management if the system image does not
        // support it.
        if (puid) {
            m_procOwnedRenderContext[puid].insert(ret);
        } else {
            tinfo->m_contextSet.insert(ret);
        }
    } else {
        ret = 0;
    }

    return ret;
}

HandleType FrameBuffer::createWindowSurface(int p_config,
                                            int p_width,
                                            int p_height) {
    emugl::Mutex::AutoLock mutex(m_lock);

    HandleType ret = 0;

    const FbConfig* config = getConfigs()->get(p_config);
    if (!config) {
        return ret;
    }

    WindowSurfacePtr win(WindowSurface::create(
            getDisplay(), config->getEglConfig(), p_width, p_height));
    if (win.get() != NULL) {
        ret = genHandle();
        m_windows[ret] = std::pair<WindowSurfacePtr, HandleType>(win, 0);
        RenderThreadInfo* tinfo = RenderThreadInfo::get();
        tinfo->m_windowSet.insert(ret);
    }

    return ret;
}

void FrameBuffer::drainRenderContext() {
    emugl::Mutex::AutoLock mutex(m_lock);
    emugl::ReadWriteMutex::AutoWriteLock contextLock(m_contextStructureLock);
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    if (tinfo->m_contextSet.empty())
        return;
    for (std::set<HandleType>::iterator it = tinfo->m_contextSet.begin();
         it != tinfo->m_contextSet.end(); ++it) {
        HandleType contextHandle = *it;
        m_contexts.erase(contextHandle);
    }
    tinfo->m_contextSet.clear();
}

void FrameBuffer::drainWindowSurface() {
    emugl::Mutex::AutoLock mutex(m_lock);
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    if (tinfo->m_windowSet.empty())
        return;
    for (std::set<HandleType>::iterator it = tinfo->m_windowSet.begin();
         it != tinfo->m_windowSet.end(); ++it) {
        HandleType windowHandle = *it;
        if (m_windows.find(windowHandle) != m_windows.end()) {
            HandleType oldColorBufferHandle = m_windows[windowHandle].second;
            if (oldColorBufferHandle) {
                ColorBufferMap::iterator cit(
                        m_colorbuffers.find(oldColorBufferHandle));
                if (cit != m_colorbuffers.end()) {
                    if (--(*cit).second.refcount == 0) {
                        m_colorbuffers.erase(cit);
                    }
                }
            }
            m_windows.erase(windowHandle);
        }
    }
    tinfo->m_windowSet.clear();
}

void FrameBuffer::DestroyRenderContext(HandleType p_context) {
    emugl::Mutex::AutoLock mutex(m_lock);
    emugl::ReadWriteMutex::AutoWriteLock contextLock(m_contextStructureLock);
    m_contexts.erase(p_context);
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    uint64_t puid = tinfo->m_puid;
    // The new emulator manages render contexts per guest process.
    // Fall back to per-thread management if the system image does not
    // support it.
    if (puid) {
        auto ite = m_procOwnedRenderContext.find(puid);
        if (ite != m_procOwnedRenderContext.end()) {
            ite->second.erase(p_context);
        }
    } else {
        tinfo->m_contextSet.erase(p_context);
    }
}

void FrameBuffer::DestroyWindowSurface(HandleType p_surface) {
    emugl::Mutex::AutoLock mutex(m_lock);
    if (m_windows.erase(p_surface) != 0) {
        RenderThreadInfo* tinfo = RenderThreadInfo::get();
        tinfo->m_windowSet.erase(p_surface);
    }
}

int FrameBuffer::openColorBuffer(HandleType p_colorbuffer) {
    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    emugl::Mutex::AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        ERR("FB: openColorBuffer cb handle %#x not found\n", p_colorbuffer);
        return -1;
    }
    (*c).second.refcount++;

    uint64_t puid = tInfo->m_puid;
    if (puid) {
        m_procOwnedColorBuffers[puid].insert(p_colorbuffer);
    }
    return 0;
}

void FrameBuffer::closeColorBuffer(HandleType p_colorbuffer) {
    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    emugl::Mutex::AutoLock mutex(m_lock);
    closeColorBufferLocked(p_colorbuffer);
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        auto ite = m_procOwnedColorBuffers.find(puid);
        if (ite != m_procOwnedColorBuffers.end()) {
            ite->second.erase(p_colorbuffer);
        }
    }
}

void FrameBuffer::closeColorBufferLocked(HandleType p_colorbuffer) {
    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // This is harmless: it is normal for guest system to issue
        // closeColorBuffer command when the color buffer is already
        // garbage collected on the host. (we dont have a mechanism
        // to give guest a notice yet)
        return;
    }
    if (--(*c).second.refcount == 0) {
        m_colorbuffers.erase(c);
    }
}

void FrameBuffer::cleanupProcGLObjects(uint64_t puid) {
    emugl::Mutex::AutoLock mutex(m_lock);
    // Clean up color buffers.
    // A color buffer needs to be closed as many times as it is opened by
    // the guest process, to give the correct reference count.
    // (Note that a color buffer can be shared across guest processes.)
    {
        auto procIte = m_procOwnedColorBuffers.find(puid);
        if (procIte != m_procOwnedColorBuffers.end()) {
            for (auto cb : procIte->second) {
                closeColorBufferLocked(cb);
            }
            m_procOwnedColorBuffers.erase(procIte);
        }
    }

    // Clean up EGLImage handles
    {
        auto procIte = m_procOwnedEGLImages.find(puid);
        if (procIte != m_procOwnedEGLImages.end()) {
            if (!procIte->second.empty()) {
                // Bind context before potentially triggering any gl calls.
                ScopedBind bind(this);
                for (auto eglImg : procIte->second) {
                    s_egl.eglDestroyImageKHR(
                            m_eglDisplay,
                            reinterpret_cast<EGLImageKHR>((HandleType)eglImg));
                }
            }
            m_procOwnedEGLImages.erase(procIte);
        }
    }

    // Cleanup render contexts
    {
        auto procIte = m_procOwnedRenderContext.find(puid);
        if (procIte != m_procOwnedRenderContext.end()) {
            for (auto ctx : procIte->second) {
                m_contexts.erase(ctx);
            }
            m_procOwnedRenderContext.erase(procIte);
        }
    }
}

bool FrameBuffer::flushWindowSurfaceColorBuffer(HandleType p_surface) {
    emugl::Mutex::AutoLock mutex(m_lock);

    WindowSurfaceMap::iterator w(m_windows.find(p_surface));
    if (w == m_windows.end()) {
        ERR("FB::flushWindowSurfaceColorBuffer: window handle %#x not found\n",
            p_surface);
        // bad surface handle
        return false;
    }

    WindowSurface* surface = (*w).second.first.get();
    surface->flushColorBuffer();

    return true;
}

bool FrameBuffer::setWindowSurfaceColorBuffer(HandleType p_surface,
                                              HandleType p_colorbuffer) {
    emugl::Mutex::AutoLock mutex(m_lock);

    WindowSurfaceMap::iterator w(m_windows.find(p_surface));
    if (w == m_windows.end()) {
        // bad surface handle
        ERR("%s: bad window surface handle %#x\n", __FUNCTION__, p_surface);
        return false;
    }

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        DBG("%s: bad color buffer handle %#x\n", __FUNCTION__, p_colorbuffer);
        // bad colorbuffer handle
        return false;
    }

    (*w).second.first->setColorBuffer((*c).second.cb);
    (*w).second.second = p_colorbuffer;
    return true;
}

void FrameBuffer::readColorBuffer(HandleType p_colorbuffer,
                                  int x,
                                  int y,
                                  int width,
                                  int height,
                                  GLenum format,
                                  GLenum type,
                                  void* pixels) {
    emugl::Mutex::AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return;
    }

    (*c).second.cb->readPixels(x, y, width, height, format, type, pixels);
}

bool FrameBuffer::updateColorBuffer(HandleType p_colorbuffer,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    GLenum format,
                                    GLenum type,
                                    void* pixels) {
    emugl::Mutex::AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    (*c).second.cb->subUpdate(x, y, width, height, format, type, pixels);

    return true;
}

bool FrameBuffer::bindColorBufferToTexture(HandleType p_colorbuffer) {
    emugl::Mutex::AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    return (*c).second.cb->bindToTexture();
}

bool FrameBuffer::bindColorBufferToRenderbuffer(HandleType p_colorbuffer) {
    emugl::Mutex::AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    return (*c).second.cb->bindToRenderbuffer();
}

bool FrameBuffer::bindContext(HandleType p_context,
                              HandleType p_drawSurface,
                              HandleType p_readSurface) {
    emugl::Mutex::AutoLock mutex(m_lock);

    WindowSurfacePtr draw, read;
    RenderContextPtr ctx;

    //
    // if this is not an unbind operation - make sure all handles are good
    //
    if (p_context || p_drawSurface || p_readSurface) {
        ctx = getContext(p_context);
        if (!ctx)
            return false;
        WindowSurfaceMap::iterator w(m_windows.find(p_drawSurface));
        if (w == m_windows.end()) {
            // bad surface handle
            return false;
        }
        draw = (*w).second.first;

        if (p_readSurface != p_drawSurface) {
            WindowSurfaceMap::iterator w(m_windows.find(p_readSurface));
            if (w == m_windows.end()) {
                // bad surface handle
                return false;
            }
            read = (*w).second.first;
        } else {
            read = draw;
        }
    }

    if (!s_egl.eglMakeCurrent(m_eglDisplay,
                              draw ? draw->getEGLSurface() : EGL_NO_SURFACE,
                              read ? read->getEGLSurface() : EGL_NO_SURFACE,
                              ctx ? ctx->getEGLContext() : EGL_NO_CONTEXT)) {
        ERR("eglMakeCurrent failed\n");
        return false;
    }

    if (ctx) {
        if (ctx.get()->getEmulatedGLES1Context()) {
            DBG("%s: found emulated gles1 context @ %p\n", __FUNCTION__,
                ctx.get()->getEmulatedGLES1Context());
            s_gles1.set_current_gles_context(
                    ctx.get()->getEmulatedGLES1Context());
            DBG("%s: set emulated gles1 context current in thread info\n",
                __FUNCTION__);

            if (draw.get() == NULL) {
                DBG("%s: setup make current (null draw surface)\n",
                    __FUNCTION__);
                s_gles1.make_current_setup(0, 0);
            } else {
                DBG("%s: setup make current (draw surface %ux%u)\n",
                    __FUNCTION__, draw->getWidth(), draw->getHeight());
                s_gles1.make_current_setup(draw->getWidth(), draw->getHeight());
            }
            DBG("%s: set up the emulated gles1 context's info\n", __FUNCTION__);
        }
    }

    //
    // Bind the surface(s) to the context
    //
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    WindowSurfacePtr bindDraw, bindRead;
    if (draw.get() == NULL && read.get() == NULL) {
        // Unbind the current read and draw surfaces from the context
        bindDraw = tinfo->currDrawSurf;
        bindRead = tinfo->currReadSurf;
    } else {
        bindDraw = draw;
        bindRead = read;
    }

    if (bindDraw.get() != NULL && bindRead.get() != NULL) {
        if (bindDraw.get() != bindRead.get()) {
            bindDraw->bind(ctx, WindowSurface::BIND_DRAW);
            bindRead->bind(ctx, WindowSurface::BIND_READ);
        } else {
            bindDraw->bind(ctx, WindowSurface::BIND_READDRAW);
        }
    }

    //
    // update thread info with current bound context
    //
    tinfo->currContext = ctx;
    tinfo->currDrawSurf = draw;
    tinfo->currReadSurf = read;
    if (ctx) {
        if (ctx->version() > GLESApi_CM)
            tinfo->m_gl2Dec.setContextData(&ctx->decoderContextData());
        else
            tinfo->m_glDec.setContextData(&ctx->decoderContextData());
    } else {
        tinfo->m_glDec.setContextData(NULL);
        tinfo->m_gl2Dec.setContextData(NULL);
    }
    return true;
}

RenderContextPtr FrameBuffer::getContext(HandleType p_context) {
    return android::base::findOrDefault(m_contexts, p_context,
                                        RenderContextPtr());
}

HandleType FrameBuffer::createClientImage(HandleType context,
                                          EGLenum target,
                                          GLuint buffer) {
    RenderContextPtr ctx;

    if (context) {
        RenderContextMap::iterator r(m_contexts.find(context));
        if (r == m_contexts.end()) {
            // bad context handle
            return false;
        }

        ctx = (*r).second;
    }

    EGLContext eglContext = ctx ? ctx->getEGLContext() : EGL_NO_CONTEXT;
    EGLImageKHR image = s_egl.eglCreateImageKHR(
            m_eglDisplay, eglContext, target,
            reinterpret_cast<EGLClientBuffer>(buffer), NULL);
    HandleType imgHnd = (HandleType) reinterpret_cast<uintptr_t>(image);

    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        emugl::Mutex::AutoLock mutex(m_lock);
        m_procOwnedEGLImages[puid].insert(imgHnd);
    }
    return imgHnd;
}

EGLBoolean FrameBuffer::destroyClientImage(HandleType image) {
    // eglDestroyImageKHR has its own lock  already.
    EGLBoolean ret = s_egl.eglDestroyImageKHR(
            m_eglDisplay, reinterpret_cast<EGLImageKHR>(image));
    if (!ret)
        return false;
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        emugl::Mutex::AutoLock mutex(m_lock);
        m_procOwnedEGLImages[puid].erase(image);
        // We don't explicitly call m_procOwnedEGLImages.erase(puid) when the
        // size
        // reaches 0, since it could go between zero and one many times in the
        // lifetime of a process.
        // It will be cleaned up by cleanupProcGLObjects(puid) when the process
        // is
        // dead.
    }
    return true;
}

//
// The framebuffer lock should be held when calling this function !
//
bool FrameBuffer::bind_locked() {
    EGLContext prevContext = s_egl.eglGetCurrentContext();
    EGLSurface prevReadSurf = s_egl.eglGetCurrentSurface(EGL_READ);
    EGLSurface prevDrawSurf = s_egl.eglGetCurrentSurface(EGL_DRAW);

    if (prevContext != m_pbufContext || prevReadSurf != m_pbufSurface ||
        prevDrawSurf != m_pbufSurface) {
        if (!s_egl.eglMakeCurrent(m_eglDisplay, m_pbufSurface, m_pbufSurface,
                                  m_pbufContext)) {
            if (!m_shuttingDown)
                ERR("eglMakeCurrent failed\n");
            return false;
        }
    } else {
        ERR("Nested %s call detected, should never happen\n", __func__);
    }

    m_prevContext = prevContext;
    m_prevReadSurf = prevReadSurf;
    m_prevDrawSurf = prevDrawSurf;
    return true;
}

bool FrameBuffer::bindSubwin_locked() {
    EGLContext prevContext = s_egl.eglGetCurrentContext();
    EGLSurface prevReadSurf = s_egl.eglGetCurrentSurface(EGL_READ);
    EGLSurface prevDrawSurf = s_egl.eglGetCurrentSurface(EGL_DRAW);

    if (prevContext != m_eglContext || prevReadSurf != m_eglSurface ||
        prevDrawSurf != m_eglSurface) {
        if (!s_egl.eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface,
                                  m_eglContext)) {
            ERR("eglMakeCurrent failed\n");
            return false;
        }
    } else {
        ERR("Nested %s call detected, should never happen\n", __func__);
    }

    //
    // initialize GL state in eglContext if not yet initilaized
    //
    if (!m_eglContextInitialized) {
        m_eglContextInitialized = true;
    }

    m_prevContext = prevContext;
    m_prevReadSurf = prevReadSurf;
    m_prevDrawSurf = prevDrawSurf;
    return true;
}

bool FrameBuffer::unbind_locked() {
    EGLContext curContext = s_egl.eglGetCurrentContext();
    EGLSurface curReadSurf = s_egl.eglGetCurrentSurface(EGL_READ);
    EGLSurface curDrawSurf = s_egl.eglGetCurrentSurface(EGL_DRAW);

    if (m_prevContext != curContext || m_prevReadSurf != curReadSurf ||
        m_prevDrawSurf != curDrawSurf) {
        if (!s_egl.eglMakeCurrent(m_eglDisplay, m_prevDrawSurf, m_prevReadSurf,
                                  m_prevContext)) {
            return false;
        }
    }

    m_prevContext = EGL_NO_CONTEXT;
    m_prevReadSurf = EGL_NO_SURFACE;
    m_prevDrawSurf = EGL_NO_SURFACE;
    return true;
}

void FrameBuffer::createTrivialContext(HandleType shared,
                                       HandleType* contextOut,
                                       HandleType* surfOut) {
    assert(contextOut);
    assert(surfOut);

    *contextOut = createRenderContext(0, shared, GLESApi_2);
    // Zero size is formally allowed here, but SwiftShader doesn't like it and
    // fails.
    *surfOut = createWindowSurface(0, 1, 1);
}

bool FrameBuffer::post(HandleType p_colorbuffer, bool needLockAndBind) {
    if (needLockAndBind) {
        m_lock.lock();
    }
    bool ret = false;

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        goto EXIT;
    }

    m_lastPostedColorBuffer = p_colorbuffer;

    if (m_subWin) {
        // bind the subwindow eglSurface
        if (needLockAndBind && !bindSubwin_locked()) {
            ERR("FrameBuffer::post(): eglMakeCurrent failed\n");
            goto EXIT;
        }

        // get the viewport
        GLint vp[4];
        s_gles2.glGetIntegerv(GL_VIEWPORT, vp);

        // divide by device pixel ratio because windowing coordinates ignore
        // DPR,
        // but the framebuffer includes DPR
        vp[2] = vp[2] / m_dpr;
        vp[3] = vp[3] / m_dpr;

        // find the x and y values at the origin when "fully scrolled"
        // multiply by 2 because the texture goes from -1 to 1, not 0 to 1
        float fx = 2.f * (vp[2] - m_windowWidth) / (float)vp[2];
        float fy = 2.f * (vp[3] - m_windowHeight) / (float)vp[3];

        // finally, compute translation values
        float dx = m_px * fx;
        float dy = m_py * fy;

        //
        // render the color buffer to the window
        //
        ret = (*c).second.cb->post(m_zRot, dx, dy);
        if (ret) {
            s_egl.eglSwapBuffers(m_eglDisplay, m_eglSurface);
        }

        // restore previous binding
        if (needLockAndBind) {
            unbind_locked();
        }
    } else {
        // If there is no sub-window, don't display anything, the client will
        // rely on m_onPost to get the pixels instead.
        ret = true;
    }

    //
    // output FPS statistics
    //
    if (m_fpsStats) {
        long long currTime =
                android::base::System::get()->getHighResTimeUs() / 1000;
        m_statsNumFrames++;
        if (currTime - m_statsStartTime >= 1000) {
            float dt = (float)(currTime - m_statsStartTime) / 1000.0f;
            printf("FPS: %5.3f\n", (float)m_statsNumFrames / dt);
            m_statsStartTime = currTime;
            m_statsNumFrames = 0;
        }
    }

    //
    // Send framebuffer (without FPS overlay) to callback
    //
    if (m_onPost) {
        (*c).second.cb->readback(m_fbImage);
        m_onPost(m_onPostContext, m_framebufferWidth, m_framebufferHeight, -1,
                 GL_RGBA, GL_UNSIGNED_BYTE, m_fbImage);
    }

EXIT:
    if (needLockAndBind) {
        m_lock.unlock();
    }
    return ret;
}

bool FrameBuffer::repost() {
    if (m_lastPostedColorBuffer) {
        return post(m_lastPostedColorBuffer);
    }
    return false;
}

void FrameBuffer::onSave(android::base::Stream* stream) {
    emugl::Mutex::AutoLock mutex(m_lock);
    stream->putBe32(m_x);
    stream->putBe32(m_y);
    stream->putBe32(m_framebufferWidth);
    stream->putBe32(m_framebufferHeight);
    stream->putBe32(m_windowWidth);
    stream->putBe32(m_windowHeight);
    stream->putFloat(m_dpr);

    stream->putBe32(m_useSubWindow);
    stream->putBe32(m_eglContextInitialized);

    stream->putBe32(m_fpsStats);
    stream->putBe32(m_statsNumFrames);
    stream->putBe64(m_statsStartTime);

    // snapshot contexts
    stream->putBe32(m_contexts.size());
    for (const auto& ctx : m_contexts) {
        ctx.second->onSave(stream);
    }

    // TODO: snapshot color buffers and window surfaces
    stream->putBe32(m_colorbuffers.size());
    for (const auto& cb : m_colorbuffers) {
        stream->putBe32(cb.first);
        cb.second.cb->onSave(stream);
    }
    // TODO: snapshot memory management
}

bool FrameBuffer::onLoad(android::base::Stream* stream) {
    emugl::Mutex::AutoLock mutex(m_lock);
    m_x = stream->getBe32();
    m_y = stream->getBe32();
    m_framebufferWidth = stream->getBe32();
    m_framebufferHeight = stream->getBe32();
    m_windowWidth = stream->getBe32();
    m_windowHeight = stream->getBe32();
    m_dpr = stream->getFloat();
    // TODO: resize the window

    m_useSubWindow = stream->getBe32();
    m_eglContextInitialized = stream->getBe32();

    m_fpsStats = stream->getBe32();
    m_statsNumFrames = stream->getBe32();
    m_statsStartTime = stream->getBe64();

    // restore contexts
    m_contexts.clear();
    size_t numContexts = stream->getBe32();
    for (size_t i = 0; i < numContexts; i++) {
        RenderContextPtr ctx(RenderContext::onLoad(stream, m_eglDisplay));
        if (ctx) {
            m_contexts[ctx->getHndl()] = ctx;
        }
    }
    m_windows.clear();
    m_colorbuffers.clear();
    // TODO: restore color buffers and window surfaces
    size_t numColorBuffers = stream->getBe32();
    for (size_t i = 0; i < numColorBuffers; i++) {
        HandleType hndl = stream->getBe32();
        ColorBufferPtr cb(ColorBuffer::onLoad(stream, m_eglDisplay,
                                              getCaps().has_eglimage_texture_2d,
                                              m_colorBufferHelper));
        if (cb.get() != NULL) {
            m_colorbuffers[hndl].cb = cb;
            m_colorbuffers[hndl].refcount = 1;
        }
    }
    // TODO: restore memory management
    return true;
}

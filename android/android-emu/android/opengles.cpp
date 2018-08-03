/* Copyright (C) 2011 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#include "android/opengles.h"

#include "android/crashreport/crash-handler.h"
#include "android/emulation/GoldfishDma.h"
#include "android/featurecontrol/FeatureControl.h"
#include "android/globals.h"
#include "android/opengl/emugl_config.h"
#include "android/opengl/logger.h"
#include "android/snapshot/PathUtils.h"
#include "android/utils/bufprint.h"
#include "android/utils/debug.h"
#include "android/utils/dll.h"
#include "android/utils/path.h"
#include "config-host.h"

#include "OpenglRender/render_api_functions.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define D(...)  VERBOSE_PRINT(init,__VA_ARGS__)
#define DD(...) VERBOSE_PRINT(gles,__VA_ARGS__)

/* Name of the GLES rendering library we're going to use */
#if UINTPTR_MAX == UINT32_MAX
#define RENDERER_LIB_NAME  "libOpenglRender"
#elif UINTPTR_MAX == UINT64_MAX
#define RENDERER_LIB_NAME  "lib64OpenglRender"
#else
#error Unknown UINTPTR_MAX
#endif

/* Declared in "android/globals.h" */
int  android_gles_fast_pipes = 1;

// Define the Render API function pointers.
#define FUNCTION_(ret, name, sig, params) \
        static ret (*name) sig = NULL;
LIST_RENDER_API_FUNCTIONS(FUNCTION_)
#undef FUNCTION_

// Define a function that initializes the function pointers by looking up
// the symbols from the shared library.
static int initOpenglesEmulationFuncs(ADynamicLibrary* rendererLib) {
    void*  symbol;
    char*  error;

#define FUNCTION_(ret, name, sig, params) \
    symbol = adynamicLibrary_findSymbol(rendererLib, #name, &error); \
    if (symbol != NULL) { \
        using type = ret(sig); \
        name = (type*)symbol; \
    } else { \
        derror("GLES emulation: Could not find required symbol (%s): %s", #name, error); \
        free(error); \
        return -1; \
    }
    LIST_RENDER_API_FUNCTIONS(FUNCTION_)
#undef FUNCTION_

    return 0;
}

static bool sRendererUsesSubWindow;
static bool sEgl2egl;
static emugl::RenderLibPtr sRenderLib = nullptr;
static emugl::RendererPtr sRenderer = nullptr;

static EGLDispatch* sEgl;
static GLESv2Dispatch* sGLES;

int android_initOpenglesEmulation() {
    char* error = NULL;

    if (sRenderLib != NULL)
        return 0;

    D("Initializing hardware OpenGLES emulation support");

    ADynamicLibrary* const rendererSo =
            adynamicLibrary_open(RENDERER_LIB_NAME, &error);
    if (rendererSo == NULL) {
        derror("Could not load OpenGLES emulation library [%s]: %s",
               RENDERER_LIB_NAME, error);
        return -1;
    }

    /* Resolve the functions */
    if (initOpenglesEmulationFuncs(rendererSo) < 0) {
        derror("OpenGLES emulation library mismatch. Be sure to use the correct version!");
        goto BAD_EXIT;
    }

    sRenderLib = initLibrary();
    if (!sRenderLib) {
        derror("OpenGLES initialization failed!");
        goto BAD_EXIT;
    }

    sRendererUsesSubWindow = true;
    if (const char* env = getenv("ANDROID_GL_SOFTWARE_RENDERER")) {
        if (env[0] != '\0' && env[0] != '0') {
            sRendererUsesSubWindow = false;
        }
    }

    sEgl2egl = false;
    if (const char* env = getenv("ANDROID_EGL_ON_EGL")) {
        if (env[0] != '\0' && env[0] == '1') {
            sEgl2egl = true;
        }
    }

    sEgl = (EGLDispatch *)sRenderLib->getEGL();
    sGLES = (GLESv2Dispatch *)sRenderLib->getGL();

    return 0;

BAD_EXIT:
    derror("OpenGLES emulation library could not be initialized!");
    adynamicLibrary_close(rendererSo);
    return -1;
}

int
android_startOpenglesRenderer(int width, int height, bool guestPhoneApi, int guestApiLevel,
                              int* glesMajorVersion_out,
                              int* glesMinorVersion_out)
{
    if (!sRenderLib) {
        D("Can't start OpenGLES renderer without support libraries");
        return -1;
    }

    if (sRenderer) {
        return 0;
    }

    android_init_opengl_logger();

    sRenderLib->setRenderer(emuglConfig_get_current_renderer());
    sRenderLib->setAvdInfo(guestPhoneApi, guestApiLevel);
    sRenderLib->setCrashReporter(&crashhandler_die_format);
    sRenderLib->setFeatureController(&android::featurecontrol::isEnabled);
    sRenderLib->setSyncDevice(goldfish_sync_create_timeline,
            goldfish_sync_create_fence,
            goldfish_sync_timeline_inc,
            goldfish_sync_destroy_timeline,
            goldfish_sync_register_trigger_wait,
            goldfish_sync_device_exists);

    emugl_logger_struct logfuncs;
    logfuncs.coarse = android_opengl_logger_write;
    logfuncs.fine = android_opengl_cxt_logger_write;
    sRenderLib->setLogger(logfuncs);
    emugl_dma_ops dma_ops;
    dma_ops.add_buffer = android_goldfish_dma_ops.add_buffer;
    dma_ops.remove_buffer = android_goldfish_dma_ops.remove_buffer;
    dma_ops.get_host_addr = android_goldfish_dma_ops.get_host_addr;
    dma_ops.invalidate_host_mappings = android_goldfish_dma_ops.invalidate_host_mappings;
    dma_ops.unlock = android_goldfish_dma_ops.unlock;
    sRenderLib->setDmaOps(dma_ops);

    sRenderer = sRenderLib->initRenderer(width, height, sRendererUsesSubWindow, sEgl2egl);
    if (!sRenderer) {
        D("Can't start OpenGLES renderer?");
        return -1;
    }

    // after initRenderer is a success, the maximum GLES API is calculated depending
    // on feature control and host GPU support. Set the obtained GLES version here.
    if (glesMajorVersion_out && glesMinorVersion_out)
        sRenderLib->getGlesVersion(glesMajorVersion_out, glesMinorVersion_out);
    return 0;
}

bool
android_asyncReadbackSupported() {
    if (sRenderer) {
        return sRenderer->asyncReadbackSupported();
    } else {
        D("tried to query async readback support "
          "before renderer initialized. Likely guest rendering");
        return false;
    }
}

void
android_setPostCallback(OnPostFunc onPost, void* onPostContext)
{
    if (sRenderer) {
        sRenderer->setPostCallback(onPost, onPostContext);
    }
}

ReadPixelsFunc android_getReadPixelsFunc() {
    if (sRenderer) {
        return sRenderer->getReadPixelsCallback();
    } else {
        return nullptr;
    }
}

static char* strdupBaseString(const char* src) {
    const char* begin = strchr(src, '(');
    if (!begin) {
        return strdup(src);
    }

    const char* end = strrchr(begin + 1, ')');
    if (!end) {
        return strdup(src);
    }

    // src is of the form:
    // "foo (barzzzzzzzzzz)"
    //       ^            ^
    //       (b+1)        e
    //     = 5            18
    int len;
    begin += 1;
    len = end - begin;

    char* result;
    result = (char*)malloc(len + 1);
    memcpy(result, begin, len);
    result[len] = '\0';
    return result;
}

void android_getOpenglesHardwareStrings(char** vendor,
                                        char** renderer,
                                        char** version) {
    assert(vendor != NULL && renderer != NULL && version != NULL);
    assert(*vendor == NULL && *renderer == NULL && *version == NULL);
    if (!sRenderer) {
        D("Can't get OpenGL ES hardware strings when renderer not started");
        return;
    }

    const emugl::Renderer::HardwareStrings strings =
            sRenderer->getHardwareStrings();
    D("OpenGL Vendor=[%s]", strings.vendor.c_str());
    D("OpenGL Renderer=[%s]", strings.renderer.c_str());
    D("OpenGL Version=[%s]", strings.version.c_str());

    /* Special case for the default ES to GL translators: extract the strings
     * of the underlying OpenGL implementation. */
    if (strncmp(strings.vendor.c_str(), "Google", 6) == 0 &&
            strncmp(strings.renderer.c_str(), "Android Emulator OpenGL ES Translator", 37) == 0) {
        *vendor = strdupBaseString(strings.vendor.c_str());
        *renderer = strdupBaseString(strings.renderer.c_str());
        *version = strdupBaseString(strings.version.c_str());
    } else {
        *vendor = strdup(strings.vendor.c_str());
        *renderer = strdup(strings.renderer.c_str());
        *version = strdup(strings.version.c_str());
    }
}

void android_getOpenglesVersion(int* maj, int* min) {
    sRenderLib->getGlesVersion(maj, min);
    fprintf(stderr, "%s: maj min %d %d\n", __func__, *maj, *min);
}

void
android_stopOpenglesRenderer(bool wait)
{
    if (sRenderer) {
        sRenderer->stop(wait);
        if (wait) {
            sRenderer.reset();
            android_stop_opengl_logger();
        }
    }
}

int
android_showOpenglesWindow(void* window, int wx, int wy, int ww, int wh,
                           int fbw, int fbh, float dpr, float rotation,
                           bool deleteExisting)
{
    if (!sRenderer) {
        return -1;
    }
    FBNativeWindowType win = (FBNativeWindowType)(uintptr_t)window;
    bool success = sRenderer->showOpenGLSubwindow(
            win, wx, wy, ww, wh, fbw, fbh, dpr, rotation,
                       deleteExisting);
    return success ? 0 : -1;
}

void
android_setOpenglesTranslation(float px, float py)
{
    if (sRenderer) {
        sRenderer->setOpenGLDisplayTranslation(px, py);
    }
}

void
android_setOpenglesScreenMask(int width, int height, const unsigned char* rgbaData)
{
    if (sRenderer) {
        sRenderer->setScreenMask(width, height, rgbaData);
    }
}

int
android_hideOpenglesWindow(void)
{
    if (!sRenderer) {
        return -1;
    }
    bool success = sRenderer->destroyOpenGLSubwindow();
    return success ? 0 : -1;
}

void
android_redrawOpenglesWindow(void)
{
    if (sRenderer) {
        sRenderer->repaintOpenGLDisplay();
    }
}

bool
android_hasGuestPostedAFrame(void)
{
    if (sRenderer) {
        return sRenderer->hasGuestPostedAFrame();
    }
    return false;
}

void
android_resetGuestPostedAFrame(void)
{
    if (sRenderer) {
        sRenderer->resetGuestPostedAFrame();
    }
}

static ScreenshotFunc sScreenshotFunc = nullptr;

void android_registerScreenshotFunc(ScreenshotFunc f)
{
    sScreenshotFunc = f;
}

void android_screenShot(const char* dirname)
{
    if (sScreenshotFunc) {
        sScreenshotFunc(dirname);
    }
}

const emugl::RendererPtr& android_getOpenglesRenderer() {
    return sRenderer;
}

void android_cleanupProcGLObjects(uint64_t puid) {
    if (sRenderer) {
        sRenderer->cleanupProcGLObjects(puid);
    }
}

static void* sDisplay, * sSurface, * sConfig, * sContext;
static int sWidth, sHeight;
static EGLint s_gles_attr[5];

extern void tinyepoxy_init(GLESv2Dispatch* gles, int version);

static bool prepare_epoxy(void) {
    void* unused;
    if (!sRenderLib->getDSCC(&sDisplay, &sSurface, &sConfig, &unused,
                             &sWidth, &sHeight)) {
        return false;
    }
    int major, minor;
    sRenderLib->getGlesVersion(&major, &minor);
    EGLint attr[] = {
        EGL_CONTEXT_CLIENT_VERSION, major,
        EGL_CONTEXT_MINOR_VERSION_KHR, minor,
        EGL_NONE
    };
    sContext = sEgl->eglCreateContext(sDisplay, sConfig, EGL_NO_CONTEXT,
                                      attr);
    if (sContext == nullptr) {
        return false;
    }
    static_assert(sizeof(attr) == sizeof(s_gles_attr), "Mismatch");
    memcpy(s_gles_attr, attr, sizeof(s_gles_attr));
    tinyepoxy_init(sGLES, major * 10 + minor);
    return true;
}

struct DisplayChangeListener;
struct QEMUGLParams;

void * android_gl_create_context(DisplayChangeListener * unuse1,
                                 QEMUGLParams* unuse2) {
    static bool ok =  prepare_epoxy();
    if (!ok) {
        return nullptr;
    }
    sEgl->eglMakeCurrent(sDisplay, sSurface, sSurface, sContext);
    return sEgl->eglCreateContext(sDisplay, sConfig, sContext, s_gles_attr);
}

void android_gl_destroy_context(DisplayChangeListener* unused, void * ctx) {
    sEgl->eglDestroyContext(sDisplay, ctx);
}

int android_gl_make_context_current(DisplayChangeListener* unused, void * ctx) {
    return sEgl->eglMakeCurrent(sDisplay, sSurface, sSurface, ctx);
}

static GLuint s_tex_id, s_fbo_id;
static uint32_t s_gfx_h, s_gfx_w;
static bool s_y0_top;

// ui/gtk-egl.c:gd_egl_scanout_texture as reference.
void android_gl_scanout_texture(DisplayChangeListener* unuse,
                                uint32_t backing_id,
                                bool backing_y_0_top,
                                uint32_t backing_width,
                                uint32_t backing_height,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h) {
    s_tex_id = backing_id;
    s_gfx_h = h;
    s_gfx_w = w;
    s_y0_top = backing_y_0_top;
    sEgl->eglMakeCurrent(sDisplay, sSurface, sSurface, sContext);
    if (!s_fbo_id) {
        sGLES->glGenFramebuffers(1, &s_fbo_id);
    }
    sGLES->glBindFramebuffer(GL_FRAMEBUFFER_EXT, s_fbo_id);
    sGLES->glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, backing_id, 0);
}

// ui/gtk-egl.c:gd_egl_scanout_flush as reference.
void android_gl_scanout_flush(DisplayChangeListener* unuse,
                              uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    if (!s_fbo_id)  {
        return;
    }
    sEgl->eglMakeCurrent(sDisplay, sSurface, sSurface, sContext);

    sGLES->glBindFramebuffer(GL_READ_FRAMEBUFFER, s_fbo_id);
    sGLES->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    int y1 = s_y0_top ? 0 : s_gfx_h;
    int y2 = s_y0_top ? s_gfx_h : 0;

    sGLES->glViewport(0, 0, sWidth, sHeight);
    sGLES->glBlitFramebuffer(0, y1, s_gfx_w, y2,
                             0, 0, sWidth, sHeight,
                             GL_COLOR_BUFFER_BIT, GL_NEAREST);
    sEgl->eglSwapBuffers(sDisplay, sSurface);
    sGLES->glBindFramebuffer(GL_FRAMEBUFFER_EXT, s_fbo_id);
}

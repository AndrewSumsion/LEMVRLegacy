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
#include "OpenGLESDispatch/EGLDispatch.h"

#include "android/base/synchronization/Lock.h"
#include "android/base/memory/LazyInstance.h"

#include "emugl/common/shared_library.h"

#include <stdio.h>
#include <stdlib.h>

EGLDispatch s_egl;

#define DEFAULT_EGL_LIB EMUGL_LIBNAME("EGL_translator")

#define RENDER_EGL_LOAD_FIELD(return_type, function_name, signature) \
    s_egl. function_name = (function_name ## _t) lib->findSymbol(#function_name); \

#define RENDER_EGL_LOAD_FIELD_WITH_EGL(return_type, function_name, signature) \
    if ((!s_egl. function_name) && s_egl.eglGetProcAddress) s_egl. function_name = \
            (function_name ## _t) s_egl.eglGetProcAddress(#function_name); \

#define RENDER_EGL_LOAD_OPTIONAL_FIELD(return_type, function_name, signature) \
    if (s_egl.eglGetProcAddress) s_egl. function_name = \
            (function_name ## _t) s_egl.eglGetProcAddress(#function_name); \
    if (!s_egl.function_name || !s_egl.eglGetProcAddress) \
            RENDER_EGL_LOAD_FIELD(return_type, function_name, signature)

struct AsyncEGLInitData {
    bool initialized = false;
    android::base::Lock lock;
};

static android::base::LazyInstance<AsyncEGLInitData> sInitData =
    LAZY_INSTANCE_INIT;

bool init_egl_dispatch() {
    android::base::AutoLock lock(sInitData->lock);

    if (sInitData->initialized) return true;

    const char *libName = getenv("ANDROID_EGL_LIB");
    if (!libName) libName = DEFAULT_EGL_LIB;
    char error[256];
    emugl::SharedLibrary *lib = emugl::SharedLibrary::open(libName, error, sizeof(error));
    if (!lib) {
        printf("Failed to open %s: [%s]\n", libName, error);
        return false;
    }

    LIST_RENDER_EGL_FUNCTIONS(RENDER_EGL_LOAD_FIELD)
    LIST_RENDER_EGL_FUNCTIONS(RENDER_EGL_LOAD_FIELD_WITH_EGL)
    LIST_RENDER_EGL_EXTENSIONS_FUNCTIONS(RENDER_EGL_LOAD_OPTIONAL_FIELD)
    LIST_RENDER_EGL_SNAPSHOT_FUNCTIONS(RENDER_EGL_LOAD_FIELD)

    sInitData->initialized = true;

    return true;
}

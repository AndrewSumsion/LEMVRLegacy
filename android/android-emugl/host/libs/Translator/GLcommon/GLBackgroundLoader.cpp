/*
* Copyright (C) 2017 The Android Open Source Project
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

#include "GLcommon/GLBackgroundLoader.h"

#include "android/utils/system.h"
#include "android/base/system/System.h"
#include "GLcommon/GLEScontext.h"
#include "GLcommon/SaveableTexture.h"

#include <EGL/eglext.h>
#include <GLES2/gl2.h>

intptr_t GLBackgroundLoader::main() {

#if SNAPSHOT_PROFILE > 1
    printf("Starting GL background loading at %" PRIu64 " ms\n",
           get_uptime_ms());
#endif

    if (!m_eglIface.createAndBindAuxiliaryContext(&m_context, &m_surface)) {
        return 0;
    }

    for (const auto& it: m_textureMap) {
        // Acquire the texture loader for each load; bail
        // in case something else happened to interrupt loading.
        auto ptr = m_textureLoaderWPtr.lock();
        if (!ptr) break;

        const SaveableTexturePtr& saveable = it.second;
        if (saveable) {
            m_glesIface.restoreTexture(saveable.get());
        }
    }

    m_eglIface.unbindAndDestroyAuxiliaryContext(m_context, m_surface);
    m_textureMap.clear();

#if SNAPSHOT_PROFILE > 1
    printf("Finished GL background loading at %" PRIu64 " ms\n",
           get_uptime_ms());
#endif

    return 0;
}

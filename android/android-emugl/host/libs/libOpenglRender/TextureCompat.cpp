/*
* Copyright (C) 2021 The Android Open Source Project
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

#include "TextureCompat.h"

#include "EglGlobalInfo.h"
#include <GLcommon/GLESmacros.h>
#include <GLcommon/NamedObject.h>
#include <GLcommon/TranslatorIfaces.h>
#include <GLcommon/GLEScontext.h>

unsigned int getGlobalTextureName(unsigned int localName) {
    const EGLiface* s_eglIface = EglGlobalInfo::getInstance()->getEglIface();

    GET_CTX_RET(0);

    return ctx->shareGroup()->getGlobalName(NamedObjectType::TEXTURE, localName);
}
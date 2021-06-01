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
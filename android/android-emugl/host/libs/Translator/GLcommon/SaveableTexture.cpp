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

#include "GLcommon/SaveableTexture.h"

#include "android/base/ArraySize.h"
#include "android/base/containers/SmallVector.h"
#include "android/base/files/StreamSerializing.h"
#include "GLcommon/GLEScontext.h"
#include "GLcommon/GLutils.h"
#include "GLES2/gl2ext.h"

#include <algorithm>

static uint32_t s_texAlign(uint32_t v, uint32_t align) {
    uint32_t rem = v % align;
    return rem ? (v + (align - rem)) : v;
}

// s_computePixelSize is both in the host and the guest. Consider moving it to
// android-emugl/shared

static int s_computePixelSize(GLenum format, GLenum type) {
#define FORMAT_ERROR(format, type)                                         \
    fprintf(stderr, "%s:%d unknown format/type 0x%x 0x%x\n", __FUNCTION__, \
            __LINE__, format, type);

    switch (type) {
        case GL_BYTE:
            switch (format) {
                case GL_R8:
                case GL_R8I:
                case GL_R8_SNORM:
                case GL_RED:
                    return 1;
                case GL_RED_INTEGER:
                    return 1;
                case GL_RG8:
                case GL_RG8I:
                case GL_RG8_SNORM:
                case GL_RG:
                    return 1 * 2;
                case GL_RG_INTEGER:
                    return 1 * 2;
                case GL_RGB8:
                case GL_RGB8I:
                case GL_RGB8_SNORM:
                case GL_RGB:
                    return 1 * 3;
                case GL_RGB_INTEGER:
                    return 1 * 3;
                case GL_RGBA8:
                case GL_RGBA8I:
                case GL_RGBA8_SNORM:
                case GL_RGBA:
                    return 1 * 4;
                case GL_RGBA_INTEGER:
                    return 1 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_BYTE:
            switch (format) {
                case GL_R8:
                case GL_R8UI:
                case GL_RED:
                    return 1;
                case GL_RED_INTEGER:
                    return 1;
                case GL_ALPHA8_EXT:
                case GL_ALPHA:
                    return 1;
                case GL_LUMINANCE8_EXT:
                case GL_LUMINANCE:
                    return 1;
                case GL_LUMINANCE8_ALPHA8_EXT:
                case GL_LUMINANCE_ALPHA:
                    return 1 * 2;
                case GL_RG8:
                case GL_RG8UI:
                case GL_RG:
                    return 1 * 2;
                case GL_RG_INTEGER:
                    return 1 * 2;
                case GL_RGB8:
                case GL_RGB8UI:
                case GL_SRGB8:
                case GL_RGB:
                    return 1 * 3;
                case GL_RGB_INTEGER:
                    return 1 * 3;
                case GL_RGBA8:
                case GL_RGBA8UI:
                case GL_SRGB8_ALPHA8:
                case GL_RGBA:
                    return 1 * 4;
                case GL_RGBA_INTEGER:
                    return 1 * 4;
                case GL_BGRA_EXT:
                case GL_BGRA8_EXT:
                    return 1 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_SHORT:
            switch (format) {
                case GL_R16I:
                case GL_RED_INTEGER:
                    return 2;
                case GL_RG16I:
                case GL_RG_INTEGER:
                    return 2 * 2;
                case GL_RGB16I:
                case GL_RGB_INTEGER:
                    return 2 * 3;
                case GL_RGBA16I:
                case GL_RGBA_INTEGER:
                    return 2 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_SHORT:
            switch (format) {
                case GL_DEPTH_COMPONENT16:
                case GL_DEPTH_COMPONENT:
                    return 2;
                case GL_R16UI:
                case GL_RED_INTEGER:
                    return 2;
                case GL_RG16UI:
                case GL_RG_INTEGER:
                    return 2 * 2;
                case GL_RGB16UI:
                case GL_RGB_INTEGER:
                    return 2 * 3;
                case GL_RGBA16UI:
                case GL_RGBA_INTEGER:
                    return 2 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_INT:
            switch (format) {
                case GL_R32I:
                case GL_RED_INTEGER:
                    return 4;
                case GL_RG32I:
                case GL_RG_INTEGER:
                    return 4 * 2;
                case GL_RGB32I:
                case GL_RGB_INTEGER:
                    return 4 * 3;
                case GL_RGBA32I:
                case GL_RGBA_INTEGER:
                    return 4 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_INT:
            switch (format) {
                case GL_DEPTH_COMPONENT16:
                case GL_DEPTH_COMPONENT24:
                case GL_DEPTH_COMPONENT32_OES:
                case GL_DEPTH_COMPONENT:
                    return 4;
                case GL_R32UI:
                case GL_RED_INTEGER:
                    return 4;
                case GL_RG32UI:
                case GL_RG_INTEGER:
                    return 4 * 2;
                case GL_RGB32UI:
                case GL_RGB_INTEGER:
                    return 4 * 3;
                case GL_RGBA32UI:
                case GL_RGBA_INTEGER:
                    return 4 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4_REV_EXT:
        case GL_UNSIGNED_SHORT_1_5_5_5_REV_EXT:
            return 2;
        case GL_UNSIGNED_INT_10F_11F_11F_REV:
        case GL_UNSIGNED_INT_5_9_9_9_REV:
        case GL_UNSIGNED_INT_2_10_10_10_REV:
        case GL_UNSIGNED_INT_24_8_OES:
            return 4;
        case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
            return 4 + 4;
        case GL_FLOAT:
            switch (format) {
                case GL_DEPTH_COMPONENT32F:
                case GL_DEPTH_COMPONENT:
                    return 4;
                case GL_ALPHA32F_EXT:
                case GL_ALPHA:
                    return 4;
                case GL_LUMINANCE32F_EXT:
                case GL_LUMINANCE:
                    return 4;
                case GL_LUMINANCE_ALPHA32F_EXT:
                case GL_LUMINANCE_ALPHA:
                    return 4 * 2;
                case GL_RED:
                    return 4;
                case GL_R32F:
                    return 4;
                case GL_RG:
                    return 4 * 2;
                case GL_RG32F:
                    return 4 * 2;
                case GL_RGB:
                    return 4 * 3;
                case GL_RGB32F:
                    return 4 * 3;
                case GL_RGBA:
                    return 4 * 4;
                case GL_RGBA32F:
                    return 4 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        case GL_HALF_FLOAT:
        case GL_HALF_FLOAT_OES:
            switch (format) {
                case GL_ALPHA16F_EXT:
                case GL_ALPHA:
                    return 2;
                case GL_LUMINANCE16F_EXT:
                case GL_LUMINANCE:
                    return 2;
                case GL_LUMINANCE_ALPHA16F_EXT:
                case GL_LUMINANCE_ALPHA:
                    return 2 * 2;
                case GL_RED:
                    return 2;
                case GL_R16F:
                    return 2;
                case GL_RG:
                    return 2 * 2;
                case GL_RG16F:
                    return 2 * 2;
                case GL_RGB:
                    return 2 * 3;
                case GL_RGB16F:
                    return 2 * 3;
                case GL_RGBA:
                    return 2 * 4;
                case GL_RGBA16F:
                    return 2 * 4;
                default:
                    FORMAT_ERROR(format, type);
            }
            break;
        default:
            FORMAT_ERROR(format, type);
    }

    return 0;
}

static uint32_t s_texImageSize(GLenum internalformat,
                               GLenum type,
                               int unpackAlignment,
                               GLsizei width,
                               GLsizei height) {
    uint32_t alignedWidth = s_texAlign(width, unpackAlignment);
    uint32_t pixelSize = s_computePixelSize(internalformat, type);
    uint32_t totalSize = pixelSize * alignedWidth * height;

    return totalSize;
}

SaveableTexture::SaveableTexture(const EglImage& eglImage)
    : m_width(eglImage.width),
      m_height(eglImage.height),
      m_format(eglImage.format),
      m_internalFormat(eglImage.internalFormat),
      m_type(eglImage.type),
      m_border(eglImage.border),
      m_globalName(eglImage.globalTexObj->getGlobalName()) {}

SaveableTexture::SaveableTexture(const TextureData& texture)
    : m_target(texture.target),
      m_width(texture.width),
      m_height(texture.height),
      m_depth(texture.depth),
      m_format(texture.format),
      m_internalFormat(texture.internalFormat),
      m_type(texture.type),
      m_border(texture.border),
      m_globalName(texture.globalName) {}

SaveableTexture::SaveableTexture(GlobalNameSpace* globalNameSpace,
                                 loader_t loader)
    : m_loader(loader), m_globalNamespace(globalNameSpace) {
    mNeedRestore = true;
}

void SaveableTexture::loadFromStream(android::base::Stream* stream) {
    m_target = stream->getBe32();
    m_width = stream->getBe32();
    m_height = stream->getBe32();
    m_depth = stream->getBe32();
    m_format = stream->getBe32();
    m_internalFormat = stream->getBe32();
    m_type = stream->getBe32();
    m_border = stream->getBe32();
    // TODO: handle other texture targets
    if (m_target == GL_TEXTURE_2D || m_target == GL_TEXTURE_CUBE_MAP ||
        m_target == GL_TEXTURE_3D || m_target == GL_TEXTURE_2D_ARRAY) {
        unsigned int numLevels =
                1 + floor(log2((float)std::max(m_width, m_height)));
        auto loadTex = [stream, numLevels](
                               std::unique_ptr<LevelImageData[]>& levelData,
                               bool isDepth) {
            levelData.reset(new LevelImageData[numLevels]);
            for (unsigned int level = 0; level < numLevels; level++) {
                levelData[level].m_width = stream->getBe32();
                levelData[level].m_height = stream->getBe32();
                if (isDepth) {
                    levelData[level].m_depth = stream->getBe32();
                }
                loadBuffer(stream, &levelData[level].m_data);
            }
        };
        switch (m_target) {
            case GL_TEXTURE_2D:
                loadTex(m_levelData[0], false);
                break;
            case GL_TEXTURE_CUBE_MAP:
                for (int i = 0; i < 6; i++) {
                    loadTex(m_levelData[i], false);
                }
                break;
            case GL_TEXTURE_3D:
            case GL_TEXTURE_2D_ARRAY:
                loadTex(m_levelData[0], true);
                break;
            default:
                break;
        }
        // Load tex param
        mTexMagFilter = stream->getBe32();
        mTexMinFilter = stream->getBe32();
        mTexWrapS = stream->getBe32();
        mTexWrapT = stream->getBe32();
    } else {
        fprintf(stderr, "Warning: texture target %d not supported\n", m_target);
    }
}

void SaveableTexture::onSave(
        android::base::Stream* stream, Buffer* buffer) const {
    stream->putBe32(m_target);
    stream->putBe32(m_width);
    stream->putBe32(m_height);
    stream->putBe32(m_depth);
    stream->putBe32(m_format);
    stream->putBe32(m_internalFormat);
    stream->putBe32(m_type);
    stream->putBe32(m_border);
    // TODO: handle other texture targets
    if (m_target == GL_TEXTURE_2D || m_target == GL_TEXTURE_CUBE_MAP ||
        m_target == GL_TEXTURE_3D || m_target == GL_TEXTURE_2D_ARRAY) {
        static constexpr GLenum pixelStoreIndexes[] = {
                GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS,
                GL_PACK_ALIGNMENT,
        };
        static constexpr GLint pixelStoreDesired[] = {0, 0, 0, 1};
        GLint pixelStorePrev[android::base::arraySize(pixelStoreIndexes)];

        GLint prevTex = 0;
        GLDispatch& dispatcher = GLEScontext::dispatcher();
        assert(dispatcher.glGetIntegerv);
        for (int i = 0; i != android::base::arraySize(pixelStoreIndexes); ++i) {
            if (isGles2Gles() && pixelStoreIndexes[i] != GL_PACK_ALIGNMENT &&
                pixelStoreIndexes[i] != GL_UNPACK_ALIGNMENT) {
                continue;
            }
            dispatcher.glGetIntegerv(pixelStoreIndexes[i], &pixelStorePrev[i]);
            if (pixelStorePrev[i] != pixelStoreDesired[i]) {
                dispatcher.glPixelStorei(pixelStoreIndexes[i],
                                         pixelStoreDesired[i]);
            }
        }
        switch (m_target) {
            case GL_TEXTURE_2D:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
                break;
            case GL_TEXTURE_CUBE_MAP:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prevTex);
                break;
            case GL_TEXTURE_3D:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_3D, &prevTex);
                break;
            case GL_TEXTURE_2D_ARRAY:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevTex);
                break;
            default:
                break;
        }
        dispatcher.glBindTexture(m_target, m_globalName);
        // Get the number of mipmap levels.
        unsigned int numLevels =
                1 + floor(log2((float)std::max(m_width, m_height)));
        auto saveTex = [this, stream, numLevels, &dispatcher, buffer](
                               GLenum target, bool isDepth) {
            GLint width = m_width;
            GLint height = m_height;
            for (unsigned int level = 0; level < numLevels; level++) {
                GLint depth = 1;
                if (!isGles2Gles()) {
                    dispatcher.glGetTexLevelParameteriv(target, level,
                                                        GL_TEXTURE_WIDTH, &width);
                    dispatcher.glGetTexLevelParameteriv(target, level,
                                                        GL_TEXTURE_HEIGHT, &height);
                }
                stream->putBe32(width);
                stream->putBe32(height);
                if (isDepth) {
                    dispatcher.glGetTexLevelParameteriv(
                            target, level, GL_TEXTURE_DEPTH, &depth);
                    stream->putBe32(depth);
                }
                // Snapshot texture data
                buffer->clear();
                buffer->resize_noinit(
                        s_texImageSize(m_format, m_type, 1, width, height) *
                        depth);
                if (!buffer->empty()) {
                    dispatcher.glGetTexImage(target, level, m_format, m_type,
                                             buffer->data());
                }
                saveBuffer(stream, *buffer);
                if (isGles2Gles()) {
                    width = std::max(width/2, 1);
                    height = std::max(height/2, 1);
                }
            }
        };
        switch (m_target) {
            case GL_TEXTURE_2D:
                saveTex(GL_TEXTURE_2D, false);
                break;
            case GL_TEXTURE_CUBE_MAP:
                saveTex(GL_TEXTURE_CUBE_MAP_POSITIVE_X, false);
                saveTex(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, false);
                saveTex(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, false);
                saveTex(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, false);
                saveTex(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, false);
                saveTex(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, false);
                break;
            case GL_TEXTURE_3D:
                saveTex(GL_TEXTURE_3D, true);
                break;
            case GL_TEXTURE_2D_ARRAY:
                saveTex(GL_TEXTURE_2D_ARRAY, true);
                break;
            default:
                break;
        }
        // Snapshot texture param
        auto saveParam = [this, stream, &dispatcher](GLenum pname) {
            GLint param;
            dispatcher.glGetTexParameteriv(m_target, pname, &param);
            stream->putBe32(param);
        };
        saveParam(GL_TEXTURE_MAG_FILTER);
        saveParam(GL_TEXTURE_MIN_FILTER);
        saveParam(GL_TEXTURE_WRAP_S);
        saveParam(GL_TEXTURE_WRAP_T);
        // Restore environment
        for (int i = 0; i != android::base::arraySize(pixelStoreIndexes); ++i) {
            if (isGles2Gles() && pixelStoreIndexes[i] != GL_PACK_ALIGNMENT &&
                pixelStoreIndexes[i] != GL_UNPACK_ALIGNMENT) {
                continue;
            }
            if (pixelStorePrev[i] != pixelStoreDesired[i]) {
                dispatcher.glPixelStorei(pixelStoreIndexes[i],
                                         pixelStorePrev[i]);
            }
        }
        dispatcher.glBindTexture(m_target, prevTex);
    } else {
        fprintf(stderr, "Warning: texture target 0x%x not supported\n",
                m_target);
    }
}

void SaveableTexture::restore() {
    assert(m_loader);
    m_loader(this);
    if (m_target == GL_TEXTURE_2D || m_target == GL_TEXTURE_CUBE_MAP ||
        m_target == GL_TEXTURE_3D || m_target == GL_TEXTURE_2D_ARRAY) {
        // restore the texture
        GLDispatch& dispatcher = GLEScontext::dispatcher();
        // Make sure we are using the right dispatcher
        assert(dispatcher.glGetIntegerv);

        static constexpr GLenum pixelStoreIndexes[] = {
                GL_UNPACK_ROW_LENGTH,  GL_UNPACK_IMAGE_HEIGHT,
                GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS,
                GL_UNPACK_SKIP_IMAGES, GL_UNPACK_ALIGNMENT,
        };

        static constexpr GLint pixelStoreDesired[] = {0, 0, 0, 0, 0, 1};

        GLint pixelStorePrev[android::base::arraySize(pixelStoreIndexes)];
        for (int i = 0; i != android::base::arraySize(pixelStoreIndexes); ++i) {
            if (isGles2Gles() && pixelStoreIndexes[i] != GL_PACK_ALIGNMENT &&
                pixelStoreIndexes[i] != GL_UNPACK_ALIGNMENT) {
                continue;
            }
            dispatcher.glGetIntegerv(pixelStoreIndexes[i], &pixelStorePrev[i]);
            if (pixelStorePrev[i] != pixelStoreDesired[i]) {
                dispatcher.glPixelStorei(pixelStoreIndexes[i],
                                         pixelStoreDesired[i]);
            }
        }

        m_globalTexObj.reset(new NamedObject(
                GenNameInfo(NamedObjectType::TEXTURE), m_globalNamespace));
        m_globalName = m_globalTexObj->getGlobalName();
        GLint prevTex = 0;
        switch (m_target) {
            case GL_TEXTURE_2D:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
                break;
            case GL_TEXTURE_CUBE_MAP:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &prevTex);
                break;
            case GL_TEXTURE_3D:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_3D, &prevTex);
                break;
            case GL_TEXTURE_2D_ARRAY:
                dispatcher.glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &prevTex);
                break;
            default:
                break;
        }
        dispatcher.glBindTexture(m_target, m_globalName);
        // Restore texture data
        dispatcher.glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Get the number of mipmap levels.
        unsigned int numLevels =
                1 + floor(log2((float)std::max(m_width, m_height)));
        auto restoreTex2D =
                [this, numLevels, &dispatcher](
                        GLenum target,
                        std::unique_ptr<LevelImageData[]>& levelData) {
                    for (unsigned int level = 0; level < numLevels; level++) {
                        const void* pixels =
                                levelData[level].m_data.empty()
                                        ? nullptr
                                        : levelData[level].m_data.data();
                        if (!level || pixels) {
                            dispatcher.glTexImage2D(
                                    target, level, m_internalFormat,
                                    levelData[level].m_width,
                                    levelData[level].m_height, m_border,
                                    m_format, m_type, pixels);
                        }
                    }
                    levelData.reset();
                };
        auto restoreTex3D =
                [this, numLevels, &dispatcher](
                        GLenum target,
                        std::unique_ptr<LevelImageData[]>& levelData) {
                    for (unsigned int level = 0; level < numLevels; level++) {
                        const void* pixels =
                                levelData[level].m_data.empty()
                                        ? nullptr
                                        : levelData[level].m_data.data();
                        if (!level || pixels) {
                            dispatcher.glTexImage3D(
                                    target, level, m_internalFormat,
                                    levelData[level].m_width,
                                    levelData[level].m_height,
                                    levelData[level].m_depth, m_border,
                                    m_format, m_type, pixels);
                        }
                    }
                    levelData.reset();
                };
        switch (m_target) {
            case GL_TEXTURE_2D:
                restoreTex2D(GL_TEXTURE_2D, m_levelData[0]);
                break;
            case GL_TEXTURE_CUBE_MAP:
                restoreTex2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X, m_levelData[0]);
                restoreTex2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_X, m_levelData[1]);
                restoreTex2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Y, m_levelData[2]);
                restoreTex2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Y, m_levelData[3]);
                restoreTex2D(GL_TEXTURE_CUBE_MAP_POSITIVE_Z, m_levelData[4]);
                restoreTex2D(GL_TEXTURE_CUBE_MAP_NEGATIVE_Z, m_levelData[5]);
                break;
            case GL_TEXTURE_3D:
            case GL_TEXTURE_2D_ARRAY:
                restoreTex3D(m_target, m_levelData[0]);
                break;
            default:
                break;
        }
        // Restore tex param
        dispatcher.glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER,
                                   mTexMagFilter);
        dispatcher.glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER,
                                   mTexMinFilter);
        dispatcher.glTexParameteri(m_target, GL_TEXTURE_WRAP_S, mTexWrapS);
        dispatcher.glTexParameteri(m_target, GL_TEXTURE_WRAP_T, mTexWrapT);
        // Restore environment
        for (int i = 0; i != android::base::arraySize(pixelStoreIndexes); ++i) {
            if (isGles2Gles() && pixelStoreIndexes[i] != GL_PACK_ALIGNMENT &&
                pixelStoreIndexes[i] != GL_UNPACK_ALIGNMENT) {
                continue;
            }
            if (pixelStorePrev[i] != pixelStoreDesired[i]) {
                dispatcher.glPixelStorei(pixelStoreIndexes[i],
                                         pixelStorePrev[i]);
            }
        }
        dispatcher.glBindTexture(m_target, prevTex);
    }
}

const NamedObjectPtr& SaveableTexture::getGlobalObject() {
    touch();
    return m_globalTexObj;
}

void SaveableTexture::fillEglImage(EglImage* eglImage) {
    touch();
    eglImage->border = m_border;
    eglImage->format = m_format;
    eglImage->height = m_height;
    eglImage->globalTexObj = m_globalTexObj;
    eglImage->internalFormat = m_internalFormat;
    eglImage->type = m_type;
    eglImage->width = m_width;
}

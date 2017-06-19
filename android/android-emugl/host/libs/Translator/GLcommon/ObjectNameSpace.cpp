/*
* Copyright (C) 2016 The Android Open Source Project
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

#include "GLcommon/ObjectNameSpace.h"

#include "GLcommon/GLEScontext.h"
#include "GLcommon/TextureLoader.h"
#include "GLcommon/TextureSaver.h"
#include "GLcommon/TranslatorIfaces.h"
#include "android/base/containers/Lookup.h"
#include "android/base/files/PathUtils.h"
#include "android/base/files/StreamSerializing.h"
#include "android/base/memory/LazyInstance.h"

#include <assert.h>

NameSpace::NameSpace(NamedObjectType p_type, GlobalNameSpace *globalNameSpace,
        android::base::Stream* stream, ObjectData::loadObject_t loadObject) :
    m_type(p_type),
    m_globalNameSpace(globalNameSpace) {
    if (!stream) return;
    // When loading from a snapshot, we restores translator states here, but
    // host GPU states are not touched until postLoadRestore is called.
    // GlobalNames are not yet generated.
    size_t objSize = stream->getBe32();
    for (size_t obj = 0; obj < objSize; obj++) {
        ObjectLocalName localName = stream->getBe64();
        ObjectDataPtr data = loadObject((NamedObjectType)m_type,
                localName, stream);
        if (m_type == NamedObjectType::TEXTURE) {
            // Texture data are managed differently
            // They are loaded by GlobalNameSpace before loading
            // share groups
            TextureData* texData = (TextureData*)data.get();
            SaveableTexturePtr saveableTexture =
                    globalNameSpace->getSaveableTextureFromLoad(
                        texData->globalName);
            texData->setSaveableTexture(std::move(saveableTexture));
            texData->globalName = 0;
        }
        setObjectData(localName, std::move(data));
    }
}

NameSpace::~NameSpace() {
}

void NameSpace::postLoad(ObjectData::getObjDataPtr_t getObjDataPtr) {
    for (const auto& objData : m_objectDataMap) {
        objData.second->postLoad(getObjDataPtr);
    }
}

void NameSpace::touchTextures() {
    assert(m_type == NamedObjectType::TEXTURE);
    for (const auto& obj : m_objectDataMap) {
        TextureData* texData = (TextureData*)obj.second.get();
        SaveableTexturePtr saveableTexture(texData->releaseSaveableTexture());
        if (saveableTexture) {
            NamedObjectPtr texNamedObj = saveableTexture->getGlobalObject();
            setGlobalObject(obj.first, texNamedObj);
            texData->globalName = texNamedObj->getGlobalName();
        }
    }
}

void NameSpace::postLoadRestore(ObjectData::getGlobalName_t getGlobalName) {
    // Texture data are special, they got the global name from SaveableTexture
    // This is because texture data can be shared across multiple share groups
    if (m_type == NamedObjectType::TEXTURE) {
        touchTextures();
        return;
    }
    // 2 passes are needed for SHADER_OR_PROGRAM type, because (1) they
    // live in the same namespace (2) shaders must be created before
    // programs.
    int numPasses = m_type == NamedObjectType::SHADER_OR_PROGRAM
            ? 2 : 1;
    for (int pass = 0; pass < numPasses; pass ++) {
        for (const auto& obj : m_objectDataMap) {
            assert(m_type == ObjectDataType2NamedObjectType(
                    obj.second->getDataType()));
            // get global names
            if ((obj.second->getDataType() == PROGRAM_DATA && pass == 0)
                    || (obj.second->getDataType() == SHADER_DATA &&
                            pass == 1)) {
                continue;
            }
            genName(obj.second->getGenNameInfo(), obj.first, false);
            obj.second->restore(obj.first, getGlobalName);
        }
    }
}

void NameSpace::preSave(GlobalNameSpace *globalNameSpace) {
    if (m_type != NamedObjectType::TEXTURE) {
        return;
    }
    // In case we loaded textures from a previous snapshot and have not yet
    // restore them to GPU, we do the restoration here.
    // TODO: skip restoration and write saveableTexture directly to the new
    // snapshot
    touchTextures();
    for (const auto& obj : m_objectDataMap) {
        globalNameSpace->preSaveAddTex((const TextureData*)obj.second.get());
    }
}

void NameSpace::onSave(android::base::Stream* stream) {
    stream->putBe32(m_objectDataMap.size());
    for (const auto& obj : m_objectDataMap) {
        stream->putBe64(obj.first);
        obj.second->onSave(stream);
    }
}

ObjectLocalName
NameSpace::genName(GenNameInfo genNameInfo, ObjectLocalName p_localName, bool genLocal)
{
    assert(m_type == genNameInfo.m_type);
    ObjectLocalName localName = p_localName;
    if (genLocal) {
        do {
            localName = ++m_nextName;
        } while(localName == 0 ||
                m_localToGlobalMap.find(localName) !=
                        m_localToGlobalMap.end() );
    }

    auto it = m_localToGlobalMap.emplace(localName,
                                         NamedObjectPtr(
                                            new NamedObject(genNameInfo,
                                                    m_globalNameSpace))).first;
    unsigned int globalName = it->second->getGlobalName();
    m_globalToLocalMap[globalName] = localName;

    return localName;
}


unsigned int
NameSpace::getGlobalName(ObjectLocalName p_localName)
{
    NamesMap::iterator n( m_localToGlobalMap.find(p_localName) );
    if (n != m_localToGlobalMap.end()) {
        // object found - return its global name map
        return (*n).second->getGlobalName();
    }

    // object does not exist;
    return 0;
}

ObjectLocalName
NameSpace::getLocalName(unsigned int p_globalName)
{
    const auto it = m_globalToLocalMap.find(p_globalName);
    if (it != m_globalToLocalMap.end()) {
        return it->second;
    }

    return 0;
}

NamedObjectPtr NameSpace::getNamedObject(ObjectLocalName p_localName) {
    auto it = m_localToGlobalMap.find(p_localName);
    if (it != m_localToGlobalMap.end()) {
        return it->second;
    }

    return nullptr;
}

void
NameSpace::deleteName(ObjectLocalName p_localName)
{
    NamesMap::iterator n( m_localToGlobalMap.find(p_localName) );
    if (n != m_localToGlobalMap.end()) {
        m_globalToLocalMap.erase(n->second->getGlobalName());
        m_localToGlobalMap.erase(n);
    }
    m_objectDataMap.erase(p_localName);
}

bool
NameSpace::isObject(ObjectLocalName p_localName)
{
    return (m_localToGlobalMap.find(p_localName) != m_localToGlobalMap.end() );
}

void
NameSpace::setGlobalObject(ObjectLocalName p_localName,
                               NamedObjectPtr p_namedObject) {
    NamesMap::iterator n(m_localToGlobalMap.find(p_localName));
    if (n != m_localToGlobalMap.end()) {
        m_globalToLocalMap.erase(n->second->getGlobalName());
        (*n).second = p_namedObject;
    } else {
        m_localToGlobalMap.emplace(p_localName, p_namedObject);
    }
    m_globalToLocalMap.emplace(p_namedObject->getGlobalName(), p_localName);
}

void
NameSpace::replaceGlobalObject(ObjectLocalName p_localName,
                               NamedObjectPtr p_namedObject)
{
    NamesMap::iterator n( m_localToGlobalMap.find(p_localName) );
    if (n != m_localToGlobalMap.end()) {
        m_globalToLocalMap.erase(n->second->getGlobalName());
        (*n).second = p_namedObject;
        m_globalToLocalMap.emplace(p_namedObject->getGlobalName(), p_localName);
    }
}

static android::base::LazyInstance<ObjectDataPtr> nullObjectData = {};

const ObjectDataPtr& NameSpace::getObjectDataPtr(
        ObjectLocalName p_localName) {
    const auto it = m_objectDataMap.find(p_localName);
    if (it != m_objectDataMap.end()) {
        return it->second;
    }
    return *nullObjectData;
}

void NameSpace::setObjectData(ObjectLocalName p_localName,
        ObjectDataPtr data) {
    m_objectDataMap.emplace(p_localName, std::move(data));
}

void GlobalNameSpace::preSaveAddEglImage(const EglImage* eglImage) {
    unsigned int globalName = eglImage->globalTexObj->getGlobalName();
    emugl::Mutex::AutoLock lock(m_lock);
    m_textureMap.emplace(globalName,
            SaveableTexturePtr(new SaveableTexture(*eglImage)));
}

void GlobalNameSpace::preSaveAddTex(const TextureData* texture) {
    emugl::Mutex::AutoLock lock(m_lock);
    m_textureMap.emplace(texture->globalName,
            SaveableTexturePtr(new SaveableTexture(*texture)));
}

static std::string getTexFileName(const char* snapshotDir) {
    return android::base::PathUtils::join(snapshotDir, "texture.bin");
}

static android::base::LazyInstance<std::unique_ptr<TextureLoader> >
        s_textureLoader = LAZY_INSTANCE_INIT;

void GlobalNameSpace::onSave(android::base::Stream* stream,
                             const char* snapshotDir,
                             SaveableTexture::saver_t saver) {
    s_textureLoader->reset();
    android::base::SmallFixedVector<unsigned char, 128> buffer;
    std::string snapshotFileName = getTexFileName(snapshotDir);
    auto texFile = fopen(snapshotFileName.c_str(), "wb");
    if (!texFile) {
        fprintf(stderr, "Error: failed to write texture file.\n");
    } else {
        TextureSaver texSaver(android::base::StdioStream(
                texFile, android::base::StdioStream::kOwner));
        saveCollection(
                stream, m_textureMap,
                [saver, &texSaver](android::base::Stream* stream,
                                   const std::pair<const unsigned int,
                                                   SaveableTexturePtr>& tex) {
                    stream->putBe32(tex.first);
                    texSaver.saveTexture(
                            tex.first,
                            [saver, &tex](android::base::Stream* stream,
                                          TextureSaver::Buffer* buffer) {
                                saver(tex.second.get(), stream, buffer);
                            });
                });
    }
    decltype(m_textureMap)().swap(m_textureMap);
}

void GlobalNameSpace::onLoad(android::base::Stream* stream,
                             const char* snapshotDir,
                             SaveableTexture::creator_t creator) {
    assert(m_textureMap.size() == 0);
    std::string snapshotFileName = getTexFileName(snapshotDir);
    auto texFile = fopen(snapshotFileName.c_str(), "rb");
    if (!texFile) {
        fprintf(stderr, "Error: failed to read texture file.\n");
        return;
    }
    s_textureLoader->reset(new TextureLoader(texFile));
    if (!s_textureLoader.get()->start()) {
        fprintf(stderr,
                "Error: texture file unsupported version or corrupted.\n");
        return;
    }
    loadCollection(
            stream, &m_textureMap,
            [this, creator](android::base::Stream* stream) {
                unsigned int globalName = stream->getBe32();
                // A lot of function wrapping happens here.
                // When touched, saveableTexture triggers
                // s_textureLoader->loadTexture, which sets up the file position
                // and the mutex, and triggers saveableTexture->loadTexture for
                // the real loading.
                SaveableTexture* saveableTexture = creator(
                        this, [globalName](SaveableTexture* saveableTexture) {
                            s_textureLoader.get()->loadTexture(
                                    globalName,
                                    [saveableTexture](
                                            android::base::Stream* stream) {
                                        saveableTexture->loadFromStream(stream);
                                    });
                        });
                return std::make_pair(globalName,
                                      SaveableTexturePtr(saveableTexture));
            });
}

void GlobalNameSpace::postLoad(android::base::Stream* stream) {
    decltype(m_textureMap)().swap(m_textureMap);
}

const SaveableTexturePtr& GlobalNameSpace::getSaveableTextureFromLoad(
        unsigned int oldGlobalName) {
    assert(m_textureMap.count(oldGlobalName));
    return m_textureMap[oldGlobalName];
}


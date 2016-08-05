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
#include <GLcommon/objectNameManager.h>
#include <GLcommon/ObjectNameSpace.h>
#include <GLcommon/GLEScontext.h>

#include <utility>

namespace {
// A struct serving as a key in a hash table, represents an object name with
// object type together.
struct TypedObjectName {
    ObjectLocalName name;
    NamedObjectType type;

    TypedObjectName(NamedObjectType type, ObjectLocalName name)
        : name(name), type(type) {}

    bool operator==(const TypedObjectName& other) const noexcept {
        return name == other.name && type == other.type;
    }
};
}  // namespace

namespace std {
template <>
struct hash<TypedObjectName> {
    size_t operator()(const TypedObjectName& tn) const noexcept {
        return hash<int>()(tn.name) ^
               hash<ObjectLocalName>()(static_cast<int>(tn.type));
    }
};
}  // namespace std

using ObjectDataMap = std::unordered_map<TypedObjectName, ObjectDataPtr>;
using TextureRefCounterMap = std::unordered_map<unsigned int, size_t>;

ShareGroup::ShareGroup(GlobalNameSpace *globalNameSpace) {
    for (int i = 0; i < static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES);
         i++) {
        m_nameSpace[i] =
                new NameSpace(static_cast<NamedObjectType>(i), globalNameSpace);
    }
}

ShareGroup::~ShareGroup()
{
    emugl::Mutex::AutoLock _lock(m_lock);
    for (int t = 0; t < static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES);
         t++) {
        delete m_nameSpace[t];
    }

    delete (ObjectDataMap *)m_objectsData;
    delete (TextureRefCounterMap *)m_globalTextureRefCounter;
}

ObjectLocalName
ShareGroup::genName(GenNameInfo genNameInfo,
                    ObjectLocalName p_localName,
                    bool genLocal)
{
    if (static_cast<int>(genNameInfo.m_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES))
        return 0;

    emugl::Mutex::AutoLock _lock(m_lock);
    ObjectLocalName localName =
            m_nameSpace[static_cast<int>(genNameInfo.m_type)]->genName(
                                                    genNameInfo,
                                                    p_localName, genLocal);
    return localName;
}

ObjectLocalName ShareGroup::genName(NamedObjectType namedObjectType,
                                    ObjectLocalName p_localName,
                                    bool genLocal) {
    return genName(GenNameInfo(namedObjectType), p_localName, genLocal);
}

ObjectLocalName ShareGroup::genName(ShaderProgramType shaderProgramType,
                                    ObjectLocalName p_localName,
                                    bool genLocal) {
    return genName(GenNameInfo(shaderProgramType), p_localName, genLocal);
}

unsigned int
ShareGroup::getGlobalName(NamedObjectType p_type,
                          ObjectLocalName p_localName)
{
    if (static_cast<int>(p_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES)) {
        return 0;
    }

    emugl::Mutex::AutoLock _lock(m_lock);
    return m_nameSpace[static_cast<int>(p_type)]->getGlobalName(p_localName);
}

ObjectLocalName
ShareGroup::getLocalName(NamedObjectType p_type,
                         unsigned int p_globalName)
{
    if (static_cast<int>(p_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES)) {
        return 0;
    }

    emugl::Mutex::AutoLock _lock(m_lock);
    return m_nameSpace[static_cast<int>(p_type)]->getLocalName(p_globalName);
}

NamedObjectPtr ShareGroup::getNamedObject(NamedObjectType p_type,
                                          ObjectLocalName p_localName) {
    if (static_cast<int>(p_type) >= 
            static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES)) {
        return 0;
    }

    emugl::Mutex::AutoLock _lock(m_lock);
    return m_nameSpace[static_cast<int>(p_type)]->getNamedObject(p_localName);
}

void
ShareGroup::deleteName(NamedObjectType p_type, ObjectLocalName p_localName)
{
    if (static_cast<int>(p_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES))
        return;

    emugl::Mutex::AutoLock _lock(m_lock);
    m_nameSpace[static_cast<int>(p_type)]->deleteName(p_localName);
    ObjectDataMap *map = (ObjectDataMap *)m_objectsData;
    if (map) {
        map->erase(TypedObjectName(p_type, p_localName));
    }
}

bool
ShareGroup::isObject(NamedObjectType p_type, ObjectLocalName p_localName)
{
    if (static_cast<int>(p_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES))
        return 0;

    emugl::Mutex::AutoLock _lock(m_lock);
    return m_nameSpace[static_cast<int>(p_type)]->isObject(p_localName);
}

void
ShareGroup::replaceGlobalObject(NamedObjectType p_type,
                              ObjectLocalName p_localName,
                              NamedObjectPtr p_globalObject)
{
    if (static_cast<int>(p_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES))
        return;

    emugl::Mutex::AutoLock _lock(m_lock);
    m_nameSpace[static_cast<int>(p_type)]->replaceGlobalObject(p_localName,
                                                               p_globalObject);
}

void
ShareGroup::setObjectData(NamedObjectType p_type,
                          ObjectLocalName p_localName,
                          ObjectDataPtr data)
{
    if (static_cast<int>(p_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES))
        return;

    emugl::Mutex::AutoLock _lock(m_lock);

    ObjectDataMap *map = (ObjectDataMap *)m_objectsData;
    if (!map) {
        map = new ObjectDataMap();
        m_objectsData = map;
    }

    TypedObjectName id(p_type, p_localName);
    map->emplace(id, std::move(data));
}

ObjectDataPtr
ShareGroup::getObjectData(NamedObjectType p_type,
                          ObjectLocalName p_localName)
{
    ObjectDataPtr ret;

    if (static_cast<int>(p_type) >=
        static_cast<int>(NamedObjectType::NUM_OBJECT_TYPES))
        return ret;

    emugl::Mutex::AutoLock _lock(m_lock);

    ObjectDataMap *map = (ObjectDataMap *)m_objectsData;
    if (map) {
        ObjectDataMap::iterator i =
                map->find(TypedObjectName(p_type, p_localName));
        if (i != map->end()) ret = (*i).second;
    }
    return ret;
}

ObjectNameManager::ObjectNameManager(GlobalNameSpace *globalNameSpace) :
    m_globalNameSpace(globalNameSpace) {}

ShareGroupPtr
ObjectNameManager::createShareGroup(void *p_groupName)
{
    emugl::Mutex::AutoLock _lock(m_lock);

    ShareGroupPtr shareGroupReturn;

    ShareGroupsMap::iterator s( m_groups.find(p_groupName) );
    if (s != m_groups.end()) {
        shareGroupReturn = (*s).second;
    } else {
        //
        // Group does not exist, create new group
        //
        shareGroupReturn = ShareGroupPtr(new ShareGroup(m_globalNameSpace));
        m_groups.emplace(p_groupName, shareGroupReturn);
    }

    return shareGroupReturn;
}

ShareGroupPtr
ObjectNameManager::getShareGroup(void *p_groupName)
{
    emugl::Mutex::AutoLock _lock(m_lock);

    ShareGroupPtr shareGroupReturn;

    ShareGroupsMap::iterator s( m_groups.find(p_groupName) );
    if (s != m_groups.end()) {
        shareGroupReturn = (*s).second;
    }

    return shareGroupReturn;
}

ShareGroupPtr
ObjectNameManager::attachShareGroup(void *p_groupName,
                                    void *p_existingGroupName)
{
    emugl::Mutex::AutoLock _lock(m_lock);

    ShareGroupsMap::iterator s( m_groups.find(p_existingGroupName) );
    if (s == m_groups.end()) {
        // ShareGroup is not found !!!
        return ShareGroupPtr();
    }

    ShareGroupPtr shareGroupReturn((*s).second);
    if (m_groups.find(p_groupName) == m_groups.end()) {
        m_groups.emplace(p_groupName, shareGroupReturn);
    }
    return shareGroupReturn;
}

void
ObjectNameManager::deleteShareGroup(void *p_groupName)
{
    emugl::Mutex::AutoLock _lock(m_lock);

    ShareGroupsMap::iterator s( m_groups.find(p_groupName) );
    if (s != m_groups.end()) {
        m_groups.erase(s);
    }
}

void *ObjectNameManager::getGlobalContext()
{
    emugl::Mutex::AutoLock _lock(m_lock);
    return m_groups.empty() ? nullptr : m_groups.begin()->first;
}

// Copyright 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/metrics/StudioHelper.h"

#include "android/base/files/PathUtils.h"
#include "android/base/misc/StringUtils.h"
#include "android/base/system/System.h"
#include "android/base/Version.h"
#include "android/emulation/ConfigDirs.h"
#include "android/metrics/studio-helper.h"
#include "android/utils/debug.h"
#include "android/utils/dirscanner.h"
#include "android/utils/path.h"

#include <libxml/tree.h>

#include <fstream>

#include <stdlib.h>
#include <string.h>

#define E(...) derror(__VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define D(...) VERBOSE_PRINT(init, __VA_ARGS__)

/***************************************************************************/
// These consts are replicated as macros in StudioHelper_unittest.cpp
// changes to these will require equivalent changes to the unittests
//
static const char kAndroidStudioUuidDir[] = "JetBrains";
static const char kAndroidStudioUuid[] = "PermanentUserID";

#ifdef __APPLE__
static const char kAndroidStudioDir[] = "AndroidStudio";
#else   // ! ___APPLE__
static const char kAndroidStudioDir[] = ".AndroidStudio";
static const char kAndroidStudioDirInfix[] = "config";
#endif  // __APPLE__

static const char kAndroidStudioDirSuffix[] = "options";
static const char kAndroidStudioDirPreview[] = "Preview";
static const char kAndroidStudioUuidHexPattern[] =
        "00000000-0000-0000-0000-000000000000";

// StudioXML describes the XML parameters we are seeking for in a
// Studio preferences file. StudioXml structs are statically
// defined and used in  android_studio_get_installation_id()
// and android_studio_get_optins().
typedef struct {
    const char* filename;
    const char* nodename;
    const char* propname;
    const char* propvalue;
    const char* keyname;
} StudioXml;

/***************************************************************************/

using namespace android;
using namespace android::base;

// static
const Version StudioHelper::extractAndroidStudioVersion(
        const char* const dirName) {
    if (dirName == NULL) {
        return Version::invalid();
    }

    // get rid of kAndroidStudioDir prefix to get to version
    if (strncmp(dirName,
                kAndroidStudioDir,
                sizeof(kAndroidStudioDir) - 1) != 0) {
        return Version::invalid();
    }

    const char* cVersion = dirName + sizeof(kAndroidStudioDir) - 1;

    // if this is a preview, get rid of kAndroidStudioDirPreview
    // prefix too and mark preview as build #1
    // (assume build #2 for releases)
    auto build = 2;
    if (strncmp(cVersion,
                kAndroidStudioDirPreview,
                sizeof(kAndroidStudioDirPreview) - 1) == 0) {
        cVersion += sizeof(kAndroidStudioDirPreview) - 1;
        build = 1;
    }

    // cVersion should now contain at least a number; if not,
    // this is a very early AndroidStudio installation, let's
    // call it version 0
    if (cVersion[0] == '\0') {
        cVersion = "0";
    }

    // Create a major.micro.minor version string and append
    // a "2" for release and a "1" for previews; this will
    // allow proper sorting
    Version rawVersion(cVersion);
    if (rawVersion == Version::invalid()) {
        return Version::invalid();
    }

    return Version(rawVersion.component<Version::kMajor>(),
                   rawVersion.component<Version::kMinor>(),
                   rawVersion.component<Version::kMicro>(),
                   build);
}

// static
std::string StudioHelper::latestAndroidStudioDir(const std::string& scanPath) {
    std::string latest_path;

    if (scanPath.empty()) {
        return latest_path;
    }

    DirScanner* scanner = dirScanner_new(scanPath.c_str());
    if (scanner == NULL) {
        return latest_path;
    }

    System* system = System::get();
    Version latest_version(0, 0, 0);

    for (;;) {
        const char* full_path = dirScanner_nextFull(scanner);
        if (full_path == NULL) {
            // End of enumeration.
            break;
        }
        // ignore files, only interested in subDirs
        if (!system->pathIsDir(full_path)) {
            continue;
        }

        char* name = path_basename(full_path);
        if (name == NULL) {
            continue;
        }
        Version v = extractAndroidStudioVersion(name);
        free(name);
        if (v.isValid() && latest_version < v) {
            latest_version = v;
            latest_path = std::string(full_path);
        }
    }

    return latest_path;
}

// static
std::string StudioHelper::pathToStudioXML(const std::string& studioPath,
                                          const std::string& filename) {
    if (studioPath.empty() || filename.empty())
        return std::string();

    // build /path/to/.AndroidStudio/subpath/to/file.xml
    std::vector<std::string> vpath;
    vpath.push_back(studioPath);
#ifndef __APPLE__
    vpath.push_back(std::string(kAndroidStudioDirInfix));
#endif  // !__APPLE__
    vpath.push_back(std::string(kAndroidStudioDirSuffix));
    vpath.push_back(filename);
    return PathUtils::recompose(vpath);
}

#ifdef _WIN32
// static
std::string StudioHelper::pathToStudioUUIDWindows() {
    System* sys = System::get();
    std::string appDataPath = sys->getAppDataDirectory();

    std::string retval;
    if (!appDataPath.empty()) {
        // build /path/to/APPDATA/subpath/to/StudioUUID file
        std::vector<std::string> vpath;
        vpath.push_back(appDataPath);
        vpath.push_back(std::string(kAndroidStudioUuidDir));
        vpath.push_back(std::string(kAndroidStudioUuid));

        retval = PathUtils::recompose(vpath);
    }

    return retval;
}
#endif

/*****************************************************************************/

// Recurse through studio xml doc and return the value described in match
// as a string, if one is set. Otherwise, return an empty string
//
static std::string eval_studio_config_xml(xmlDocPtr doc,
                                          xmlNodePtr root,
                                          const StudioXml* const match) {
    xmlNodePtr current = NULL;
    xmlChar* xmlval = NULL;
    std::string retVal;

    for (current = root; current; current = current->next) {
        if (current->type == XML_ELEMENT_NODE) {
            if ((!xmlStrcmp(current->name, BAD_CAST match->nodename))) {
                xmlChar* propvalue =
                        xmlGetProp(current, BAD_CAST match->propname);
                int nomatch = xmlStrcmp(propvalue, BAD_CAST match->propvalue);
                xmlFree(propvalue);
                if (!nomatch) {
                    xmlval = xmlGetProp(current, BAD_CAST match->keyname);
                    if (xmlval != NULL) {
                        // xmlChar* is defined as unsigned char
                        // we are simply reading the result and don't
                        // operate on it, soe simply reinterpret as
                        // as char * array
                        retVal = std::string(reinterpret_cast<char*>(xmlval));
                        xmlFree(xmlval);
                        break;
                    }
                }
            }
        }
        retVal = eval_studio_config_xml(doc, current->children, match);
        if (!retVal.empty()) {
            break;
        }
    }

    return retVal;
}

// Find latest studio preferences directory and return the
// value of the xml entry described in |match|.
//
static std::string parseStudioXML(const StudioXml* const match) {
    std::string retval;

    System* sys = System::get();
    // Get path to .AndroidStudio
    std::string appDataPath;
    std::string studio = sys->envGet("ANDROID_STUDIO_PREFERENCES");
    if (studio.empty()) {
#ifdef __APPLE__
        appDataPath = sys->getAppDataDirectory();
#else
        appDataPath = sys->getHomeDirectory();
#endif  // __APPLE__
        if (appDataPath.empty()) {
            return retval;
        }
        studio = StudioHelper::latestAndroidStudioDir(appDataPath);
    }
    if (studio.empty()) {
        return retval;
    }

    // Find match->filename xml file under .AndroidStudio
    std::string xml_path =
            StudioHelper::pathToStudioXML(studio, std::string(match->filename));
    if (xml_path.empty()) {
        D("Failed to find %s in %s", match->filename, studio.c_str());
        return retval;
    }

    xmlDocPtr doc = xmlReadFile(xml_path.c_str(), NULL, 0);
    if (doc == NULL)
        return retval;

    xmlNodePtr root = xmlDocGetRootElement(doc);
    retval = eval_studio_config_xml(doc, root, match);

    xmlFreeDoc(doc);

    return retval;
}

#ifdef _WIN32
static std::string android_studio_get_Windows_uuid() {
    // Appropriately sized container for UUID
    std::string uuid_file_path = StudioHelper::pathToStudioUUIDWindows();
    std::string retval;

    // Read UUID from file
    std::ifstream uuid_file(uuid_file_path.c_str());
    if (uuid_file) {
        std::string line;
        std::getline(uuid_file, line);
        retval = std::string(line.c_str());
    }

    return retval;
}
#endif  // _WIN32

/*****************************************************************************/

// Get the status of user opt-in to crash reporting in AndroidStudio
// preferences. Returns 1 only if the user has opted-in, 0 otherwise.
//
int android_studio_get_optins() {
    int retval = 0;

    static const StudioXml optins = {
            .filename = "usage.statistics.xml",
            .nodename = "component",
            .propname = "name",
            .propvalue = "UsagesStatistic",
            .keyname = "allowed",  // assuming "true"/"false" string values
    };

    std::string xmlVal = parseStudioXML(&optins);
    if (xmlVal.empty()) {
        D("Failed to parse %s preferences file %s", kAndroidStudioDir,
          optins.filename);
        D("Defaulting user crash-report opt-in to false");
        return 0;
    }

    // return 0 if user has not opted in to crash reports
    if (xmlVal == "true") {
        retval = 1;
    } else if (xmlVal == "false") {
        retval = 0;
    } else {
        D("Invalid value set in %s preferences file %s", kAndroidStudioDir,
          optins.filename);
    }

    return retval;
}

static std::string android_studio_get_installation_id_legacy() {
    std::string retval;
#ifndef __WIN32
    static const StudioXml uuid = {
            .filename = "options.xml",
            .nodename = "property",
            .propname = "name",
            .propvalue = "installation.uid",
            .keyname = "value",  // assuming kAndroidStudioUuidHexPattern
    };
    retval = parseStudioXML(&uuid);

    if (retval.empty()) {
        D("Failed to parse %s preferences file %s", kAndroidStudioDir,
          uuid.filename);
    }
#else
    // In Microsoft Windows, getting Android Studio installation
    // ID requires searching in completely different path than the
    // rest of Studio preferences ...
    retval = android_studio_get_Windows_uuid();
    if (retval.empty()) {
        D("Failed to parse %s preferences file %s", kAndroidStudioDir,
          kAndroidStudioUuid);
    }
#endif
    return retval;
}

// Get the installation.id reported by Android Studio (string).
// If there is no Android Studio installation or a value
// cannot be retrieved, a fixed dummy UUID will be returned.  Caller is
// responsible for freeing returned string.
char* android_studio_get_installation_id() {
    std::string uuid_path =
            PathUtils::join(ConfigDirs::getUserDirectory(), "uid.txt");
    std::ifstream uuid_file(uuid_path.c_str());
    if (uuid_file) {
        std::string line;
        std::getline(uuid_file, line);
        if (!line.empty()) {
            return strdup(line.c_str());
        }
    }

    // Couldn't find uuid in the android specific location. Try legacy uuid
    // locations.
    auto uuid = android_studio_get_installation_id_legacy();
    if (!uuid.empty()) {
        return android::base::strDup(uuid);
    }

    D("Defaulting to zero installation ID");
    return strdup(kAndroidStudioUuidHexPattern);
}

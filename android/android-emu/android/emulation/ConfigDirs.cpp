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

#include "android/emulation/ConfigDirs.h"

#include "android/base/files/PathUtils.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/base/misc/StringUtils.h"
#include "android/base/system/System.h"

#include <assert.h>

namespace android {

using ::android::base::PathUtils;
using ::android::base::ScopedCPtr;
using ::android::base::System;

// static
std::string ConfigDirs::getUserDirectory() {
    // Name of the Android configuration directory under $HOME.
    static const char kAndroidSubDir[] = ".android";
    System* system = System::get();
    std::string home = system->envGet("ANDROID_EMULATOR_HOME");
    if (home.size()) {
        return home;
    }
    home = system->envGet("ANDROID_SDK_HOME");
    if (home.size()) {
        return PathUtils::join(home, kAndroidSubDir);
    }
    home = system->getHomeDirectory();
    if (home.empty()) {
        home = system->getTempDir();
        if (home.empty()) {
            home = "/tmp";
        }
    }
    return PathUtils::join(home, kAndroidSubDir);
}

// static
std::string ConfigDirs::getAvdRootDirectory() {
    static const char kAndroidSubDir[] = ".android";
    static const char kAvdSubDir[] = "avd";
    System* system = System::get();

    std::string avdRoot = system->envGet("ANDROID_AVD_HOME");
    if ( !avdRoot.empty() && system->pathIsDir(avdRoot) ) {
        return avdRoot;
    }

    // No luck with ANDROID_AVD_HOME, try ANDROID_SDK_HOME
    avdRoot = system->envGet("ANDROID_SDK_HOME");

    if ( !avdRoot.empty() ) {
        // ANDROID_SDK_HOME is defined
        avdRoot = PathUtils::join(avdRoot, kAndroidSubDir);
        if (isValidAvdRoot(avdRoot)) {
            // ANDROID_SDK_HOME is good
            return PathUtils::join(avdRoot, kAvdSubDir);
        }
        // ANDROID_SDK_HOME is defined but bad. In this case,
        // Android Studio tries $USER_HOME and $HOME. We'll
        // do the same.
        avdRoot = system->envGet("USER_HOME");
        if ( !avdRoot.empty() ) {
            avdRoot = PathUtils::join(avdRoot, kAndroidSubDir);
            if (isValidAvdRoot(avdRoot)) {
                return PathUtils::join(avdRoot, kAvdSubDir);
            }
        }
        avdRoot = system->envGet("HOME");
        if ( !avdRoot.empty() ) {
            avdRoot = PathUtils::join(avdRoot, kAndroidSubDir);
            if (isValidAvdRoot(avdRoot)) {
                return PathUtils::join(avdRoot, kAvdSubDir);
            }
        }
    }

    // No luck with ANDROID_SDK_HOME / USER_HOME / HOME
    // Try even more.
    avdRoot = PathUtils::join(getUserDirectory(), kAvdSubDir);
    return avdRoot;
}

// static
std::string ConfigDirs::getSdkRootDirectoryByEnv() {
    auto system = System::get();

    std::string sdkRoot = system->envGet("ANDROID_HOME");
    if ( isValidSdkRoot(sdkRoot) ) {
        return sdkRoot;
    }

    // ANDROID_HOME is not good. Try ANDROID_SDK_ROOT.
    sdkRoot = system->envGet("ANDROID_SDK_ROOT");
    if (sdkRoot.size()) {
        // Unquote a possibly "quoted" path.
        if (sdkRoot[0] == '"') {
            assert(sdkRoot[sdkRoot.size() - 1] == '"');
            // std::string does not support assigning a part of the string to
            // itself.
            auto copySize = sdkRoot.size() - 2;
            ScopedCPtr<char> buf(android::base::strDup(sdkRoot));
            sdkRoot.assign(buf.get() + 1, copySize);
        }
        if (isValidSdkRoot(sdkRoot)) {
            return sdkRoot;
        }
    }
    return std::string();
}

std::string ConfigDirs::getSdkRootDirectoryByPath() {
    auto system = System::get();

    auto parts = PathUtils::decompose(system->getLauncherDirectory());
    parts.push_back("..");
    PathUtils::simplifyComponents(&parts);

    std::string sdkRoot = PathUtils::recompose(parts);
    if ( isValidSdkRoot(sdkRoot) ) {
        return sdkRoot;
    }
    return std::string();
}

// static
std::string ConfigDirs::getSdkRootDirectory() {
    std::string sdkRoot = getSdkRootDirectoryByEnv();
    if (!sdkRoot.empty()) {
        return sdkRoot;
    }

    // Otherwise, infer from the path of the emulator's binary.
    return getSdkRootDirectoryByPath();
}

// static
bool ConfigDirs::isValidSdkRoot(const android::base::StringView& rootPath) {
    if (rootPath.empty()) {
        return false;
    }
    System* system = System::get();
    if ( !system->pathIsDir(rootPath) || !system->pathCanRead(rootPath) ) {
        return false;
    }
    std::string platformsPath = PathUtils::join(rootPath, "platforms");
    if ( !system->pathIsDir(platformsPath) ) {
        return false;
    }
    std::string platformToolsPath = PathUtils::join(rootPath, "platform-tools");
    if ( !system->pathIsDir(platformToolsPath) ) {
        return false;
    }

    return true;
}

// static
bool ConfigDirs::isValidAvdRoot(const android::base::StringView& avdPath) {
    if (avdPath.empty()) {
        return false;
    }
    System* system = System::get();
    if ( !system->pathIsDir(avdPath) || !system->pathCanRead(avdPath) ) {
        return false;
    }
    std::string avdAvdPath = PathUtils::join(avdPath, "avd");
    if ( !system->pathIsDir(avdAvdPath) ) {
        return false;
    }

    return true;
}

}  // namespace android

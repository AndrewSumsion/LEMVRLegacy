// Copyright 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/update-check/UpdateChecker.h"

#include "android/base/files/PathUtils.h"
#include "android/base/system/System.h"
#include "android/update-check/update_check.h"
#include "android/utils/debug.h"
#include "android/utils/filelock.h"
#include "android/utils/misc.h"

#include <curl/curl.h>

#include <errno.h>
#include <string.h>
#include <time.h>

#include <fstream>
#include <memory>
#include <new>
#include <string>

#define STRINGIFY(x) _STRINGIFY(x)
#define _STRINGIFY(x) #x

#ifdef ANDROID_SDK_TOOLS_REVISION
#define SDK_VERSION_STRING STRINGIFY(ANDROID_SDK_TOOLS_REVISION)
#else
#define SDK_VERSION_STRING "standalone"
#endif

static const char kDataFileName[] = ".emu-update-last-check";
static const char kVersionUrl[] =
        "https://dl.google.com/android/repository/repository-10.xml";

static const char kNewerVersionMessage[] =
        "Your emulator is out of date, please update by launching Android "
        "Studio";

using android::base::Version;
using android::update_check::UpdateChecker;

void android_checkForUpdates(const char* homePath) {
    std::auto_ptr<UpdateChecker> checker(new (std::nothrow)
                                                 UpdateChecker(homePath));

    if (checker->init() && checker->needsCheck() && checker->runAsyncCheck()) {
        // checker will delete itself after the check in the worker thread
        checker.release();
    } else {
        dprint("UpdateChecker: skipped version check");
    }
}

namespace android {
namespace update_check {

static int parseXmlValue(const char* name,
                         const std::string& xml,
                         size_t offset) {
    const size_t pos = xml.find(name, offset);
    if (pos == std::string::npos) {
        dwarning("UpdateCheck: can't find version attribute '%s'", name);
        return -1;
    }
    char* end;
    const int value = strtoi(xml.c_str() + pos + strlen(name), &end, 10);
    if (errno || *end != '<') {
        dwarning("UpdateCheck: invalid value of a version attribute '%s'",
                 name);
        return -1;
    }
    return value;
}

class VersionExtractor : public IVersionExtractor {
public:
    virtual android::base::Version extractVersion(const std::string& data) {
        // quick and dirty
        // TODO: use libxml2 to properly parse XML and extract node value

        // find the last tool item in the xml
        const size_t toolPos = data.rfind("<sdk:tool>");
        if (toolPos == std::string::npos) {
            return Version::Invalid();
        }

        const int major = parseXmlValue("<sdk:major>", data, toolPos);
        const int minor = parseXmlValue("<sdk:minor>", data, toolPos);
        const int micro = parseXmlValue("<sdk:micro>", data, toolPos);
        if (major < 0 || minor < 0 || micro < 0) {
            return Version::Invalid();
        }

        return Version(major, minor, micro);
    }

    virtual android::base::Version getCurrentVersion() const {
        static const Version currentVersion = Version(SDK_VERSION_STRING);
        return currentVersion;
    }
};

static size_t curlWriteCallback(void* contents,
                                size_t size,
                                size_t nmemb,
                                void* userp) {
    std::string* const xml = static_cast<std::string*>(userp);
    const size_t total = size * nmemb;

    xml->insert(xml->end(), static_cast<char*>(contents),
                static_cast<char*>(contents) + total);

    return total;
}

class DataLoader : public IDataLoader {
public:
    virtual std::string load() {
        std::string xml;
        if (CURL* const curl = curl_easy_init()) {
            curl_easy_setopt(curl, CURLOPT_URL, kVersionUrl);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curlWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &xml);
            const CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                dwarning("UpdateCheck: failed to get an URL: %d (%s)\n", res,
                         curl_easy_strerror(res));
            }
            curl_easy_cleanup(curl);
        }

        return xml;
    }
};

class TimeStorage : public ITimeStorage {
public:
    TimeStorage(const char* configPath) : mFileLock(NULL) {
        mDataFileName =
                android::base::PathUtils::addTrailingDirSeparator(configPath)
                        .append(kDataFileName);
    }

    ~TimeStorage() {
        if (mFileLock) {
            filelock_release(mFileLock);
        }
    }

    bool lock() {
        if (mFileLock) {
            dwarning("UpdateCheck: lock() called twice by the same process");
            return true;
        }

        mFileLock = filelock_create(mDataFileName.c_str());
        // if someone's already checking it - don't do it twice
        return mFileLock != NULL;
    }

    time_t getTime() {
        std::ifstream dataFile(mDataFileName.c_str());
        if (!dataFile) {
            return time_t();  // no data file
        }
        int64_t intTime = 0;
        dataFile >> intTime;
        return static_cast<time_t>(intTime);
    }

    void setTime(time_t time) {
        std::ofstream dataFile(mDataFileName.c_str());
        if (!dataFile) {
            dwarning("UpdateCheck: couldn't open data file for writing");
        } else {
            dataFile << static_cast<int64_t>(time) << '\n';
        }
    }

private:
    android::base::String mDataFileName;
    FileLock* mFileLock;
};

class NewerVersionReporter : public INewerVersionReporter {
public:
    virtual void reportNewerVersion(const android::base::Version& /*existing*/,
                                    const android::base::Version& /*newer*/) {
        printf("%s\n", kNewerVersionMessage);
    }
};

UpdateChecker::UpdateChecker(const char* configPath)
    : mThread(this),
      mVersionExtractor(new VersionExtractor()),
      mDataLoader(new DataLoader()),
      mTimeStorage(new TimeStorage(configPath)),
      mReporter(new NewerVersionReporter()) {}

UpdateChecker::UpdateChecker(IVersionExtractor* extractor,
                             IDataLoader* loader,
                             ITimeStorage* storage,
                             INewerVersionReporter* reporter)
    : mThread(this),
      mVersionExtractor(extractor),
      mDataLoader(loader),
      mTimeStorage(storage),
      mReporter(reporter) {}

bool UpdateChecker::init() {
    if (!mTimeStorage->lock()) {
        return false;
    }

    return true;
}

bool UpdateChecker::needsCheck() const {
    const time_t now = android::base::System::get()->getUnixTime();
    // Check only if the date of previous check is before today date
    return clearHMS(mTimeStorage->getTime()) < clearHMS(now);
}

bool UpdateChecker::runAsyncCheck() {
    return mThread.start();
}

time_t UpdateChecker::clearHMS(time_t t) {
    tm local = *localtime(&t);

    // clear the time part
    local.tm_hour = local.tm_min = local.tm_sec = 0;

    return mktime(&local);
}

void UpdateChecker::asyncWorker() {
    Version current = mVersionExtractor->getCurrentVersion();
    Version last = loadLatestVersion();

    dprint("UpdateCheck: current version '%s', last version '%s'",
           current.toString().c_str(), last.toString().c_str());

    if (current < last) {
        mReporter->reportNewerVersion(current, last);
    }

    // Update the last version check time
    mTimeStorage->setTime(android::base::System::get()->getUnixTime());
}

Version UpdateChecker::loadLatestVersion() {
    const std::string xml = mDataLoader->load();
    const Version ver = mVersionExtractor->extractVersion(xml);
    return ver;
}

}  // namespace update_check
}  // namespace android

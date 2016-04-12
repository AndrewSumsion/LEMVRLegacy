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

#pragma once

#include "android/base/threads/Thread.h"
#include "android/base/Version.h"
#include "android/update-check/IVersionExtractor.h"

#include <memory>
#include <string>
#include <time.h>

namespace android {
namespace update_check {

// Set of interfaces for the operations UpdateChecker performs:
//  IDataLoader - download the xml manifest from the Web
//  ITimeStorage - lock the file to store last update checking time,
//                 save/load time from the file
//  INewerVersionReporter - report to the user about available newer version

class IDataLoader {
public:
    virtual ~IDataLoader() {}
    virtual std::string load(const char* version) = 0;
};

class ITimeStorage {
public:
    virtual ~ITimeStorage() {}

    virtual bool lock() = 0;
    virtual time_t getTime() = 0;
    virtual void setTime(time_t time) = 0;
};

class INewerVersionReporter {
public:
    virtual ~INewerVersionReporter() {}

    virtual void reportNewerVersion(const android::base::Version& existing,
                                    const android::base::Version& newer) = 0;
};

// UpdateChecker - Class that incapsulates the logic for update checking
//  once a day. It runs the check asynchronously and only if there was no
//  check performed today.
//  The class can also return the version that it discovered is available.
//
// Usage:
//   To check the version daily:
//     Create instance of the class dynamically, and if init() and
//     needsCheck() return true, call runAsyncCheck(). It will do the
//     checking and delete itself if runAsyncCheck() returned true.
//     If any of the calls returned false, one has to delete the object itself.
//
//   To get the version discoved by the last 'runAsyncCheck()':
//     Create an instance of the class and call getLatestVersion().

class UpdateChecker {
public:
    // |configPath| is the path to the emulator configuration directory
    // where the checker can store its records about last check time
    // |coreVersion| is the application's core version (e.g. qemu2 2.2.0)
    //      'nullptr' means 'don't send any emulator-specific information in the
    //      request, just check the version
    explicit UpdateChecker(const char* configPath,
                           const char* coreVersion = nullptr);

    bool init();

    bool needsCheck() const;

    bool runAsyncCheck();

    android::base::Version getLatestVersion();

protected:
    // constructor for tests
    UpdateChecker(IVersionExtractor*,
                  IDataLoader*,
                  ITimeStorage*,
                  INewerVersionReporter*);

    void asyncWorker();

private:
    const char* mCoreVersion;
    std::unique_ptr<IVersionExtractor> mVersionExtractor;
    std::unique_ptr<IDataLoader> mDataLoader;
    std::unique_ptr<ITimeStorage> mTimeStorage;
    std::unique_ptr<INewerVersionReporter> mReporter;
};

}  // namespace update_check
}  // namespace android

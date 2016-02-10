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

#include "android/crashreport/CrashReporter.h"

#include "android/crashreport/crash-handler.h"
#include "android/base/containers/StringVector.h"
#include "android/base/files/PathUtils.h"
#include "android/base/system/System.h"
#include "android/base/system/Win32UnicodeString.h"
#include "android/base/Uuid.h"
#include "android/metrics/metrics_reporter.h"
#include "android/utils/debug.h"
#include "android/utils/eintr_wrapper.h"
#include "android/utils/path.h"

#include <fstream>

#include <fcntl.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#endif

#define E(...) derror(__VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define D(...) VERBOSE_PRINT(init, __VA_ARGS__)
#define I(...) printf(__VA_ARGS__)

using android::base::PathUtils;
using android::base::String;
using android::base::StringView;
using android::base::System;
using android::base::Uuid;

namespace android {
namespace crashreport {

const char* const CrashReporter::kDumpMessageFileName =
        "internal-error-msg.txt";
const char* const CrashReporter::kProcessMemoryInfoFileName =
        "process-memory-info.txt";
const char* const CrashReporter::kCrashOnExitFileName =
        "crash-on-exit.txt";

const char* const CrashReporter::kCrashOnExitPattern =
        "Crash on exit";

const char* const CrashReporter::kProcessCrashesQuietlyKey =
        "set/processCrashesQuietly";

CrashReporter::CrashReporter()
    : mDumpDir(System::get()->getTempDir().c_str()),
      // TODO: add a function that can create a directory or error-out
      // if it exists atomically. For now let's just allow UUIDs to do their
      // job to keep these unique
      mDataExchangeDir(
              PathUtils::join(mDumpDir, Uuid::generateFast().toString())
                      .c_str()) {
    const auto res = path_mkdir_if_needed(mDataExchangeDir.c_str(), 0744);
    if (res < 0) {
        E("Failed to create temp directory for crash service communication: "
          "'%s'",
          mDataExchangeDir.c_str());
    }
}

CrashReporter::~CrashReporter() = default;

const std::string& CrashReporter::getDumpDir() const {
    return mDumpDir;
}

const std::string& CrashReporter::getDataExchangeDir() const {
    return mDataExchangeDir;
}

void CrashReporter::GenerateDump(const char* message) {
    passDumpMessage(message);
    writeDump();
}

void CrashReporter::GenerateDumpAndDie(const char* message) {
    passDumpMessage(message);
    // this is the most cross-platform way of crashing
    // any other I know about has its flows:
    //  - abort() isn't caught by Breakpad on Windows
    //  - null() may screw the callstack
    //  - explicit *null = 1 can be optimized out
    //  - requesting dump and exiting later has a very noticable delay in
    //    between, so some real crash could stick in the middle
    volatile int* volatile ptr = nullptr;
    *ptr = 1313;  // die
}

void CrashReporter::SetExitMode(const char* msg) {
    mIsInExitMode = true;
    androidMetrics_update();
    attachData(kCrashOnExitFileName, msg);
}

bool CrashReporter::isInExitMode() const {
    return mIsInExitMode;
}

void CrashReporter::passDumpMessage(const char* message) {
    attachData(kDumpMessageFileName, message);
}

void CrashReporter::attachData(StringView name, StringView data) {
    // don't do any dynamic allocation here - it might be called during dump
    // writing, e.g. because of OOM exception
    char fullName[PATH_MAX + 1] = {};
    snprintf(fullName, sizeof(fullName) - 1,
             "%s%c%s",
             getDataExchangeDir().c_str(), System::kDirSeparator,
             (name.empty() ? "additional_data.txt" : name.c_str()));

    // Open the communication file in append mode to make sure we won't
    // overwrite any existing message (e.g. if several threads are writing at
    // once)
#ifdef _WIN32
    // well, here we do a little of allocation - but just because of the API
    android::base::Win32UnicodeString wideStr(fullName);
    int fd = _wopen(wideStr.c_str(),
                    _O_WRONLY | _O_CREAT | _O_APPEND | _O_NOINHERIT | _O_TEXT,
                    _S_IREAD | _S_IWRITE);
#else
    int fd = HANDLE_EINTR(open(fullName,
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644));
#endif
    if (fd < 0) {
        W("Failed to open a temp file '%s' for writing", fullName);
        return;
    }

    HANDLE_EINTR(write(fd, data.data(), data.size()));
    HANDLE_EINTR(write(fd, "\n", 1));

    close(fd);
}

}  // namespace crashreport
}  // namespace android

using android::crashreport::CrashReporter;
using android::crashreport::CrashSystem;

extern "C" {

bool crashhandler_init() {
    if (CrashSystem::CrashType::CRASHUPLOAD == CrashSystem::CrashType::NONE) {
        return false;
    }

    if (!CrashSystem::get()->validatePaths()) {
        return false;
    }

    CrashSystem::CrashPipe crashpipe(CrashSystem::get()->getCrashPipe());

    const std::string procident = CrashSystem::get()->getProcessId();

    if (procident.empty()) {
        return false;
    }

    if (!crashpipe.isValid()) {
        return false;
    }

    ::android::base::StringVector cmdline =
            CrashSystem::get()->getCrashServiceCmdLine(crashpipe.mServer,
                                                       procident);

    int pid = CrashSystem::spawnService(cmdline);
    if (pid > 0) {
        CrashReporter::get()->setupChildCrashProcess(pid);
    } else {
        W("Could not spawn crash service\n");
        return false;
    }

    if (!CrashReporter::get()->waitServicePipeReady(crashpipe.mClient)) {
        W("Crash service did not start\n");
        return false;
    }

    return CrashReporter::get()->attachCrashHandler(crashpipe);
}

void crashhandler_die(const char* message) {
    if (const auto reporter = CrashReporter::get()) {
        reporter->GenerateDumpAndDie(message);
        // convince the compiler and everyone else that we will never return
        abort();
    } else {
        I("Emulator: exiting becase of the internal error '%s'\n", message);
        _exit(1);
    }
}

void crashhandler_die_format(const char* format, ...) {
    char message[2048] = {};
    va_list args;

    va_start(args, format);
    vsnprintf(message, sizeof(message) - 1, format, args);
    va_end(args);

    crashhandler_die(message);
}

void crashhandler_add_string(const char* name, const char* string) {
    if (const auto reporter = CrashReporter::get()) {
        reporter->attachData(name, string);
    }
}

void crashhandler_exitmode(const char* message) {
    CrashReporter::get()->SetExitMode(message);
}

bool crashhandler_copy_attachment(const char* destination, const char* source) {
    const std::string& path = CrashReporter::get()->getDataExchangeDir();
    if (path.empty()) {
        E("Could not determine crash dump directory");
        return false;
    }
    String dest = PathUtils::join(path, destination);
    if (path_copy_file(dest.c_str(), source) != 0) {
        E("Could not copy file '%s' to '%s': %s", source, dest.c_str(),
          strerror(errno));
        return false;
    }
    return true;
}

}  // extern "C"


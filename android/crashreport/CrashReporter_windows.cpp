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

#include "android/base/memory/LazyInstance.h"
#include "android/base/system/System.h"
#include "android/base/system/Win32UnicodeString.h"
#include "android/utils/debug.h"
#include "client/windows/handler/exception_handler.h"

#include <psapi.h>

#include <memory>

#include <inttypes.h>
#include <stdint.h>

#define E(...) derror(__VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define D(...) VERBOSE_PRINT(init, __VA_ARGS__)
#define I(...) printf(__VA_ARGS__)

namespace android {
namespace crashreport {

namespace {

class HostCrashReporter : public CrashReporter {
public:
    HostCrashReporter() : CrashReporter(), mHandler() {}

    virtual ~HostCrashReporter() {}

    bool attachCrashHandler(const CrashSystem::CrashPipe& crashpipe) override {
        if (mHandler) {
            return false;
        }

        ::android::base::Win32UnicodeString dumpDir(getDumpDir().c_str(),
                                                    getDumpDir().length());
        std::wstring dumpDir_wstr(dumpDir.c_str());

        ::android::base::Win32UnicodeString crashPipe(crashpipe.mClient.c_str(),
                                                    crashpipe.mClient.length());

        // google_breakpad::ExceptionHandler makes a local copy of dumpDir arg.
        // crashPipe arg is copied locally during ExceptionHandler's construction
        // of CrashGenerationClient.
        mHandler.reset(new google_breakpad::ExceptionHandler(
                dumpDir_wstr, &HostCrashReporter::exceptionFilterCallback,
                nullptr, nullptr,
                google_breakpad::ExceptionHandler::HANDLER_ALL, MiniDumpNormal,
                crashPipe.c_str(), nullptr));
        return mHandler != nullptr;
    }

    bool waitServicePipeReady(const std::string& pipename,
                              int timeout_ms) override {
        static_assert(kWaitIntervalMS > 0, "kWaitIntervalMS must be greater than 0");
        ::android::base::Win32UnicodeString pipename_unicode(pipename.c_str(),
                                                             pipename.length());
        for (; timeout_ms > 0; timeout_ms -= kWaitIntervalMS) {
            if (WaitNamedPipeW(pipename_unicode.c_str(), timeout_ms) != 0) {
                return true;
            }

            if (GetLastError() == ERROR_SEM_TIMEOUT) {
                // Pipe exists but timed out waiting to become ready
                return false;
            } else {
                // Pipe does not exist yet, sleep and try again
                ::android::base::System::sleepMs(kWaitIntervalMS);
            }

        }
        return false;
    }

    void setupChildCrashProcess(int pid) override {}

    void writeDump() override { mHandler->WriteMinidump(); }

private:
    static bool exceptionFilterCallback(
        void*,
        EXCEPTION_POINTERS*,
        MDRawAssertionInfo*);

    bool onCrashPlatformSpecific() override;

    std::unique_ptr<google_breakpad::ExceptionHandler> mHandler;
};

::android::base::LazyInstance<HostCrashReporter> sCrashReporter =
        LAZY_INSTANCE_INIT;

bool HostCrashReporter::exceptionFilterCallback(
    void*,
    EXCEPTION_POINTERS*,
    MDRawAssertionInfo*) {
    return CrashReporter::get()->onCrash();
}

static void attachMemoryInfo()
{
    PROCESS_MEMORY_COUNTERS_EX memCounters = {sizeof(memCounters)};
    if (::GetProcessMemoryInfo(::GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memCounters),
                sizeof(memCounters))) {
        char buf[1024] = {};
        snprintf(buf, sizeof(buf) - 1,
            "PageFaultCount: %" PRIu64 "\n"
            "PeakWorkingSetSize: %" PRIu64 " kB\n"
            "WorkingSetSize: %" PRIu64 " kB\n"
            "QuotaPeakPagedPoolUsage: %" PRIu64 " kB\n"
            "QuotaPagedPoolUsage: %" PRIu64 " kB\n"
            "QuotaPeakNonPagedPoolUsage: %" PRIu64 " kB\n"
            "QuotaNonPagedPoolUsage: %" PRIu64 " kB\n"
            "PagefileUsage (commit): %" PRIu64 " kB\n"
            "PeakPagefileUsage: %" PRIu64 " kB\n",
            uint64_t(memCounters.PageFaultCount),
            uint64_t(memCounters.PeakWorkingSetSize / 1024),
            uint64_t(memCounters.WorkingSetSize / 1024),
            uint64_t(memCounters.QuotaPeakPagedPoolUsage / 1024),
            uint64_t(memCounters.QuotaPagedPoolUsage / 1024),
            uint64_t(memCounters.QuotaPeakNonPagedPoolUsage / 1024),
            uint64_t(memCounters.QuotaNonPagedPoolUsage / 1024),
            uint64_t((memCounters.PagefileUsage
                ? memCounters.PagefileUsage
                : memCounters.PrivateUsage) / 1024),
            uint64_t(memCounters.PeakPagefileUsage / 1024));

        CrashReporter::get()->attachData(
                    CrashReporter::kProcessMemoryInfoFileName, buf);
    }
}

static void attachProcessList() {
    char command[MAX_PATH + 128] = {};
    snprintf(command, sizeof(command) - 1,
             "tasklist /V >%s\\%s",
             CrashReporter::get()->getDataExchangeDir().c_str(),
             CrashReporter::kProcessListFileName);

    if (system(command) != 0) {
        // try to call the "query process *" command, which used to exist
        // before the taskkill
        snprintf(command, sizeof(command) - 1,
                 "query process * >%s\\%s",
                 CrashReporter::get()->getDataExchangeDir().c_str(),
                 CrashReporter::kProcessListFileName);
        if (system(command) != 0) {
            CrashReporter::get()->attachData(
                        CrashReporter::kProcessListFileName,
                        "Failed to get a process list");
        }
    }
}

bool HostCrashReporter::onCrashPlatformSpecific() {
    // collect the memory usage at the time of the crash
    attachMemoryInfo();
    attachProcessList();

    return true;
}

}  // namespace anonymous

CrashReporter* CrashReporter::get() {
    return sCrashReporter.ptr();
}

}  // namespace crashreport
}  // namespace android

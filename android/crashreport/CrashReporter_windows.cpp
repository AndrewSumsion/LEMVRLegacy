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

#include <memory>

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
                dumpDir_wstr, nullptr, nullptr, nullptr,
                google_breakpad::ExceptionHandler::HANDLER_ALL, MiniDumpNormal,
                crashPipe.c_str(), nullptr));
        return mHandler != nullptr;
    }

    bool waitServicePipeReady(const std::string& pipename,
                              const int timeout_ms) override {
        bool serviceReady = false;
        for (int i = 0; i < timeout_ms / kWaitIntervalMS; i++) {
            if (::android::base::System::get()->pathIsFile(pipename.c_str())) {
                serviceReady = true;
                D("Crash Server Ready after %d ms\n", i * kWaitIntervalMS);
                break;
            }
            ::android::base::System::sleepMs(kWaitIntervalMS);
        }
        return serviceReady;
    }

    void setupChildCrashProcess(int pid) override {}

private:
    std::unique_ptr<google_breakpad::ExceptionHandler> mHandler;
};

::android::base::LazyInstance<HostCrashReporter> sCrashReporter =
        LAZY_INSTANCE_INIT;

}  // namespace anonymous

CrashReporter* CrashReporter::get() {
    return sCrashReporter.ptr();
}

}  // namespace crashreport
}  // namespace android

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

#include "android/crashreport/CrashService_darwin.h"

#include "android/base/String.h"
#include "android/base/files/PathUtils.h"
#include "android/base/system/System.h"

#include "android/crashreport/CrashSystem.h"
#include "android/utils/debug.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <memory>
#include <sstream>

#define E(...) derror(__VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define D(...) VERBOSE_PRINT(init, __VA_ARGS__)
#define I(...) printf(__VA_ARGS__)

#define HWINFO_CMD "system_profiler -detailLevel mini"

namespace android {

using ::android::base::String;
using ::android::base::PathUtils;
using ::android::base::System;

namespace crashreport {

HostCrashService::~HostCrashService() {
    stopCrashServer();
    cleanupHWInfo();
}

void HostCrashService::OnClientDumpRequest(
        void* context,
        const google_breakpad::ClientInfo& client_info,
        const std::string& file_path) {
    if (static_cast<CrashService::DumpRequestContext*>(context)
                ->file_path.empty()) {
        D("Client Requesting dump %s\n", file_path.c_str());
        static_cast<CrashService::DumpRequestContext*>(context)->file_path =
                file_path;
    }
}

void HostCrashService::OnClientExit(
        void* context,
        const google_breakpad::ClientInfo& client_info) {
    D("Client exiting\n");
    CrashService::ServerState* serverstate =
            static_cast<CrashService::ServerState*>(context);
    if (serverstate->connected > 0) {
        serverstate->connected -= 1;
    }
    if (serverstate->connected == 0) {
        serverstate->waiting = false;
    }
}

bool HostCrashService::startCrashServer(const std::string& pipe) {
    if (mCrashServer) {
        return false;
    }
    initCrashServer();

    mCrashServer.reset(new ::google_breakpad::CrashGenerationServer(
            pipe.c_str(), nullptr, nullptr, &OnClientDumpRequest,
            &mDumpRequestContext, &OnClientExit, &mServerState, true,
            CrashSystem::get()->getCrashDirectory()));
    if (!mCrashServer) {
        return false;
    }
    return mCrashServer->Start();
}

bool HostCrashService::stopCrashServer() {
    if (mCrashServer) {
        mCrashServer.reset();
        return true;
    } else {
        return false;
    }
}

bool HostCrashService::isClientAlive() {
    if (!mClientPID) {
        return false;
    }
    // waitpid added for child clients
    waitpid(mClientPID, nullptr, WNOHANG);
    // kill with 0 signal will return non 0 if process does not exist
    if (kill(mClientPID, 0) != 0) {
        return false;
    } else {
        return true;
    }
}

bool HostCrashService::getHWInfo() {
    mHWTmpFilePath.clear();
    System* sys = System::get();
    String tmp_dir = sys->getTempDir();

    String tmp_file_path_template =
            PathUtils::join(tmp_dir, "android_emulator_crash_report_XXXXXX");

    int tmpfd = mkstemp((char*)tmp_file_path_template.data());

    if (tmpfd == -1) {
        E("Error: Can't create temporary file! errno=%d", errno);
        return false;
    }

    String syscmd(HWINFO_CMD);
    syscmd += " > ";
    syscmd += tmp_file_path_template;
    system(syscmd.c_str());

    mHWTmpFilePath = tmp_file_path_template.c_str();
    return true;
}

void HostCrashService::cleanupHWInfo() {
    if (mHWTmpFilePath.empty()) {
        return;
    }

    int rm_ret = remove(mHWTmpFilePath.c_str());

    if (rm_ret == -1) {
        E("Failed to delete HW info at %s", mHWTmpFilePath.c_str());
    }
}

}  // namespace crashreport
}  // namespace android

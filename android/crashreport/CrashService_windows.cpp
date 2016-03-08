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

#include "android/crashreport/CrashService_windows.h"

#include "android/base/files/PathUtils.h"
#include "android/base/StringFormat.h"
#include "android/base/system/System.h"

#include "android/base/system/Win32UnicodeString.h"
#include "android/crashreport/CrashReporter.h"
#include "android/crashreport/CrashSystem.h"
#include "android/utils/debug.h"

#include <fstream>
#include <string>

#include <sys/types.h>
#include <unistd.h>

#include <psapi.h>

#define E(...) derror(__VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define D(...) VERBOSE_PRINT(init, __VA_ARGS__)
#define I(...) printf(__VA_ARGS__)

#define CMD_BUF_SIZE 1024
#define HWINFO_CMD L"dxdiag /dontskip /whql:off /64bit /t"

namespace android {

using ::android::base::PathUtils;
using ::android::base::System;
using ::android::base::Win32UnicodeString;

namespace crashreport {

HostCrashService::~HostCrashService() {
    stopCrashServer();
}

void HostCrashService::OnClientConnect(
        void* context,
        const google_breakpad::ClientInfo* client_info) {
    D("Client connected, pid = %d\n", client_info->pid());
    static_cast<CrashService::ServerState*>(context)->connected += 1;
}

void HostCrashService::OnClientDumpRequest(
        void* context,
        const google_breakpad::ClientInfo* client_info,
        const std::wstring* file_path) {
    if (static_cast<CrashService::DumpRequestContext*>(context)
                ->file_path.empty()) {
        ::std::string file_path_string =
                Win32UnicodeString::convertToUtf8(
                        file_path->c_str());
        D("Client Requesting dump %s\n", file_path_string.c_str());
        static_cast<CrashService::DumpRequestContext*>(context)
                ->file_path.assign(file_path_string.c_str());
    }
}

void HostCrashService::OnClientExit(
        void* context,
        const google_breakpad::ClientInfo* client_info) {
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

    Win32UnicodeString pipe_unicode(pipe.c_str(), pipe.length());
    ::std::wstring pipe_string(pipe_unicode.data());
    Win32UnicodeString crashdir_unicode(
            ::android::crashreport::CrashSystem::get()
                    ->getCrashDirectory()
                    .c_str());
    std::wstring crashdir_wstr(crashdir_unicode.c_str());

    mCrashServer.reset(new ::google_breakpad::CrashGenerationServer(
            pipe_string, nullptr, OnClientConnect, &mServerState,
            OnClientDumpRequest, &mDumpRequestContext, OnClientExit,
            &mServerState, nullptr, nullptr, true, &crashdir_wstr));

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

bool HostCrashService::setClient(int clientpid) {
    mClientProcess.reset(OpenProcess(SYNCHRONIZE, FALSE, clientpid));
    return mClientProcess.get() != nullptr;
}

bool HostCrashService::isClientAlive() {
    if (!mClientProcess) {
        return false;
    }
    if (WaitForSingleObject(mClientProcess.get(), 0) != WAIT_TIMEOUT) {
        return false;
    } else {
        return true;
    }
}

bool HostCrashService::getHWInfo() {
    const std::string& dataDirectory = getDataDirectory();
    if (dataDirectory.empty()) {
        E("Unable to get data directory for crash report attachments");
        return false;
    }
    std::string utf8Path = PathUtils::join(dataDirectory, kHwInfoName);
    Win32UnicodeString file_path(utf8Path);

    Win32UnicodeString syscmd(HWINFO_CMD);
    syscmd.append(file_path);
    int result = _wsystem(syscmd.c_str());
    if (result != 0) {
        E("Unable to get hardware info: %d", errno);
        return false;
    }
    return true;
}

// Convenience function to convert a value to a value in kilobytes
template<typename T>
static T toKB(T value) {
    return value / 1024;
}

bool HostCrashService::getMemInfo() {
    const std::string& data_directory = getDataDirectory();
    if (data_directory.empty()) {
        E("Unable to get data directory for crash report attachments");
        return false;
    }
    std::string path = PathUtils::join(data_directory, kMemInfoName);
    // TODO: Replace ofstream when we have a good way of handling UTF-8 paths
    std::ofstream fout(path.c_str());
    if (!fout) {
        E("Unable to open '%s' to write crash report attachment", path.c_str());
        return false;
    }

    MEMORYSTATUSEX mem;
    mem.dwLength  = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
        DWORD error = GetLastError();
        E("Failed to get global memory status: %lu", error);
        fout << "ERROR: Failed to get global memory status: " << error << "\n";
        return false;
    }

    PERFORMANCE_INFORMATION pi = { sizeof(pi) };
    if (!GetPerformanceInfo(&pi, sizeof(pi))) {
        DWORD error = GetLastError();
        E("Failed to get performance info: %lu", error);
        fout << "ERROR: Failed to get performance info: " << error << "\n";
        return false;
    }

    size_t pageSize = pi.PageSize;
    fout << "Total physical memory: " << toKB(mem.ullTotalPhys) << " kB\n"
         << "Avail physical memory: " << toKB(mem.ullAvailPhys) << " kB\n"
         << "Total page file: " << toKB(mem.ullTotalPageFile) << " kB\n"
         << "Avail page file: " << toKB(mem.ullAvailPageFile) << " kB\n"
         << "Total virtual: " << toKB(mem.ullTotalVirtual) << " kB\n"
         << "Avail virtual: " << toKB(mem.ullAvailVirtual) << " kB\n"
         << "Commit total: " << toKB(pi.CommitTotal * pageSize) << " kB\n"
         << "Commit limit: " << toKB(pi.CommitLimit * pageSize) << " kB\n"
         << "Commit peak: " << toKB(pi.CommitPeak * pageSize) << " kB\n"
         << "System cache: " << toKB(pi.SystemCache * pageSize) << " kB\n"
         << "Kernel total: " << toKB(pi.KernelTotal * pageSize) << " kB\n"
         << "Kernel paged: " << toKB(pi.KernelPaged * pageSize) << " kB\n"
         << "Kernel nonpaged: " << toKB(pi.KernelNonpaged * pageSize) << " kB\n"
         << "Handle count: " << pi.HandleCount << "\n"
         << "Process count: " << pi.ProcessCount << "\n"
         << "Thread count: " << pi.ThreadCount << "\n";

    return fout.good();
}

void HostCrashService::collectProcessList()
{
    if (mDataDirectory.empty()) {
        return;
    }

    auto command = android::base::StringFormat(
                       "tasklist /V >%s\\%s",
                       mDataDirectory,
                       CrashReporter::kProcessListFileName);

    if (system(command.c_str()) != 0) {
        // try to call the "query process *" command, which used to exist
        // before the taskkill
        command = android::base::StringFormat(
                    "query process * >%s\\%s",
                    mDataDirectory,
                    CrashReporter::kProcessListFileName);
        system(command.c_str());
    }
}

}  // namespace crashreport
}  // namespace android

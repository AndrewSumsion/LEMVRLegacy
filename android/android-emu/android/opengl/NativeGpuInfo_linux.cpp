// Copyright 2017 The Android Open Source Project
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

#include "android/opengl/NativeGpuInfo.h"

#include "android/base/StringView.h"
#include "android/base/files/PathUtils.h"
#include "android/base/files/ScopedFd.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/base/misc/FileUtils.h"
#include "android/base/system/System.h"

#include <string>

using android::base::makeCustomScopedPtr;
using android::base::PathUtils;
using android::base::RunOptions;
using android::base::ScopedFd;
using android::base::StringView;
using android::base::System;

static const int kGPUInfoQueryTimeoutMs = 5000;

static std::string load_gpu_info() {
    std::string tmp_dir = System::get()->getTempDir();

    // Get temporary file path
    constexpr StringView temp_filename_pattern = "gpuinfo_XXXXXX";
    std::string temp_file_path =
            PathUtils::join(tmp_dir, temp_filename_pattern);

    auto tmpfd = ScopedFd(mkstemp((char*)temp_file_path.data()));
    if (!tmpfd.valid()) {
        return {};
    }
    temp_file_path.resize(strlen(temp_file_path.c_str()));
    auto tmpFileDeleter = makeCustomScopedPtr(
            &temp_file_path,
            [](const std::string* name) { remove(name->c_str()); });

    // Execute the command to get GPU info.
    if (!System::get()->runCommand({"lspci", "-mvnn"},
                                   RunOptions::WaitForCompletion |
                                           RunOptions::TerminateOnTimeout |
                                           RunOptions::DumpOutputToFile,
                                   kGPUInfoQueryTimeoutMs, nullptr, nullptr,
                                   temp_file_path)) {
        return {};
    }

    return android::readFileIntoString(temp_file_path).valueOr({});
}

static std::string parse_last_hexbrackets(const std::string& str) {
    size_t closebrace_p = str.rfind("]");
    size_t openbrace_p = str.rfind("[", closebrace_p - 1);
    return str.substr(openbrace_p + 1, closebrace_p - openbrace_p - 1);
}

void parse_gpu_info_list_linux(const std::string& contents,
                               GpuInfoList* gpulist) {
    size_t line_loc = contents.find("\n");
    if (line_loc == std::string::npos) {
        line_loc = contents.size();
    }
    size_t p = 0;
    std::string key;
    std::string val;
    bool lookfor = false;

    // Linux - Only support one GPU for now.
    // On Linux, the only command that seems not to take
    // forever is lspci.
    // We just look for "VGA" in lspci, then
    // attempt to grab vendor and device information.
    // Second, we use glx to look for the version string,
    // in case there is a renderer such as Mesa
    // to look out for.
    while (line_loc != std::string::npos) {
        key = contents.substr(p, line_loc - p);
        if (!lookfor && (key.find("VGA") != std::string::npos)) {
            lookfor = true;
            gpulist->addGpu();
            gpulist->currGpu().os = "L";
        } else if (lookfor && (key.find("Vendor") != std::string::npos)) {
            gpulist->currGpu().make = parse_last_hexbrackets(key);
        } else if (lookfor && (key.find("Device") != std::string::npos)) {
            gpulist->currGpu().device_id = parse_last_hexbrackets(key);
            lookfor = false;
        } else if (key.find("OpenGL version string") != std::string::npos) {
            gpulist->currGpu().renderer = key;
        } else {
        }
        if (line_loc == contents.size()) {
            break;
        }
        p = line_loc + 1;
        line_loc = contents.find("\n", p);
        if (line_loc == std::string::npos) {
            line_loc = contents.size();
        }
    }
}

void getGpuInfoListNative(GpuInfoList* gpulist) {
    // Load it in a traditional way - by parsing output of external process.
    std::string gpu_info = load_gpu_info();
    parse_gpu_info_list_linux(gpu_info, gpulist);

    // Unfortunately, even to obtain a driver version on Linux one has to either
    // create a full rendering context (very slow, 150+ms) or hardcode specific
    // ways to get it for each existing GPU driver (just insane).
    //
    // That's why this function doesn't populate driver version and renderer.
    //
    // If you ever need start from something to get them from OpenGL, here's
    // a very simple example:
    //      http://web.mit.edu/jhawk/mnt/spo/3d/src/Mesa-3.0/samples/oglinfo.c
    //
}

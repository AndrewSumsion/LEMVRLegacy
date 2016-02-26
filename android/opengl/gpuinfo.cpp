// Copyright 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/base/memory/LazyInstance.h"
#include "android/base/system/System.h"
#include "android/base/system/Win32UnicodeString.h"
#include "android/opengl/gpuinfo.h"
#include "android/utils/file_io.h"

#include <assert.h>
#include <inttypes.h>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using android::base::RunOptions;
using android::base::String;
using android::base::System;
#ifdef _WIN32
using android::base::Win32UnicodeString;
#endif

static const size_t kFieldLen = 2048;

static const size_t NOTFOUND = std::string::npos;

::android::base::LazyInstance<GpuInfoList> sGpuInfoList =
        LAZY_INSTANCE_INIT;

GpuInfoList* GpuInfoList::get() {
    return sGpuInfoList.ptr();
}

void GpuInfo::addDll(std::string dll_str) {
    dlls.push_back(dll_str);
}

void GpuInfoList::addGpu() {
    infos.push_back(GpuInfo());
}
GpuInfo& GpuInfoList::currGpu() {
    return infos.back();
}

std::string GpuInfoList::dump() {
    std::stringstream ss;
    for (unsigned int i = 0; i < infos.size(); i++) {
        ss << "GPU #" << i + 1 << std::endl;

        if (!infos[i].make.empty()) {
            ss << "  Make: " << infos[i].make << std::endl;
        }
        if (!infos[i].model.empty()) {
            ss << "  Model: " << infos[i].model << std::endl;
        }
        if (!infos[i].device_id.empty()) {
            ss << "  Device ID: " << infos[i].device_id << std::endl;
        }
        if (!infos[i].revision_id.empty()) {
            ss << "  Revision ID: " << infos[i].revision_id << std::endl;
        }
        if (!infos[i].version.empty()) {
            ss << "  Driver version: " << infos[i].version << std::endl;
        }
        if (!infos[i].renderer.empty()) {
            ss << "  Renderer: " << infos[i].renderer << std::endl;
        }
    }
    return ss.str();
}

// Actual blacklist starts here.
// Most entries imported from Chrome blacklist.
static const BlacklistEntry sGpuBlacklist[] = {

        // Make | Model | DeviceID | RevisionID | DriverVersion | Renderer
        {nullptr, nullptr, "0x7249", nullptr, nullptr,
         nullptr},  // ATI Radeon X1900 on Mac
        {"8086", nullptr, nullptr, nullptr, nullptr,
         "Mesa"},  // Linux, Intel, Mesa
        {"8086", nullptr, nullptr, nullptr, nullptr,
         "mesa"},  // Linux, Intel, Mesa

        {"8086", nullptr, "27ae", nullptr, nullptr,
         nullptr},  // Intel 945 Chipset
        {"1002", nullptr, nullptr, nullptr, nullptr, nullptr},  // Linux, ATI

        {nullptr, nullptr, "0x9583", nullptr, nullptr,
         nullptr},  // ATI Radeon HD2600 on Mac
        {nullptr, nullptr, "0x94c8", nullptr, nullptr,
         nullptr},  // ATI Radeon HD2400 on Mac

        {"NVIDIA (0x10de)", nullptr, "0x0324", nullptr, nullptr,
         nullptr},  // NVIDIA GeForce FX Go5200 (Mac)
        {"NVIDIA", "NVIDIA GeForce FX Go5200", nullptr, nullptr, nullptr,
         nullptr},  // NVIDIA GeForce FX Go5200 (Win)
        {"10de", nullptr, "0324", nullptr, nullptr,
         nullptr},  // NVIDIA GeForce FX Go5200 (Linux)

        {"10de", nullptr, "029e", nullptr, nullptr,
         nullptr},  // NVIDIA Quadro FX 1500 (Linux)

        // Various Quadro FX cards on Linux
        {"10de", nullptr, "00cd", nullptr, nullptr,
         "195.36.24"},
        {"10de", nullptr, "00ce", nullptr, nullptr,
         "195.36.24"},
        // Driver version 260.19.6 on Linux
        {"10de", nullptr, nullptr, nullptr, nullptr,
         "260.19.6"},

        {"NVIDIA (0x10de)", nullptr, "0x0393", nullptr, nullptr,
         nullptr},  // NVIDIA GeForce 7300 GT (Mac)
};

static const int sBlacklistSize =
    sizeof(sGpuBlacklist) / sizeof(BlacklistEntry);

// If any blacklist entry matches any gpu, return true.
bool gpuinfo_query_blacklist(GpuInfoList* gpulist,
                             const BlacklistEntry* list,
                             int size) {
    for (auto gpuinfo : gpulist->infos) {
        for (int i = 0; i < size; i++) {
            auto bl_entry = list[i];
            const char* bl_make = bl_entry.make;
            const char* bl_model = bl_entry.model;
            const char* bl_device_id = bl_entry.device_id;
            const char* bl_revision_id = bl_entry.revision_id;
            const char* bl_version = bl_entry.version;
            const char* bl_renderer = bl_entry.renderer;

            if (bl_make && (gpuinfo.make != bl_make))
                continue;
            if (bl_model && (gpuinfo.model != bl_model))
                continue;
            if (bl_device_id && (gpuinfo.device_id != bl_device_id))
                continue;
            if (bl_revision_id && (gpuinfo.revision_id != bl_revision_id))
                continue;
            if (bl_version && (gpuinfo.revision_id != bl_version))
                continue;
            if (bl_renderer && (gpuinfo.renderer.find(bl_renderer) ==
                                NOTFOUND))
                continue;
            return true;
        }
    }
    return false;
}

std::string load_gpu_info() {
    auto& sys = *System::get();
    std::string tmp_dir(sys.getTempDir().c_str());

// Get temporary file path
#ifndef _WIN32
    if (tmp_dir.back() != '/') {
        tmp_dir += "/";
    }
    std::string temp_filename_pattern = "gpuinfo_XXXXXX";
    std::string temp_file_path_template = (tmp_dir + temp_filename_pattern);

    int tmpfd = mkstemp((char*)temp_file_path_template.data());

    if (tmpfd == -1) {
        return std::string();
    }

    const char* temp_file_path = temp_file_path_template.c_str();

#else
    char tmp_filename_buffer[kFieldLen] = {};
    DWORD temp_file_ret =
            GetTempFileName(tmp_dir.c_str(), "gpu", 0, tmp_filename_buffer);

    if (!temp_file_ret) {
        return std::string();
    }

    const char* temp_file_path = tmp_filename_buffer;

#endif

// Execute the command to get GPU info.
#ifdef __APPLE__
    char command[kFieldLen] = {};
    snprintf(command, sizeof(command),
             "system_profiler SPDisplaysDataType > %s", temp_file_path);
    system(command);
#elif !defined(_WIN32)
    char command[kFieldLen] = {};

    snprintf(command, sizeof(command), "lspci -mvnn > %s", temp_file_path);
    system(command);
    snprintf(command, sizeof(command), "glxinfo >> %s", temp_file_path);
    system(command);
#else
    char temp_path_arg[kFieldLen] = {};
    snprintf(temp_path_arg, sizeof(temp_path_arg), "/OUTPUT:%s",
             temp_file_path);
    sys.runCommand({"wmic", temp_path_arg, "path", "Win32_VideoController",
                    "get", "/value"},
                   RunOptions::WaitForCompletion);
#endif

    std::ifstream fh(temp_file_path, std::ios::binary);
    if (!fh) {
#ifdef _WIN32
        Sleep(100);
#endif
        fh.open(temp_file_path, std::ios::binary);
        if (!fh) {
            return std::string();
        }
    }

    std::stringstream ss;
    ss << fh.rdbuf();
    std::string contents = ss.str();
    fh.close();

#ifndef _WIN32
    remove(temp_file_path);
#else
    DWORD del_ret = DeleteFile(temp_file_path);
    if (!del_ret) {
        Sleep(100);
        del_ret = DeleteFile(temp_file_path);
    }
#endif

#ifdef _WIN32
    int num_chars = contents.size() / sizeof(wchar_t);
    String utf8String = Win32UnicodeString::convertToUtf8(
            reinterpret_cast<const wchar_t*>(contents.c_str()), num_chars);
    return std::string(utf8String.c_str());
#else
    return contents;
#endif
}

std::string parse_last_hexbrackets(const std::string& str) {
    size_t closebrace_p = str.rfind("]");
    size_t openbrace_p = str.rfind("[", closebrace_p - 1);
    return str.substr(openbrace_p + 1, closebrace_p - openbrace_p - 1);
}

std::string parse_renderer_part(const std::string& str) {
    size_t lastspace_p = str.rfind(" ");
    size_t prevspace_p = str.rfind(" ", lastspace_p - 1);
    return str.substr(prevspace_p + 1, lastspace_p - prevspace_p - 1);
}

void parse_gpu_info_list_osx(const std::string& contents,
                             GpuInfoList* gpulist) {
    size_t line_loc = contents.find("\n");
    if (line_loc == NOTFOUND) {
        line_loc = contents.size();
    }
    size_t p = 0;
    size_t kvsep_loc;
    std::string key;
    std::string val;

    // OS X: We expect a sequence of lines from system_profiler
    // that describe all GPU's connected to the system.
    // When a line containing
    //     Chipset Model: <gpu model name>
    // that's the earliest reliable indication of information
    // about an additional GPU. After that,
    // it's a simple matter to find the vendor/device ID lines.
    while (line_loc != NOTFOUND) {
        kvsep_loc = contents.find(": ", p);
        if ((kvsep_loc != NOTFOUND) && (kvsep_loc < line_loc)) {
            key = contents.substr(p, kvsep_loc - p);
            size_t valbegin = (kvsep_loc + 2);
            val = contents.substr(valbegin, line_loc - valbegin);
            if (key.find("Chipset Model") != NOTFOUND) {
                gpulist->addGpu();
                gpulist->currGpu().model = val;
            } else if (key.find("Vendor") != NOTFOUND) {
                gpulist->currGpu().make = val;
            } else if (key.find("Device ID") != NOTFOUND) {
                gpulist->currGpu().device_id = val;
            } else if (key.find("Revision ID") != NOTFOUND) {
                gpulist->currGpu().revision_id = val;
            } else if (key.find("Display Type") != NOTFOUND) {
                gpulist->currGpu().current_gpu = true;
            } else {
            }
        }
        if (line_loc == contents.size()) {
            break;
        }
        p = line_loc + 1;
        line_loc = contents.find("\n", p);
        if (line_loc == NOTFOUND) {
            line_loc = contents.size();
        }
    }
}

void parse_gpu_info_list_linux(const std::string& contents,
                               GpuInfoList* gpulist) {
    size_t line_loc = contents.find("\n");
    if (line_loc == NOTFOUND) {
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
    // Second, we issue glxinfo and look for the version string,
    // in case there is a renderer such as Mesa
    // to look out for.
    while (line_loc != NOTFOUND) {
        key = contents.substr(p, line_loc - p);
        if (!lookfor && (key.find("VGA") != NOTFOUND)) {
            lookfor = true;
            gpulist->addGpu();
        } else if (lookfor && (key.find("Vendor") != NOTFOUND)) {
            gpulist->currGpu().make = parse_last_hexbrackets(key);
        } else if (lookfor && (key.find("Device") != NOTFOUND)) {
            gpulist->currGpu().device_id = parse_last_hexbrackets(key);
            lookfor = false;
        } else if (key.find("OpenGL version string") != NOTFOUND) {
            gpulist->currGpu().renderer = key;
        } else {
        }
        if (line_loc == contents.size()) {
            break;
        }
        p = line_loc + 1;
        line_loc = contents.find("\n", p);
        if (line_loc == NOTFOUND) {
            line_loc = contents.size();
        }
    }
}

void parse_gpu_info_list_windows(const std::string& contents,
                                 GpuInfoList* gpulist) {
    size_t line_loc = contents.find("\r\n");
    if (line_loc == NOTFOUND) {
        line_loc = contents.size();
    }
    size_t p = 0;
    size_t equals_pos = 0;
    size_t val_pos = 0;
    std::string key;
    std::string val;

    // Windows: We use `wmic path Win32_VideoController get /value`
    // to get a reasonably detailed list of '<key>=<val>'
    // pairs. From these, we can get the make/model
    // of the GPU, the driver version, and all DLLs involved.
    while (line_loc != NOTFOUND) {
        equals_pos = contents.find("=", p);
        if ((equals_pos != NOTFOUND) && (equals_pos < line_loc)) {
            key = contents.substr(p, equals_pos - p);
            val_pos = equals_pos + 1;
            val = contents.substr(val_pos, line_loc - val_pos);

            if (key.find("AdapterCompatibility") != NOTFOUND) {
                gpulist->addGpu();
                gpulist->currGpu().make = val;
            } else if (key.find("Caption") != NOTFOUND) {
                gpulist->currGpu().model = val;
            } else if (key.find("DriverVersion") != NOTFOUND) {
                gpulist->currGpu().version = val;
            } else if (key.find("InstalledDisplayDrivers") != NOTFOUND) {
                if (line_loc - val_pos == 0) {
                    continue;
                }
                const std::string& dll_str =
                    contents.substr(val_pos, line_loc - val_pos);

                size_t vp = 0;
                size_t dll_sep_loc = dll_str.find(",", vp);
                size_t dll_end =
                        (dll_sep_loc != NOTFOUND) ?  dll_sep_loc : dll_str.size() - vp;
                gpulist->currGpu().addDll(dll_str.substr(vp, dll_end - vp));

                while (dll_sep_loc != NOTFOUND) {
                    vp = dll_sep_loc + 1;
                    dll_sep_loc = dll_str.find(",", vp);
                    dll_end =
                            (dll_sep_loc != NOTFOUND) ?  dll_sep_loc : dll_str.size() - vp;
                    gpulist->currGpu().addDll(
                            dll_str.substr(vp, dll_end - vp));
                }

                const std::string& curr_make = gpulist->infos.back().make;
                if (curr_make == "NVIDIA") {
                    gpulist->currGpu().addDll("nvoglv32.dll");
                    gpulist->currGpu().addDll("nvoglv64.dll");
                } else if (curr_make == "Advanced Micro Devices, Inc.") {
                    gpulist->currGpu().addDll("atioglxx.dll");
                    gpulist->currGpu().addDll("atig6txx.dll");
                }
            }
        }
        if (line_loc == contents.size()) {
            break;
        }
        p = line_loc + 2;
        line_loc = contents.find("\r\n", p);
        if (line_loc == NOTFOUND) {
            line_loc = contents.size();
        }
    }
}

void parse_gpu_info_list(const std::string& contents, GpuInfoList* gpulist) {
#ifdef __APPLE__
    parse_gpu_info_list_osx(contents, gpulist);
#elif !defined(_WIN32)
    parse_gpu_info_list_linux(contents, gpulist);
#else
    parse_gpu_info_list_windows(contents, gpulist);
#endif
}

bool parse_and_query_blacklist(const std::string& contents) {
    GpuInfoList* gpulist = GpuInfoList::get();
    parse_gpu_info_list(contents, gpulist);
    return gpuinfo_query_blacklist(gpulist, sGpuBlacklist, sBlacklistSize);
}

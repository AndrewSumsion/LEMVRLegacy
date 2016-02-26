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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>

// gpuinfo is designed to collect information about the GPUs
// installed on the host system for the purposes of
// automatically determining which renderer to select,
// in the cases where the GPU drivers are known to have issues
// running the emulator.

// The main entry points:

// load_gpu_info() queries the host system
// and returns a platform-specific string
// describing all GPU's on the system.
std::string load_gpu_info();

// parse_and_query_blacklist() takes the string
// describing GPU information, attempts to parse it,
// and returns true or false depending on
// whether the GPU information provided
// matches a known unreliable GPU/GPU driver.
bool parse_and_query_blacklist(const std::string& contents);

// Below is the implementation.

// We keep a blacklist of known crashy GPU drivers
// as a static const list with items of this type:
struct BlacklistEntry {
    const char* make;
    const char* model;
    const char* device_id;
    const char* revision_id;
    const char* version;
    const char* renderer;
};

// GpuInfo/GpuInfoList are the representation of parsed information
// about the system's GPU.s
class GpuInfo {
public:
    GpuInfo() { current_gpu = false; }

    bool current_gpu;

    void addDll(std::string dll_str);

    std::string make;
    std::string model;
    std::string device_id;
    std::string revision_id;
    std::string version;
    std::string renderer;

    std::vector<std::string> dlls;
};

class GpuInfoList {
public:
    GpuInfoList() {}
    void addGpu();
    GpuInfo& currGpu();
    void dump();

    std::vector<GpuInfo> infos;
};

// Below are helper functions that can be useful in various
// contexts (e.g., unit testing).

// gpuinfo_query_blacklist():
// Function to query a given blacklist of GPU's.
// The blacklist |list| (of length |size|) attempts
// to match all non-NULL entry fields exactly against
// info of all GPU's in |gpulist|. If there is any match,
// the host system is considered on the blacklist.
// (Null blacklist entry fields are ignored and
// essentially act as wildcards).
bool gpuinfo_query_blacklist(GpuInfoList* gpulist,
                             const BlacklistEntry* list,
                             int size);

// Main function to parse GPU information.
void parse_gpu_info_list(const std::string& contents, GpuInfoList* gpulist);

// Platform-specific information parsing functions.
void parse_gpu_info_list_osx(const std::string& contents, GpuInfoList* gpulist);
void parse_gpu_info_list_linux(const std::string& contents, GpuInfoList* gpulist);
void parse_gpu_info_list_windows(const std::string& contents, GpuInfoList* gpulist);


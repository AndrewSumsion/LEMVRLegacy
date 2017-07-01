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

#include "android/opengl/gpuinfo.h"

#include "android/base/async/Looper.h"
#include "android/base/async/ThreadLooper.h"
#include "android/base/memory/LazyInstance.h"
#include "android/base/system/System.h"
#include "android/base/threads/ParallelTask.h"
#include "android/base/threads/Thread.h"
#include "android/opengl/gpuinfo.h"
#include "android/opengl/NativeGpuInfo.h"

#include <assert.h>
#include <inttypes.h>
#include <sstream>
#include <utility>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using android::base::Looper;
using android::base::runParallelTask;
using android::base::System;

// Try to switch to NVIDIA on Optimus systems,
// and AMD GPU on AmdPowerXpress.
// See http://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
// and https://community.amd.com/thread/169965
// These variables need to be visible from the final emulator executable
// as exported symbols.
#ifdef _WIN32
#define FLAG_EXPORT __declspec(dllexport)
#else
#define FLAG_EXPORT __attribute__ ((visibility ("default")))
#endif

FLAG_EXPORT int NvOptimusEnablement = 0x00000001;
FLAG_EXPORT int AmdPowerXpressRequestHighPerformance = 0x00000001;

#undef FLAG_EXPORT

static const System::Duration kGPUInfoQueryTimeoutMs = 5000;
static const System::Duration kQueryCheckIntervalMs = 66;

static ::android::base::LazyInstance<GpuInfoList> sGpuInfoList =
        LAZY_INSTANCE_INIT;

void GpuInfo::addDll(std::string dll_str) {
    dlls.push_back(std::move(dll_str));
}

void GpuInfoList::addGpu() {
    infos.push_back(GpuInfo());
}
GpuInfo& GpuInfoList::currGpu() {
    if (infos.empty()) { addGpu(); }
    return infos.back();
}

std::string GpuInfoList::dump() const {
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

void GpuInfoList::clear() {
    blacklist_status = false;
    Anglelist_status = false;
    SyncBlacklist_status = false;
    infos.clear();
}

static bool gpuinfo_query_list(GpuInfoList* gpulist,
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
            const char* bl_os = bl_entry.os;

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
                                std::string::npos))
                continue;
            if (bl_os && (gpuinfo.os != bl_os))
                continue;
            return true;
        }
    }
    return false;
}

// Actual blacklist starts here.
// Most entries imported from Chrome blacklist.
static const BlacklistEntry sGpuBlacklist[] = {

        // Make | Model | DeviceID | RevisionID | DriverVersion | Renderer |
        // OS
        {nullptr, nullptr, "0x7249", nullptr, nullptr,
         nullptr, "M"},  // ATI Radeon X1900 on Mac
        {"8086", nullptr, nullptr, nullptr, nullptr,
         "Mesa", "L"},  // Linux, Intel, Mesa
        {"8086", nullptr, nullptr, nullptr, nullptr,
         "mesa", "L"},  // Linux, Intel, Mesa

        {"8086", nullptr, "27ae", nullptr, nullptr,
         nullptr, nullptr},  // Intel 945 Chipset
        {"1002", nullptr, nullptr, nullptr, nullptr, nullptr,
          "L"},  // Linux, ATI

        {nullptr, nullptr, "0x9583", nullptr, nullptr,
         nullptr, "M"},  // ATI Radeon HD2600 on Mac
        {nullptr, nullptr, "0x94c8", nullptr, nullptr,
         nullptr, "M"},  // ATI Radeon HD2400 on Mac

        {"NVIDIA (0x10de)", nullptr, "0x0324", nullptr, nullptr,
         nullptr, "M"},  // NVIDIA GeForce FX Go5200 (Mac)
        {"10DE", "NVIDIA GeForce FX Go5200", nullptr, nullptr, nullptr,
         nullptr, "W"},  // NVIDIA GeForce FX Go5200 (Win)
        {"10de", nullptr, "0324", nullptr, nullptr,
         nullptr, "L"},  // NVIDIA GeForce FX Go5200 (Linux)

        {"10de", nullptr, "029e", nullptr, nullptr,
         nullptr, "L"},  // NVIDIA Quadro FX 1500 (Linux)

        // Various Quadro FX cards on Linux
        {"10de", nullptr, "00cd", nullptr, "195.36.24",
          nullptr, "L"},
        {"10de", nullptr, "00ce", nullptr, "195.36.24",
         nullptr, "L"},
        // Driver version 260.19.6 on Linux
        {"10de", nullptr, nullptr, nullptr, "260.19.6",
         nullptr, "L"},

        {"NVIDIA (0x10de)", nullptr, "0x0393", nullptr, nullptr,
         nullptr, "M"},  // NVIDIA GeForce 7300 GT (Mac)
};

static const int sBlacklistSize =
    sizeof(sGpuBlacklist) / sizeof(BlacklistEntry);

// If any blacklist entry matches any gpu, return true.
bool gpuinfo_query_blacklist(GpuInfoList* gpulist,
                             const BlacklistEntry* list,
                             int size) {
    return gpuinfo_query_list(gpulist, list, size);
}

#ifdef _WIN32
static const WhitelistEntry sAngleWhitelist[] = {
        // Make | Model | DeviceID | RevisionID | DriverVersion | Renderer |
        // OS
        // HD 3000 on Windows
        {"8086", nullptr, "0116", nullptr, nullptr,
         nullptr, "W"},
        {"8086", nullptr, "0126", nullptr, nullptr,
         nullptr, "W"},
        {"8086", nullptr, "0102", nullptr, nullptr,
         nullptr, "W"},
};

static const int sAngleWhitelistSize =
    sizeof(sAngleWhitelist) / sizeof(WhitelistEntry);


static bool gpuinfo_query_whitelist(GpuInfoList *gpulist,
                             const WhitelistEntry *list,
                             int size) {
    return gpuinfo_query_list(gpulist, list, size);
}

#endif

static const BlacklistEntry sSyncBlacklist[] = {
    // Make | Model | DeviceID | RevisionID | DriverVersion | Renderer |
    // OS
    // All NVIDIA Quadro NVS and NVIDIA NVS GPUs on Windows
    {"10de", nullptr, "06fd", nullptr, nullptr, nullptr, "W"}, // NVS 295
    {"10de", nullptr, "0a6a", nullptr, nullptr, nullptr, "W"}, // NVS 2100M
    {"10de", nullptr, "0a6c", nullptr, nullptr, nullptr, "W"}, // NVS 5100M
    {"10de", nullptr, "0ffd", nullptr, nullptr, nullptr, "W"}, // NVS 510
    {"10de", nullptr, "1056", nullptr, nullptr, nullptr, "W"}, // NVS 4200M
    {"10de", nullptr, "10d8", nullptr, nullptr, nullptr, "W"}, // NVS 300
    {"10de", nullptr, "014a", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 440
    {"10de", nullptr, "0165", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 285
    {"10de", nullptr, "017a", nullptr, nullptr, nullptr, "W"}, // Quadro NVS (generic)
    {"10de", nullptr, "018a", nullptr, nullptr, nullptr, "W"}, // Quadro NVS AGP8X (generic)
    {"10de", nullptr, "018c", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 50 PCI (generic)
    {"10de", nullptr, "01db", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 120M
    {"10de", nullptr, "0245", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 210S / NVIDIA GeForce 6150LE
    {"10de", nullptr, "032a", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 55/280 PCI
    {"10de", nullptr, "040c", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 570M / Mobile Quadro FX/NVS video card
    {"10de", nullptr, "0429", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 135M or Quadro NVS 140M
    {"10de", nullptr, "042b", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 135M
    {"10de", nullptr, "042f", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 290
    {"10de", nullptr, "06ea", nullptr, nullptr, nullptr, "W"}, // quadro nvs 150m
    {"10de", nullptr, "06eb", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 160M
    {"10de", nullptr, "06f8", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 420
    {"10de", nullptr, "06fa", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 450
    {"10de", nullptr, "0a2c", nullptr, nullptr, nullptr, "W"}, // Quadro NVS 5100M
};

static const int sSyncBlacklistSize =
    sizeof(sSyncBlacklist) / sizeof(BlacklistEntry);

void query_blacklist_fn(bool* res) {
    GpuInfoList* gpulist = sGpuInfoList.ptr();

    getGpuInfoListNative(gpulist);

    *res = gpuinfo_query_blacklist(gpulist, sGpuBlacklist, sBlacklistSize);
    sGpuInfoList->blacklist_status = *res;

#ifdef _WIN32
    sGpuInfoList->Anglelist_status =
        gpuinfo_query_whitelist(gpulist, sAngleWhitelist, sAngleWhitelistSize);
#else
    sGpuInfoList->Anglelist_status = false;
#endif
    sGpuInfoList->SyncBlacklist_status =
        gpuinfo_query_blacklist(gpulist, sSyncBlacklist, sSyncBlacklistSize);
}

// Separate thread for GPU info querying:
//
// Our goal is to account for circumstances where obtaining GPU info either
// takes too long or ties up the host system in a special way where the system
// ends up hanging. This is bad, since no progress will happen for emulator
// startup, which is more critical.
//
// We therefore use a ParallelTask and a looper with timeout to take care of
// this case.
//
// Note that we use a separate thread (rather than the main thread) because
// later on when skin_winsys_enter_main_loop is called, it will set the looper
// of the main thread to a custom Looper that works with Qt
// (android::qt::createLooper()).  Otherwise, creating a looper on the main
// thread at this point will prevent the custom looper from being used, which
// aborts the program.
class GPUInfoQueryThread : public android::base::Thread {
public:
    GPUInfoQueryThread() { this->start(); }
    virtual intptr_t main() override {
        Looper* looper = android::base::ThreadLooper::get();
        runParallelTask<bool>(looper, &query_blacklist_fn,
                              [](bool) {}, kQueryCheckIntervalMs);
        looper->runWithTimeoutMs(kGPUInfoQueryTimeoutMs);
        looper->forceQuit();
        return 0;
    }
};

static android::base::LazyInstance<GPUInfoQueryThread> sGPUInfoQueryThread =
        LAZY_INSTANCE_INIT;

void async_query_host_gpu_start() {
    sGPUInfoQueryThread.get();
}

bool async_query_host_gpu_blacklisted() {
    return globalGpuInfoList().blacklist_status;
}

bool async_query_host_gpu_AngleWhitelisted() {
    return globalGpuInfoList().Anglelist_status;
}

bool async_query_host_gpu_SyncBlacklisted() {
    return globalGpuInfoList().SyncBlacklist_status;
}

void setGpuBlacklistStatus(bool switchedToSoftware) {
    sGpuInfoList->blacklist_status = switchedToSoftware;
}

const GpuInfoList& globalGpuInfoList() {
    sGPUInfoQueryThread->wait();
    return *sGpuInfoList;
}

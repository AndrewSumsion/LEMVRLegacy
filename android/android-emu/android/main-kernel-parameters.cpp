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

#include "android/main-kernel-parameters.h"

#include "android/base/StringFormat.h"
#include "android/emulation/GoldfishDma.h"
#include "android/emulation/ParameterList.h"
#include "android/emulation/SetupParameters.h"
#include "android/featurecontrol/FeatureControl.h"
#include "android/utils/debug.h"
#include "android/utils/dns.h"

#include <algorithm>
#include <memory>

#include <inttypes.h>
#include <string.h>

using android::base::StringFormat;

// Note: The ACPI _HID that follows devices/ must match the one defined in the
// ACPI tables (hw/i386/acpi_build.c)
static const char kSysfsAndroidDtDir[] =
        "/sys/bus/platform/devices/ANDR0001:00/properties/android/";

char* emulator_getKernelParameters(const AndroidOptions* opts,
                                   const char* targetArch,
                                   int apiLevel,
                                   const char* kernelSerialPrefix,
                                   const char* avdKernelParameters,
                                   AndroidGlesEmulationMode glesMode,
                                   int bootPropOpenglesVersion,
                                   uint64_t glFramebufferSizeBytes,
                                   mem_map ramoops,
                                   bool isQemu2) {
    android::ParameterList params;
    bool isX86ish = !strcmp(targetArch, "x86") || !strcmp(targetArch, "x86_64");

    // We always force qemu=1 when running inside QEMU.
    params.add("qemu=1");

    params.addFormat("androidboot.hardware=%s",
                     isQemu2 ? "ranchu" : "goldfish");

    // TODO: enable this with option
    // params.addFormat("androidboot.logcat=*:D");

    if (isX86ish) {
        params.add("clocksource=pit");
        // b/67565886, when cpu core is set to 2, clock_gettime() function hangs
        // in goldfish kernel which caused surfaceflinger hanging in the guest
        // system. To workaround, start the kernel with no kvmclock. Currently,
        // only API 24 and API 25 have kvm clock enabled in goldfish kernel.
        params.add("no-kvmclock");
    }

    android::setupVirtualSerialPorts(
            &params, nullptr, apiLevel, targetArch, kernelSerialPrefix, isQemu2,
            opts->show_kernel, opts->logcat || opts->shell, opts->shell_serial);

    params.addIf("android.checkjni=1", !opts->no_jni);
    params.addIf("android.bootanim=0", opts->no_boot_anim);

    // qemu.gles is used to pass the GPU emulation mode to the guest
    // through kernel parameters. Note that the ro.opengles.version
    // boot property must also be defined for |gles > 0|, but this
    // is not handled here (see vl-android.c for QEMU1).
    {
        int gles;
        switch (glesMode) {
            case kAndroidGlesEmulationHost: gles = 1; break;
            case kAndroidGlesEmulationGuest: gles = 2; break;
            default: gles = 0;
        }
        params.addFormat("qemu.gles=%d", gles);
    }

    if (isQemu2 && android::featurecontrol::isEnabled(android::featurecontrol::EncryptUserData)) {
        params.add("qemu.encrypt=1");
    }

    // If qemu1, make sure GLDMA is disabled.
    if (!isQemu2)
        android::featurecontrol::setEnabledOverride(
                android::featurecontrol::GLDMA, false);

    // OpenGL ES related setup
    // 1. Set opengles.version
    params.addFormat("qemu.opengles.version=%d", bootPropOpenglesVersion);

    // 2. Calculate additional memory for software renderers (e.g., SwiftShader)
    const uint64_t one_MB = 1024ULL * 1024ULL;
    int numBuffers = 2; /* double buffering */
    uint64_t glEstimatedFramebufferMemUsageMB =
        (numBuffers * glFramebufferSizeBytes + one_MB - 1) / one_MB;

    // 3. Additional contiguous memory reservation for DMA and software framebuffers,
    // specified in MB
    const int extraCma =
        glEstimatedFramebufferMemUsageMB +
        (isQemu2 && android::featurecontrol::isEnabled(android::featurecontrol::GLDMA) ? 256 : 0);
    if (extraCma) {
        params.addFormat("cma=%" PRIu64 "M", glEstimatedFramebufferMemUsageMB + extraCma);
    }

    if (opts->logcat) {
        std::string param = opts->logcat;
        // Replace any space with a comma.
        std::replace(param.begin(), param.end(), ' ', ',');
        std::replace(param.begin(), param.end(), '\t', ',');
        params.addFormat("androidboot.logcat=%s", param);
    }

    if (opts->bootchart) {
        params.addFormat("androidboot.bootchart=%s", opts->bootchart);
    }

    if (opts->selinux) {
        params.addFormat("androidboot.selinux=%s", opts->selinux);
    }

    if (opts->dns_server) {
        SockAddress ips[ANDROID_MAX_DNS_SERVERS];
        int dnsCount = android_dns_get_servers(opts->dns_server, ips);
        if (dnsCount > 1) {
            params.addFormat("ndns=%d", dnsCount);
        }
    }

    if (isQemu2 &&
            android::featurecontrol::isEnabled(android::featurecontrol::Wifi)) {
        params.add("qemu.wifi=1");
        // Enable multiple channels so the kernel can scan on one channel while
        // communicating the other. This speeds up scanning significantly.
        params.add("mac80211_hwsim.channels=2");
    }

    if (isQemu2 && isX86ish) {
        // x86 and x86_64 platforms use an alternative Android DT directory that
        // mimics the layout of /proc/device-tree/firmware/android/
        params.addFormat("androidboot.android_dt_dir=%s", kSysfsAndroidDtDir);
    }

    if (isQemu2) {
        if (android::featurecontrol::isEnabled(android::featurecontrol::SystemAsRoot)) {
            params.add("skip_initramfs");
            params.add("rootwait");
            params.add("ro");
            params.add("init=/init");
            params.add("root=/dev/vda1");
        }
    }

    if (avdKernelParameters && avdKernelParameters[0]) {
        params.add(avdKernelParameters);
    }

    // Configure the ramoops module, and mark the region where ramoops lives as
    // unusable. This will prevent anyone else from using this memory region.
    if (ramoops.size > 0 && ramoops.start > 0) {
      params.addFormat("ramoops.mem_address=0x%" PRIx64, ramoops.start);
      params.addFormat("ramoops.mem_size=0x%" PRIx64, ramoops.size);
      params.addFormat("memmap=0x%" PRIx64 "$0x%" PRIx64,  ramoops.size, ramoops.start);
    }
    return params.toCStringCopy();
}

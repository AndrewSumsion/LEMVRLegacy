/* Copyright (C) 2015 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

#pragma once

#include "android/avd/hw-config.h"
#include "android/avd/info.h"
#include "android/cmdline-option.h"
#include "android/utils/aconfig-file.h"
#include "android/utils/compiler.h"

#include <stdint.h>

// NOTE: This include is only here to prevent QEMU2 from failing to compile
//       calls to str_reset, whose declaration has been moved to
//       android/utils/string.h.
#include "android/utils/string.h"

ANDROID_BEGIN_HEADER

// Special value return
#define EMULATOR_EXIT_STATUS_POSITIONAL_QEMU_PARAMETER  (-1)

// Parse command-line options and setups |opt| and |hw| structures.
// |p_argc| and |p_argv| are pointers to the command-line parameters
// received from main(). |targetArch| is the target architecture for
// platform builds. |is_qemu2| is true if this is called from QEMU2,
// false if called from the classic emulator. |opt| and |hw| are
// caller-provided structures that will be initialized by the function.
//
// On success, return true and sets |*the_avd| to the address of a new
// AvdInfo instance. On failure, return false and sets |*exit_status|
// to a process exit status.
//
// NOTE: As a special case |*exit_status| will be set to
// EMLATOR_EXIT_STATUS_POSITIONAL_QEMU_PARAMETER on failure to indicate that
// a QEMU positional parameter was detected. The caller should copy all
// arguments from |*p_argc| and |*p_argv| and call the QEMU main function
// with them, then exit.
bool emulator_parseCommonCommandLineOptions(int* p_argc,
                                            char*** p_argv,
                                            const char* targetArch,
                                            bool is_qemu2,
                                            AndroidOptions* opt,
                                            AndroidHwConfig* hw,
                                            AvdInfo** the_avd,
                                            int* exit_status);

/* Common routines used by both android-qemu1-glue/main.c and android/main-ui.c */

// For QEMU2 only
#define reassign_string(pstr, value) str_reset(pstr, value)

unsigned convertBytesToMB( uint64_t  size );
uint64_t convertMBToBytes( unsigned  megaBytes );

#define NETWORK_SPEED_DEFAULT  "full"
#define NETWORK_DELAY_DEFAULT  "none"

extern const char*  android_skin_net_speed;
extern const char*  android_skin_net_delay;

typedef enum {
    ACCEL_OFF = 0,
    ACCEL_ON = 1,
    ACCEL_AUTO = 2,
} CpuAccelMode;

#ifdef __linux__
    static const char kAccelerator[] = "KVM";
    static const char kEnableAccelerator[] = "-enable-kvm";
    static const char kDisableAccelerator[] = "-disable-kvm";
#else
    static const char kAccelerator[] = "Intel HAXM";
    static const char kEnableAccelerator[] = "-enable-hax";
    static const char kDisableAccelerator[] = "-disable-hax";
#endif

/*
 * Param:
 *  opts - Options passed to the main()
 *  avd - AVD info containig paths for the hardware configuration.
 *  accel_mode - indicates acceleration mode based on command line
 *  status - a string about cpu acceleration status, must be not null.
 * Return: if cpu acceleration is available
 */
bool handleCpuAcceleration(AndroidOptions* opts, const AvdInfo* avd,
                           CpuAccelMode* accel_mode, char** accel_status);

ANDROID_END_HEADER

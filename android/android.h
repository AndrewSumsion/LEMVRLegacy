/* Copyright (C) 2007-2015 The Android Open Source Project
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

#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "android/skin/rect.h"
#include "android/utils/compiler.h"

ANDROID_BEGIN_HEADER

/* Some commonly-used file names */

/* The name of the .ini file that contains the initial hardware
 * properties for the AVD. This file is specific to the AVD and
 * is in the AVD's directory.
 */
#define CORE_CONFIG_INI "config.ini"

/* The name of the .ini file that contains the complete hardware
 * properties for the AVD. This file is specific to the AVD and
 * is in the AVD's directory. This will be used to launch the
 * corresponding core from the UI.
 */
#define CORE_HARDWARE_INI "hardware-qemu.ini"

/* The file where the crash reporter holds a copy of
 * the hardware properties. This file is in a temporary
 * directory set up by the crash reporter.
 */
#define CRASH_AVD_HARDWARE_INFO "avd_info.txt"

/** in vl.c */

/* emulated network up/down speeds, expressed in bits/seconds */
extern double   qemu_net_upload_speed;
extern double   qemu_net_download_speed;

/* emulated network min-max latency, expressed in ms */
extern int      qemu_net_min_latency;
extern int      qemu_net_max_latency;

/* global flag, when true, network is disabled */
extern int      qemu_net_disable;

/* list of supported network speed names and values in bits/seconds */
typedef struct {
    const char*  name;
    const char*  display;
    int          upload;
    int          download;
} NetworkSpeed;

extern const NetworkSpeed   android_netspeeds[];
extern const size_t android_netspeeds_count;

/* list of supported network latency names and min-max values in ms */
typedef struct {
    const char*  name;
    const char*  display;
    int          min_ms;
    int          max_ms;
} NetworkLatency;

extern const NetworkLatency  android_netdelays[];
extern const size_t android_netdelays_count;

/* default network settings for emulator */
#define  DEFAULT_NETSPEED  "full"
#define  DEFAULT_NETDELAY  "none"

/* enable/disable interrupt polling mode. the emulator will always use 100%
 * of host CPU time, but will get high-quality time measurments. this is
 * required for the tracing mode unless you can bear 10ms granularities
 */
extern void  qemu_polling_enable(void);
extern void  qemu_polling_disable(void);

/**in hw/android/goldfish/fb.c */

/* framebuffer dimensions in pixels, note these can change dynamically */
extern int  android_framebuffer_w;
extern int  android_framebuffer_h;
/* framebuffer dimensions in mm */
extern int  android_framebuffer_phys_w;
extern int  android_framebuffer_phys_h;

/**  in android_main.c */

/* this is the port used for the control console in this emulator instance.
 * starts at 5554, with increments of 2 */
extern int   android_base_port;

/* this is the port used to connect ADB (Android Debug Bridge)
 * default is 5037 */
extern int   android_adb_port;

/* parses a network speed parameter and sets qemu_net_upload_speed and
 * qemu_net_download_speed accordingly. returns -1 on failure, 0 on success */
extern int   android_parse_network_speed(const char*  speed);

/* parse a network delay parameter and sets qemu_net_min/max_latency
 * accordingly. returns -1 on error, 0 on success */
extern int   android_parse_network_latency(const char*  delay);

/**  in qemu_setup.c */

// Call this from QEMU1 to enable the AndroidEmu console code to be
// properly initialized from android_emulation_setup().
extern void android_emulation_setup_use_android_emu_console(bool enabled);

// Call this from QEMU1 to enable configurable ADB and console ports.
extern void android_emulation_setup_use_configurable_ports(bool enabled);

// See android/console.h
struct AndroidConsoleAgents;

// Setup Android emulation. Return true on success.
extern bool android_emulation_setup(const struct AndroidConsoleAgents* agents);

extern void  android_emulation_teardown( void );

ANDROID_END_HEADER

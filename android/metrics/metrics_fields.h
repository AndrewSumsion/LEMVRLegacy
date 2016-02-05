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

/* This file is intended to be included multiple times.
 * Do not add header guards.
 */

#ifndef METRICS_INT
#error METRICS_INT not defined
#endif
#ifndef METRICS_STRING
#error METRICS_STRING not defined
#endif
#ifndef METRICS_DURATION
#error METRICS_DURATION not defined
#endif

METRICS_STRING(emulator_version, "emulator_version", "unknown")
METRICS_STRING(host_os_type, "host_os_type", "unknown")
METRICS_STRING(guest_arch, "guest_arch", "unknown")
METRICS_INT(guest_gpu_enabled, "guest_gpu_enabled", -99)
METRICS_STRING(guest_gl_vendor, "guest_gl_vendor", "NotApplicable")
METRICS_STRING(guest_gl_renderer, "guest_gl_renderer", "NotApplicable")
METRICS_STRING(guest_gl_version, "guest_gl_version", "NotApplicable")
METRICS_INT(tick, "tick", 0)
METRICS_DURATION(system_time, "system_time", 0)
METRICS_DURATION(user_time, "user_time", 0)
METRICS_DURATION(wallclock_time, "wallclock_time", 0)
METRICS_INT(is_dirty, "is_dirty", 1)
METRICS_INT(opengl_alive, "opengl_alive", 0)
METRICS_INT(num_failed_reports, "num_failed_reports", 0)
METRICS_INT(adb_liveness, "adb_liveness", 0)
METRICS_INT(exit_started, "exit_started", 0)

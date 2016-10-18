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

#pragma once

#include "android/utils/compiler.h"

#include <stdbool.h>

ANDROID_BEGIN_HEADER

/* Call this function at the start of the QEMU main() function to perform
 * early setup of Android emulation. This will ensure the glue will inject
 * all relevant callbacks into QEMU2. As well as setup the looper for the
 * main thread. Return true on success, false otherwise. */
extern bool qemu_android_emulation_early_setup(void);

/* Call this function to setup a list of custom DNS servers to be used
 * by the network stack. |dns_servers| must be the content of the
 * -dns-server option, i.e. a comma-separated list of DNS server addresses.
 * On success, return true and set |*count4| and |*count6| to the number
 * of IPv4 and IPv6 IP addresses, respectively. Return false on failure. */
extern bool qemu_android_emulation_setup_dns_servers(const char* dns_servers,
                                                     int* count4,
                                                     int* count6);

/* Call this function after the slirp stack has been initialized, typically
 * by calling net_init_clients() in vl.c, to inject Android-specific features
 * (e.g. custom DNS server list) into the network stack. */
extern void qemu_android_emulation_init_slirp(void);

/* Call this function after the QEMU main() function has inited the
 * machine, but before it has started it. */
extern bool qemu_android_emulation_setup(void);

/* Call this function at the end of the QEMU main() function, just
 * after the main loop has returned due to a machine exit. */
extern void qemu_android_emulation_teardown(void);

ANDROID_END_HEADER

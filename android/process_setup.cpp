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

#include "android/process_setup.h"

#include "android/base/files/PathUtils.h"
#include "android/base/Log.h"
#include "android/base/system/System.h"
#include "android/curl-support.h"
#include "android/crashreport/crash-handler.h"
#include "android/utils/sockets.h"

using android::base::PathUtils;
using android::base::String;
using android::base::System;

// The order of initialization here can be very finicky. Handle with care, and
// leave hints about any ordering constraints via comments.
void process_early_setup() {
    // Initializae sockets first so curl/crash processor can use sockets.
    // Does not create any threads.
#ifdef _WIN32
    android_socket_init();
#endif

    // Catch crashes in everything.
    // This promises to not launch any threads...
    if (!crashhandler_init()) {
        LOG(VERBOSE) << "Crash handling not initialized";
    }

    // libcurl initialization is thread-unsafe, so let's call it first
    // to make sure no other thread could be doing the same
    String launcherDir = System::get()->getLauncherDirectory();
    String caBundleFile = PathUtils::join(launcherDir, "lib", "ca-bundle.pem");
    if (!System::get()->pathCanRead(caBundleFile)) {
        LOG(VERBOSE) << "Can not read ca-bundle. Curl init skipped.";
    } else {
        curl_init(caBundleFile.c_str());
    }
}

void process_late_teardown() {
    curl_cleanup();
}

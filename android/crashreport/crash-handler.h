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

#ifdef __cplusplus
#include "android/base/StringFormat.h"
#include <utility>
#include <stdio.h>
#endif

ANDROID_BEGIN_HEADER

// Enable crash reporting by starting crash service process, initializing and
// attaching crash handlers.  Should only be run once at the start of the
// program.  Returns true on success, or false on failure which
// can occur on the following conditions:
//    Missing executables / resource files
//    Non writable directory structure for crash dumps
//    Unable to start crash service process (init failure or timeout on
//    response)
//    Unable to connect to crash service process
//    Success on previous call
bool crashhandler_init(void);

// Abort the program execution immidiately; when showing a crash dialog, use
// show |message| to the user instead of standard 'emulator have crashed'
void crashhandler_die(const char* message) __attribute__((noinline));

// A variadic overload with C interface
void crashhandler_die_format(const char* format, ...);

ANDROID_END_HEADER

#ifdef __cplusplus
// A variadic overload for a convenient message formatting
template <class... Args>
void crashhandler_die(const char* format, Args&&... args) {
    char buffer[2048] = {};    // 2048 is enough for everyone ;)
    snprintf(buffer, sizeof(buffer) - 1, format,
             android::base::unpackFormatArg(std::forward<Args>(args))...);
    crashhandler_die(buffer);
}
#endif

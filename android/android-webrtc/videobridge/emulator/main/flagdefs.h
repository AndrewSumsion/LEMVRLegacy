// Copyright (C) 2018 The Android Open Source Project
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
#ifndef FLAGDEFS_H_
#define FLAGDEFS_H_

#include "rtc_base/flags.h"

WEBRTC_DEFINE_bool(help, false, "Prints this message");
WEBRTC_DEFINE_string(server, "127.0.0.1", "The server to connect to.");
WEBRTC_DEFINE_int(port, 5557, "The port to connect to.");
WEBRTC_DEFINE_string(handle, "video0", "The memory handle to read frames from");
WEBRTC_DEFINE_bool(verbose, false, "Enables logging to stdout");
#ifdef _WIN32
    WEBRTC_DEFINE_bool(daemon, false, "This flag is ignored in windows.");
#else
    WEBRTC_DEFINE_bool(daemon, false, "Run as a deamon, will print PID of deamon upon exit");
#endif
WEBRTC_DEFINE_string(turn, "", "Process that will be invoked to retrieve TURN json configuration.");
WEBRTC_DEFINE_string(logdir,
                     "",
                     "Directory to log files to, or empty when unused");
#endif  // FLAGDEFS_H_

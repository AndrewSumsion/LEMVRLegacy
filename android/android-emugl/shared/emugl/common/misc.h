// Copyright (C) 2016 The Android Open Source Project
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

#include "android/base/Profiler.h"

namespace emugl {
    // Set and get API version of system image.
    void setAvdInfo(bool isPhoneApi, int apiLevel);
    void getAvdInfo(bool* isPhoneApi, int* apiLevel);

    // Set/get GLES major/minor version.
    void setGlesVersion(int maj, int min);
    void getGlesVersion(int* maj, int* min);

    using Profiler = android::base::Profiler;
}

/* Copyright (C) 2007-2009 The Android Open Source Project
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

#include "android/utils/path.h"

#include "android/base/system/System.h"
#include "android/base/files/PathUtils.h"

using android::base::PathUtils;
using android::base::System;

ABool path_exists(const char* path) {
    return System::get()->pathExists(path);
}

ABool path_is_regular(const char* path) {
    return System::get()->pathIsFile(path);
}

ABool path_is_dir(const char*  path) {
    return System::get()->pathIsDir(path);
}

ABool path_is_absolute(const char* path) {
    return PathUtils::isAbsolute(path);
}

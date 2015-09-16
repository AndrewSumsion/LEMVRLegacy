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

#ifndef ANDROID_SETTINGS_AGENT_H
#define ANDROID_SETTINGS_AGENT_H

#include "android/utils/compiler.h"

ANDROID_BEGIN_HEADER


enum SettingsTheme { SETTINGS_THEME_LIGHT,
                     SETTINGS_THEME_DARK,
                     SETTINGS_THEME_NUM_ENTRIES };

typedef struct SettingsAgent {
    // Sets the IP port used for the Android Debug Bridge
    void (*setAdbPort)(int portNumber);

} SettingsAgent;

ANDROID_END_HEADER

#endif // ANDROID_SETTINGS_AGENT_H

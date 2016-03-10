/* Copyright (C) 2015-2016 The Android Open Source Project
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

// This file contains values that are used for saving and
// restoring user settings associated with the Qt-based UI.
#pragma once

#include <QString>

namespace Ui {
namespace Settings {

const QString ORG_NAME("Android Open Source Project");
const QString ORG_DOMAIN("android.com");
const QString APP_NAME("Emulator");

const QString SHOW_AVD_ARCH_WARNING("showAvdArchWarning");
const QString SHOW_GPU_WARNING("showGpuWarning");

// Note: The "set/" here is because these settings are
//       associated with "settings-page", not because
//       we're in "namespace Settings".

const QString ADB_PATH("set/adbPath");
const QString AUTO_FIND_ADB("set/autoFindAdb");
const QString ALWAYS_ON_TOP("set/alwaysOnTop");
const QString FORWARD_SHORTCUTS_TO_DEVICE("set/forwardShortcutsToDevice");
const QString SAVE_PATH("set/savePath");
const QString UI_THEME("set/theme");

const QString CRASHREPORT_PREFERENCE("set/crashReportPreference");
enum CRASHREPORT_PREFERENCE_VALUE {
    CRASHREPORT_PREFERENCE_ASK = 0,
    CRASHREPORT_PREFERENCE_ALWAYS = 1,
    CRASHREPORT_PREFERENCE_NEVER = 2
};
const QString CRASHREPORT_SAVEPREFERENCE_CHECKED("set/crashReportSavePreferenceChecked");
const int CRASHREPORT_COMBOBOX_ALWAYS = 0;
const int CRASHREPORT_COMBOBOX_NEVER = 1;
const int CRASHREPORT_COMBOBOX_ASK = 2;

const QString LOCATION_LATITUDE("loc/latitude");
const QString LOCATION_LONGITUDE("loc/longitude");
const QString LOCATION_ALTITUDE("loc/altitude");
const QString LOCATION_PLAYBACK_FILE("loc/playback_file_path");
const QString LOCATION_PLAYBACK_SPEED("loc/playback_speed");
}
}

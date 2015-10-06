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

#pragma once

#include "android/avd/info.h"
#include <stdint.h>

/* a structure used to model the user-configuration settings
 *
 * At the moment, this is only used to store the last position
 * of the emulator window and a unique 64-bit UUID. We might
 * add more AVD-specific preferences here in the future.
 *
 * By definition, these settings should be optional and we
 * should be able to work without them, unlike the AVD
 * configuration information found in config.ini
 */
typedef struct AUserConfig   AUserConfig;

/* Create a new AUserConfig object from a given AvdInfo */
AUserConfig*   auserConfig_new( AvdInfo*  info );

/* Retrieve the unique UID for this AVD */
uint64_t       auserConfig_getUUID( AUserConfig*  uconfig );

/* Retrieve the stored window position for this AVD */
void           auserConfig_getWindowPos( AUserConfig*  uconfig, int  *pX, int  *pY );

/* Change the stored window position for this AVD */
void           auserConfig_setWindowPos( AUserConfig*  uconfig, int  x, int  y );

int            auserConfig_getUiTheme( AUserConfig*  uconfig);
void           auserConfig_setUiTheme( AUserConfig*  uconfig, int theme );

/* Save the user configuration back to the content directory.
 * Should be used in an atexit() handler. This will effectively
 * only save the user configuration to disk if its content
 * has changed.
 */
void           auserConfig_save( AUserConfig*  uconfig );

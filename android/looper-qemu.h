/* Copyright (C) 2010 The Android Open Source Project
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
#ifndef ANDROID_LOOPER_QEMU_H
#define ANDROID_LOOPER_QEMU_H

#include "android/looper.h"
#include "android/utils/compiler.h"

ANDROID_BEGIN_HEADER

/* Create a new looper which is implemented on top of the QEMU main event
 * loop. You should only use this when implementing the emulator UI and Core
 * features in a single program executable.
 */
Looper*  looper_newCore(void);

ANDROID_END_HEADER

#endif  // ANDROID_LOOPER_QEMU_H

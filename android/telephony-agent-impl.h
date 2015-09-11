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

#ifndef ANDROID_TELEPHONY_AGENT_IMPL_H
#define ANDROID_TELEPHONY_AGENT_IMPL_H

#include "android/telephony-agent.h"

TelephonyResponse telephony_telephonyCmd(TelephonyOperation op, const char *phoneNumber);

#endif // ANDROID_TELEPHONY_AGENT_IMPL_H

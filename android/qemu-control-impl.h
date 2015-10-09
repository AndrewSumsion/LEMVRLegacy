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

#include "android/emulation/control/battery_agent.h"
#include "android/emulation/control/cellular_agent.h"
#include "android/emulation/control/finger_agent.h"
#include "android/emulation/control/location_agent.h"
#include "android/emulation/control/telephony_agent.h"
#include "android/emulation/control/user_event_agent.h"
#include "android/emulation/control/vm_operations.h"

ANDROID_BEGIN_HEADER

// This file contains handles to globally allocated objects implementing various
// interfaces required by AndroidEmu library in emulation/control/*.

// Defined in .../emulation/control/batter_agent.h
extern const QAndroidBatteryAgent* const gQAndroidBatteryAgent;

// Defined in android/qemu-cellular-agent.c
extern const QAndroidCellularAgent* const gQAndroidCellularAgent;

// Defined in android/qemu-finger-agent.c
extern const QAndroidFingerAgent* const gQAndroidFingerAgent;

// Defined in android/qemu-location-agent.c
extern const QAndroidLocationAgent* const gQAndroidLocationAgent;

// Defined in android/qemu-telephony-agent.c
extern const QAndroidTelephonyAgent* const gQAndroidTelephonyAgent;

// Defined in android-qemu1-glue/qemu-user-event-agent-impl.c
extern const QAndroidUserEventAgent* const gQAndroidUserEventAgent;
// Called by hw/android/goldfish/events_device.c to initialize generic event
// handling.
// In this QEMU1 specific implementation, we stash away an |opaque| handle the
// and call a function on the device directly.
extern void qemu_control_setEventDevice(void* opaque);

// Defined in android/qemu-android-vm-operations.c
extern const QAndroidVmOperations* const gQAndroidVmOperations;

ANDROID_END_HEADER

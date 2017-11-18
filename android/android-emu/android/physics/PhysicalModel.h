/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "android/hw-sensors.h"
#include "android/physics/Physics.h"
#include "android/utils/compiler.h"

ANDROID_BEGIN_HEADER

/* forward declarations */
struct QAndroidPhysicalStateAgent;
struct Stream;

/*
 * Implements a model of an ambient environment containing a rigid
 * body, and produces accurately simulated sensor values for various
 * sensors in this environment.
 *
 * The physical model should be updated with target ambient and rigid
 * body state, and regularly polled for the most recent sensor values.
 *
 * Components that only require updates when the model is actively
 * changing (i.e. not at rest) should register state change callbacks
 * via setStateChangingCallbacks.  TargetStateChange callbacks occur
 * on the same thread that setTargetXXXXXX is called from.  Sensor
 * state changing callbacks may occur on an arbitrary thread.
 */

typedef struct PhysicalModel {
    /* Opaque pointer used by the physical model c api. */
    void* opaque;
} PhysicalModel;

/* Allocate and initialize a physical model */
PhysicalModel* physicalModel_new(bool shouldTick);

/* Destroy and free a physical model */
void physicalModel_free(PhysicalModel* model);

/*
 * Update the current time on the physical model. Time values must be
 * non-decreasing.
 */
void physicalModel_setCurrentTime(PhysicalModel* model, uint64_t time_ns);

/* Target setters for all physical parameters */
#define SET_TARGET_FUNCTION_NAME(x) physicalModel_setTarget##x
#define PHYSICAL_PARAMETER_(x,y,z,w) void SET_TARGET_FUNCTION_NAME(z)(\
        PhysicalModel* model, w value, PhysicalInterpolation mode);
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME

/* Target getters for all physical parameters */
#define GET_PARAMETER_FUNCTION_NAME(x) physicalModel_getParameter##x
#define PHYSICAL_PARAMETER_(x,y,z,w) w GET_PARAMETER_FUNCTION_NAME(z)(\
        PhysicalModel* model, ParameterValueType parameterValueType);
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef GET_PARAMETER_FUNCTION_NAME

/* Overrides for all sensor values */
#define OVERRIDE_FUNCTION_NAME(x) physicalModel_override##x
#define SENSOR_(x,y,z,v,w) void OVERRIDE_FUNCTION_NAME(z)(\
        PhysicalModel* model,\
        v override_value);
SENSORS_LIST
#undef SENSOR_
#undef OVERRIDE_FUNCTION_NAME

/*
 * Get the current sensor value, along with a measurement id that can be
 * compared with previous measurement ids for the same sensor in order to
 * determine whether the value has been updated since a previous call to get
 * sensor data.
 */
#define GET_FUNCTION_NAME(x) physicalModel_get##x
#define SENSOR_(x,y,z,v,w) v GET_FUNCTION_NAME(z)(\
        PhysicalModel* model, long* measurement_id);
SENSORS_LIST
#undef SENSOR_
#undef GET_FUNCTION_NAME

/* Sets or unsets the callbacks used to signal changing state */
void physicalModel_setPhysicalStateAgent(PhysicalModel* model,
        const struct QAndroidPhysicalStateAgent* agent);

/* Save the physical model state to the specified stream. */
void physicalModel_save(PhysicalModel* model, Stream* f);

/* Load the physical model state from the specified stream. */
int physicalModel_load(PhysicalModel* model, Stream* f);

ANDROID_END_HEADER

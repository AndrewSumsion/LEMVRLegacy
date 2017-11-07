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

#include "android/utils/compiler.h"

#include "android/physics/physical_state_agent.h"

ANDROID_BEGIN_HEADER

typedef struct QAndroidSensorsAgent {
    // Sets the target values of a given physical parameter.
    // Input: |parameterId| determines which parameter's values to set, i.e.
    //                      PHYSICAL_PARAMETER_POSITION,
    //                      PHYSICAL_PARAMETER_ROTATITION,
    //                      etc.
    int (*setPhysicalParameterTarget)(
            int parameterId, float a, float b, float c,
            int interpolation_method);

    // Gets the target values of a given physical parameter.
    // Input: |parameterId| determines which parameter's values to retrieve.
    int (*getPhysicalParameterTarget)(
            int parameterId, float *a, float *b, float *c);

    // Sets the values of a given sensor to the specified override.
    // Input: |sensorId| determines which sensor's values to retrieve, i.e.
    //                   ANDROID_SENSOR_ACCELERATION,
    //                   ANDROID_SENSOR_GYROSCOPE,
    //                   etc.
    int (*setSensorOverride)(int sensorId, float a, float b, float c);

    // Reads the values from a given sensor.
    // Input: |sensorId| determines which sensor's values to retrieve, i.e.
    //                   ANDROID_SENSOR_ACCELERATION,
    //                   ANDROID_SENSOR_GYROSCOPE,
    //                   etc.
    int (*getSensor)(int sensorId, float *a, float *b, float *c);


    // Sets the agent used to receive callbacks to used to track whether the
    // state of the physical model is currently changing.
    int (*setPhysicalStateAgent)(const struct QAndroidPhysicalStateAgent* agent);
} QAndroidSensorsAgent;

ANDROID_END_HEADER

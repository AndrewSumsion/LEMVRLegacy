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

#include "android/utils/compiler.h"
#ifdef __cplusplus
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#endif  // __cplusplus

ANDROID_BEGIN_HEADER

typedef enum {
    PHYSICAL_INTERPOLATION_SMOOTH=0,
    PHYSICAL_INTERPOLATION_STEP=1,
} PhysicalInterpolation;

typedef enum {
    PARAMETER_VALUE_TYPE_TARGET=0,
    PARAMETER_VALUE_TYPE_CURRENT=1,
} ParameterValueType;

#ifdef __cplusplus

constexpr float kPhysicsEpsilon = 0.001f;

inline bool vecNearEqual(glm::vec3 lhs, glm::vec3 rhs) {
    return glm::all(glm::epsilonEqual(lhs, rhs, kPhysicsEpsilon));
}

inline bool quaternionNearEqual(glm::quat lhs, glm::quat rhs) {
    return glm::all(glm::epsilonEqual(lhs, rhs, kPhysicsEpsilon)) ||
           glm::all(glm::epsilonEqual(lhs, -rhs, kPhysicsEpsilon));
}

#endif  // __cplusplus

ANDROID_END_HEADER

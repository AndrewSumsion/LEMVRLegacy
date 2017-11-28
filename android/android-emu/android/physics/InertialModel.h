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

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <array>

#include "android/physics/Physics.h"

namespace android {
namespace physics {

typedef enum {
    INERTIAL_STATE_CHANGING=0,
    INERTIAL_STATE_STABLE=1,
} InertialState;

/*
 * Implements a model of inertial motion of a rigid body such that smooth
 * movement occurs bringing the body to the target rotation and position with
 * dependent sensor data constantly available and up to date.
 *
 * The inertial model should be used by sending it target positions and
 * then polling the current actual rotation and position, acceleration and
 * velocity values in order to find the current state of the rigid body.
 */
class InertialModel {
public:
    InertialModel() = default;

    /*
     * Sets the current time of the InertialModel simulation.  This time is
     * used as the current time in calculating current position, velocity and
     * acceleration, along with the time when target position/rotation change
     * requests are recorded as taking place.  Time values must be
     * non-decreasing.
     */
    InertialState setCurrentTime(uint64_t time_ns);

    /*
     * Sets the position that the modeled object should move toward.
     */
    void setTargetPosition(glm::vec3 position, PhysicalInterpolation mode);

    /*
     * Sets the velocity at which the modeled object should start moving.
     */
    void setTargetVelocity(glm::vec3 velocity, PhysicalInterpolation mode);

    /*
     * Sets the rotation that the modeled object should move toward.
     */
    void setTargetRotation(glm::quat rotation, PhysicalInterpolation mode);

    /*
     * Gets current simulated state and sensor values of the modeled object at
     * the most recently set current time (from setCurrentTime).
     */
    glm::vec3 getPosition(
            ParameterValueType parameterValueType = PARAMETER_VALUE_TYPE_CURRENT) const;
    glm::vec3 getVelocity(
            ParameterValueType parameterValueType = PARAMETER_VALUE_TYPE_CURRENT) const;
    glm::vec3 getAcceleration(
            ParameterValueType parameterValueType = PARAMETER_VALUE_TYPE_CURRENT) const;
    glm::quat getRotation(
            ParameterValueType parameterValueType = PARAMETER_VALUE_TYPE_CURRENT) const;
    // rotational velocity as rotation around (x, y, z) axis in rad/s
    glm::vec3 getRotationalVelocity(
            ParameterValueType parameterValueType = PARAMETER_VALUE_TYPE_CURRENT) const;

private:
    void updateRotations();

    // Helper for setting the transforms for position, velocity and acceleration
    // based on the coefficients for quintic motion.
    void setInertialTransforms(
            const glm::vec3 quinticCoefficient,
            const glm::vec3 quarticCoefficient,
            const glm::vec3 cubicCoefficient,
            const glm::vec3 quadraticCoefficient,
            const glm::vec3 linearCoefficient,
            const glm::vec3 constantCoefficient);

    // Helper for calculating the current or target state given a transform
    // specifying either the acceleration, velocity, or position.
    glm::vec3 calculateInertialState(
            const glm::mat2x3& quinticTransform,
            const glm::mat4x3& cubicTransform,
            const glm::mat4x3& afterEndCubicTransform,
            ParameterValueType parameterValueType) const;

    // Helper for calculating the current or target rotational state given a
    // transform specifying either the rotational velocity, or rotation in
    // 4d vector space.
    glm::vec4 calculateRotationalState(
        const glm::mat4x4& cubicTransform,
        ParameterValueType parameterValueType) const;

    // Note: Each target interpolation begins at a set time, accelerates at a
    //       for half of the target time, then decelerates for the second half
    //       with each half defined by a quartic polynomial such that the two
    //       halves are continuous up through the 3rd derivative.
    //
    //       Position/Velocity/Acceleration are calculated by multiplying a
    //       vector containing [t*t*t, t*t, t, 1] by the appropriate transform,
    //       and adding t*t*t*t multiplied by the quartic scale (if applicable -
    //       this is necessary because there is no glm::mat5x3 which would be
    //       necessary to do this calculation with a single matrix) where t is
    //       the amount of time in seconds since mAccelPhaseStartTime, and the
    //       AccelPhase transforms are used for the first half of the time
    //       between mAccelPhaseStartTime, and DecelPhaseTransforms for the
    //       second half.  At the end of the decel phase, the velocity and
    //       acceleration will be zero and the position will be as set in the
    //       target.
    uint64_t mPositionChangeStartTime = 0UL;
    glm::mat2x3 mPositionQuintic = glm::mat2x3(0.f);
    glm::mat4x3 mPositionCubic = glm::mat4x3(0.f);
    glm::mat2x3 mVelocityQuintic = glm::mat2x3(0.f);
    glm::mat4x3 mVelocityCubic = glm::mat4x3(0.f);
    glm::mat2x3 mAccelerationQuintic = glm::mat2x3(0.f);
    glm::mat4x3 mAccelerationCubic = glm::mat4x3(0.f);
    uint64_t mPositionChangeEndTime = 0UL;
    bool mZeroVelocityAfterEndTime = true;

    glm::mat4x3 mPositionAfterEndCubic = glm::mat4x3(0.f);
    glm::mat4x3 mVelocityAfterEndCubic = glm::mat4x3(0.f);

    uint64_t mRotationChangeStartTime = 0UL;
    glm::mat4x4 mRotationCubic = glm::mat4x4(
            glm::vec4(), glm::vec4(), glm::vec4(), glm::vec4(0.f, 0.f, 0.f, 1.f));
    glm::mat4x4 mRotationalVelocityCubic = glm::mat4x4(0.f);
    uint64_t mRotationChangeEndTime = 0UL;

    /* The time to use as current in this model */
    uint64_t mModelTimeNs = 0UL;
};

}  // namespace physics
}  // namespace android

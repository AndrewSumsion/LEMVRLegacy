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

#include "android/physics/PhysicalModel.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include "android/base/system/System.h"
#include "android/emulation/control/sensors_agent.h"
#include "android/hw-sensors.h"
#include "android/physics/AmbientEnvironment.h"
#include "android/physics/InertialModel.h"
#include "android/utils/debug.h"
#include "android/utils/stream.h"

#include <mutex>

#define D(...) VERBOSE_PRINT(sensors, __VA_ARGS__)

namespace android {
namespace physics {

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
class PhysicalModelImpl {
public:
    PhysicalModelImpl();
    ~PhysicalModelImpl();

    /*
     * Gets the PhysicalModel interface for this object.
     * Can be called from any thread.
     */
    PhysicalModel* getPhysicalModel();

    static PhysicalModelImpl* getImpl(PhysicalModel* physicalModel);

    /*
     * Sets the target value for the given physical parameter that the physical
     * model should move towards.
     * Can be called from any thread.
     */
#define SET_TARGET_FUNCTION_NAME(x) setTarget##x
#define PHYSICAL_PARAMETER_(x,y,z,w) void SET_TARGET_FUNCTION_NAME(z)(\
        w value, PhysicalInterpolation mode);
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME

    /*
     * Gets current target state of the modeled object.
     * Can be called from any thread.
     */
#define GET_TARGET_FUNCTION_NAME(x) getTarget##x
#define PHYSICAL_PARAMETER_(x,y,z,w) w GET_TARGET_FUNCTION_NAME(z)() const;
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME

    /*
     * Sets the current override states for each simulated sensor.
     * Can be called from any thread.
     */
#define OVERRIDE_FUNCTION_NAME(x) override##x
#define SENSOR_(x,y,z,v,w) void OVERRIDE_FUNCTION_NAME(z)(v override_value);
    SENSORS_LIST
#undef SENSOR_
#undef OVERRIDE_FUNCTION_NAME

    /*
     * Getters for all sensor values.
     * Can be called from any thread.
     */
#define GET_FUNCTION_NAME(x) get##x
#define SENSOR_(x,y,z,v,w) v GET_FUNCTION_NAME(z)(\
        long* measurement_id) const;
    SENSORS_LIST
#undef SENSOR_
#undef GET_FUNCTION_NAME

    /*
     * Set or unset callbacks used to signal changing state.
     * Can be called from any thread.
     */
    void setPhysicalStateAgent(const QAndroidPhysicalStateAgent* agent);

    /*
     * Save the current physical state to the given stream.
     */
    void save(Stream* f);

    /*
     * Load the current physical state from the given stream.
     */
    int load(Stream* f);
private:
    /*
     * Getters for non-overridden physical sensor values.
     */
#define GET_PHYSICAL_NAME(x) getPhysical##x
#define SENSOR_(x,y,z,v,w) v GET_PHYSICAL_NAME(z)() const;
    SENSORS_LIST
#undef SENSOR_
#undef GET_PHYSICAL_NAME

    /*
     * Helper for setting overrides.
     */
    template<class T>
    void setOverride(AndroidSensor sensor,
                     T* overrideMemberPointer,
                     T overrideValue) {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        physicalStateChanging();
        mUseOverride[sensor] = true;
        mMeasurementId[sensor]++;
        *overrideMemberPointer = overrideValue;
        physicalStateStabilized();
    }

    /*
     * Helper for getting current sensor values.
     */
    template<class T>
    T getSensorValue(AndroidSensor sensor,
                     const T* overrideMemberPointer,
                     std::function<T()> physicalGetter,
                     long* measurement_id) const {
        std::lock_guard<std::recursive_mutex> lock(mMutex);
        if (mUseOverride[sensor]) {
            *measurement_id = mMeasurementId[sensor];
            return *overrideMemberPointer;
        } else {
            if (mIsPhysicalStateChanging) {
                mMeasurementId[sensor]++;
            }
            *measurement_id = mMeasurementId[sensor];
            return physicalGetter();
        }
    }

    void physicalStateChanging();
    void physicalStateStabilized();
    void targetStateChanged();

    mutable std::recursive_mutex mMutex;

    InertialModel mInertialModel;
    AmbientEnvironment mAmbientEnvironment;

    const QAndroidPhysicalStateAgent* mAgent = nullptr;
    bool mIsPhysicalStateChanging = false;

    bool mUseOverride[MAX_SENSORS] = {false};
    mutable long mMeasurementId[MAX_SENSORS] = {0};
#define OVERRIDE_NAME(x) m##x##Override
#define SENSOR_(x,y,z,v,w) v OVERRIDE_NAME(z){0.f};
    SENSORS_LIST
#undef SENSOR_
#undef OVERRIDE_NAME

    PhysicalModel mPhysicalModelInterface;
};


static glm::vec3 toGlm(vec3 input) {
    return glm::vec3(input.x, input.y, input.z);
}

static vec3 fromGlm(glm::vec3 input) {
    vec3 value;
    value.x = input.x;
    value.y = input.y;
    value.z = input.z;
    return value;
}

PhysicalModelImpl::PhysicalModelImpl() {
    mPhysicalModelInterface.opaque = reinterpret_cast<void*>(this);
}

PhysicalModelImpl::~PhysicalModelImpl() {
    assert(mAgent == nullptr);
}

PhysicalModel* PhysicalModelImpl::getPhysicalModel() {
    return &mPhysicalModelInterface;
}

PhysicalModelImpl* PhysicalModelImpl::getImpl(PhysicalModel* physicalModel) {
    return physicalModel != nullptr ?
            reinterpret_cast<PhysicalModelImpl*>(physicalModel->opaque) :
            nullptr;
}

void PhysicalModelImpl::setTargetPosition(vec3 position,
        PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mInertialModel.setTargetPosition(toGlm(position), mode);
    targetStateChanged();
    physicalStateStabilized();
}

void PhysicalModelImpl::setTargetRotation(vec3 rotation,
        PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mInertialModel.setTargetRotation(
            glm::quat(glm::radians(toGlm(rotation))), mode);
    targetStateChanged();
    physicalStateStabilized();
}

void PhysicalModelImpl::setTargetMagneticField(
        vec3 field, PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mAmbientEnvironment.setMagneticField(
        field.x, field.y, field.z, mode);
    targetStateChanged();
    physicalStateStabilized();
}

void PhysicalModelImpl::setTargetTemperature(
        float celsius, PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mAmbientEnvironment.setTemperature(celsius, mode);
    targetStateChanged();
    physicalStateStabilized();
}

void PhysicalModelImpl::setTargetProximity(
        float centimeters, PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mAmbientEnvironment.setProximity(centimeters, mode);
    targetStateChanged();
    physicalStateStabilized();
}

void PhysicalModelImpl::setTargetLight(float lux, PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mAmbientEnvironment.setLight(lux, mode);
    targetStateChanged();
    physicalStateStabilized();
}

void PhysicalModelImpl::setTargetPressure(
        float hPa, PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mAmbientEnvironment.setPressure(hPa, mode);
    targetStateChanged();
    physicalStateStabilized();
}

void PhysicalModelImpl::setTargetHumidity(
        float percentage, PhysicalInterpolation mode) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    physicalStateChanging();
    mAmbientEnvironment.setHumidity(percentage, mode);
    targetStateChanged();
    physicalStateStabilized();
}

vec3 PhysicalModelImpl::getTargetPosition() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return fromGlm(mInertialModel.getPosition());
}

vec3 PhysicalModelImpl::getTargetRotation() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return fromGlm(glm::degrees(glm::eulerAngles(
            mInertialModel.getRotation())));
}

vec3 PhysicalModelImpl::getTargetMagneticField() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return fromGlm(mAmbientEnvironment.getMagneticField());
}

float PhysicalModelImpl::getTargetTemperature() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getTemperature();
}

float PhysicalModelImpl::getTargetProximity() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getProximity();
}

float PhysicalModelImpl::getTargetLight() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getLight();
}

float PhysicalModelImpl::getTargetPressure() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getPressure();
}

float PhysicalModelImpl::getTargetHumidity() const {
    std::lock_guard<std::recursive_mutex> lock(mMutex);
    return mAmbientEnvironment.getHumidity();
}

#define GET_FUNCTION_NAME(x) get##x
#define OVERRIDE_FUNCTION_NAME(x) override##x
#define OVERRIDE_NAME(x) m##x##Override
#define SENSOR_NAME(x) ANDROID_SENSOR_##x
#define PHYSICAL_NAME(x) getPhysical##x

// Implement sensor overrides.
#define SENSOR_(x,y,z,v,w) \
void PhysicalModelImpl::OVERRIDE_FUNCTION_NAME(z)(v override_value) {\
    setOverride(SENSOR_NAME(x), &OVERRIDE_NAME(z), override_value);\
}
SENSORS_LIST
#undef SENSOR_

// Implement getters that respect overrides.
#define SENSOR_(x,y,z,v,w) \
v PhysicalModelImpl::GET_FUNCTION_NAME(z)(\
        long* measurement_id) const {\
    return getSensorValue<v>(SENSOR_NAME(x),\
                             &OVERRIDE_NAME(z),\
                             std::bind(&PhysicalModelImpl::PHYSICAL_NAME(z),\
                                       this),\
                             measurement_id);\
}
SENSORS_LIST
#undef SENSOR_

#undef PHYSICAL_NAME
#undef SENSOR_NAME
#undef OVERRIDE_NAME
#undef OVERRIDE_FUNCTION_NAME
#undef GET_FUNCTION_NAME

vec3 PhysicalModelImpl::getPhysicalAccelerometer() const {
    //Implementation Note:
    //Qt's rotation is around fixed axis, in the following
    //order: z first, x second and y last
    //refs:
    //http://doc.qt.io/qt-5/qquaternion.html#fromEulerAngles
    //
    // Gravity and magnetic vectors as observed by the device.
    // Note how we're applying the *inverse* of the transformation
    // represented by device_rotation_quat to the "absolute" coordinates
    // of the vectors.
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
        (mInertialModel.getAcceleration() - mAmbientEnvironment.getGravity()));
}

vec3 PhysicalModelImpl::getPhysicalGyroscope() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
        mInertialModel.getRotationalVelocity());
}

vec3 PhysicalModelImpl::getPhysicalMagnetometer() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
        mAmbientEnvironment.getMagneticField());
}

/* (x, y, z) == (azimuth, pitch, roll) */
vec3 PhysicalModelImpl::getPhysicalOrientation() const {
    return fromGlm(glm::eulerAngles(mInertialModel.getRotation()));
}

float PhysicalModelImpl::getPhysicalTemperature() const {
    return mAmbientEnvironment.getTemperature();
}

float PhysicalModelImpl::getPhysicalProximity() const {
    return mAmbientEnvironment.getProximity();
}

float PhysicalModelImpl::getPhysicalLight() const {
    return mAmbientEnvironment.getLight();
}

float PhysicalModelImpl::getPhysicalPressure() const {
    return mAmbientEnvironment.getPressure();
}

float PhysicalModelImpl::getPhysicalHumidity() const {
    return mAmbientEnvironment.getHumidity();
}

vec3 PhysicalModelImpl::getPhysicalMagnetometerUncalibrated() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
        mAmbientEnvironment.getMagneticField());
}

vec3 PhysicalModelImpl::getPhysicalGyroscopeUncalibrated() const {
    return fromGlm(glm::conjugate(mInertialModel.getRotation()) *
        mInertialModel.getRotationalVelocity());
}

void PhysicalModelImpl::setPhysicalStateAgent(
        const QAndroidPhysicalStateAgent* agent) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);

    if (mAgent != nullptr && mAgent->onPhysicalStateStabilized != nullptr) {
        mAgent->onPhysicalStateStabilized(mAgent->context);
    }
    mAgent = agent;
    if (mAgent != nullptr) {
        if (mIsPhysicalStateChanging) {
            // Ensure the new agent is set correctly if the is a pending state
            // change.
            if (mAgent->onPhysicalStateChanging != nullptr) {
                mAgent->onPhysicalStateChanging(mAgent->context);
            }
        } else {
            // If no state change is pending, we still send a change/stabilize
            // message so that agents can depend on them for initialization.
            if (mAgent->onPhysicalStateChanging != nullptr) {
                mAgent->onPhysicalStateChanging(mAgent->context);
            }
            if (mAgent->onPhysicalStateStabilized != nullptr) {
                mAgent->onPhysicalStateStabilized(mAgent->context);
            }
        }
        // We send an initial target state change so agents can depend on this
        // for initialization.
        if (mAgent->onTargetStateChanged != nullptr) {
            mAgent->onTargetStateChanged(mAgent->context);
        }
    }
}

static void readValueFromStream(Stream* f, vec3* value) {
    value->x = stream_get_float(f);
    value->y = stream_get_float(f);
    value->z = stream_get_float(f);
}

static void readValueFromStream(Stream* f, float* value) {
    *value = stream_get_float(f);
}

static void writeValueToStream(Stream* f, vec3 value) {
    stream_put_float(f, value.x);
    stream_put_float(f, value.y);
    stream_put_float(f, value.z);
}

static void writeValueToStream(Stream* f, float value) {
    stream_put_float(f, value);
}

void PhysicalModelImpl::save(Stream* f) {
    std::lock_guard<std::recursive_mutex> lock(mMutex);

    // first save targets
    stream_put_be32(f, MAX_PHYSICAL_PARAMETERS);

    for (int parameter = 0;
         parameter < MAX_PHYSICAL_PARAMETERS;
         parameter++) {
        switch (parameter) {
#define PHYSICAL_PARAMETER_NAME(x) PHYSICAL_PARAMETER_##x
#define GET_TARGET_FUNCTION_NAME(x) getTarget##x
#define PHYSICAL_PARAMETER_(x,y,z,w) case PHYSICAL_PARAMETER_NAME(x): {\
                w targetValue = GET_TARGET_FUNCTION_NAME(z)();\
                writeValueToStream(f, targetValue);\
                break;\
            }
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME
#undef PHYSICAL_PARAMETER_NAME
            default:
                assert(false);  // should never happen
                break;
        }
    }

    // then save overrides
    stream_put_be32(f, MAX_SENSORS);

    for (int sensor = 0;
         sensor < MAX_SENSORS;
         sensor++) {
        stream_put_be32(f, mUseOverride[sensor]);
        if (mUseOverride[sensor]) {
            switch(sensor) {
#define SENSOR_NAME(x) ANDROID_SENSOR_##x
#define OVERRIDE_NAME(x) m##x##Override
#define SENSOR_(x,y,z,v,w)\
                case SENSOR_NAME(x): {\
                    writeValueToStream(f, OVERRIDE_NAME(z));\
                    break;\
                }
SENSORS_LIST
#undef SENSOR_
#undef OVERRIDE_NAME
#undef SENSOR_NAME
                default:
                    assert(false);  // should never happen
                    break;
            }
        }
    }
}

int PhysicalModelImpl::load(Stream* f) {
    // first load targets
    const int32_t num_physical_parameters = stream_get_be32(f);
    if (num_physical_parameters > MAX_PHYSICAL_PARAMETERS) {
        D("%s: cannot load: snapshot requires %d physical parameters, %d available\n",
          __FUNCTION__, num_physical_parameters, MAX_PHYSICAL_PARAMETERS);
        return -EIO;
    }

    // Note: any new target params will remain at their defaults.

    for (int parameter = 0;
         parameter < num_physical_parameters;
         parameter++) {
        switch (parameter) {
#define PHYSICAL_PARAMETER_NAME(x) PHYSICAL_PARAMETER_##x
#define SET_TARGET_FUNCTION_NAME(x) setTarget##x
#define PHYSICAL_PARAMETER_(x,y,z,w) case PHYSICAL_PARAMETER_NAME(x): {\
                w value;\
                readValueFromStream(f, &value);\
                SET_TARGET_FUNCTION_NAME(z)(\
                        value, PHYSICAL_INTERPOLATION_STEP);\
                break;\
            }
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME
#undef PHYSICAL_PARAMETER_NAME
            default:
                assert(false);  // should never happen
                break;
        }
    }

    // then load overrides

    /* check number of physical sensors */
    int32_t num_sensors = stream_get_be32(f);
    if (num_sensors > MAX_SENSORS) {
        D("%s: cannot load: snapshot requires %d physical sensors, %d available\n",
          __FUNCTION__, num_sensors, MAX_SENSORS);
        return -EIO;
    }

    for (int sensor = 0;
         sensor < num_sensors;
         sensor++) {
        if (stream_get_be32(f)) {
            switch(sensor) {
#define SENSOR_NAME(x) ANDROID_SENSOR_##x
#define OVERRIDE_FUNCTION_NAME(x) override##x
#define SENSOR_(x,y,z,v,w)\
                case SENSOR_NAME(x): {\
                    v value;\
                    readValueFromStream(f, &value);\
                    OVERRIDE_FUNCTION_NAME(z)(value);\
                    break;\
                }
SENSORS_LIST
#undef SENSOR_
#undef OVERRIDE_FUNCTION_NAME
#undef SENSOR_NAME
                default:
                    assert(false);  // should never happen
                    break;
            }
        }
    }

    return 0;
}

void PhysicalModelImpl::physicalStateChanging() {
    assert(!mIsPhysicalStateChanging);
    if (mAgent != nullptr && mAgent->onPhysicalStateChanging != nullptr) {
        mAgent->onPhysicalStateChanging(mAgent->context);
    }
    mIsPhysicalStateChanging = true;
}

void PhysicalModelImpl::physicalStateStabilized() {
    assert(mIsPhysicalStateChanging);
    if (mAgent != nullptr && mAgent->onPhysicalStateStabilized != nullptr) {
        mAgent->onPhysicalStateStabilized(mAgent->context);
    }
    // Increment all of the measurement ids because the physical state has
    // stabilized.
    for (int i = 0; i < MAX_SENSORS; i++) {
        mMeasurementId[i]++;
    }
    mIsPhysicalStateChanging = false;
}

void PhysicalModelImpl::targetStateChanged() {
    /* when target state changes we reset all sensor overrides */
    for (int i = 0; i < MAX_SENSORS; i++) {
        mUseOverride[i] = false;
    }
    if (mAgent != nullptr && mAgent->onTargetStateChanged != nullptr) {
        mAgent->onTargetStateChanged(mAgent->context);
    }
}

}  // namespace physics
}  // namespace android

using android::physics::PhysicalModelImpl;

PhysicalModel* physicalModel_new() {
    PhysicalModelImpl* impl = new PhysicalModelImpl();
    return impl != nullptr ? impl->getPhysicalModel() : nullptr;
}

void physicalModel_free(PhysicalModel* model) {
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);
    if (impl != nullptr) {
        delete impl;
    }
}

#define SET_PHYSICAL_TARGET_FUNCTION_NAME(x) physicalModel_setTarget##x
#define SET_TARGET_FUNCTION_NAME(x) setTarget##x
#define PHYSICAL_PARAMETER_(x,y,z,w) void SET_PHYSICAL_TARGET_FUNCTION_NAME(z)(\
        PhysicalModel* model, w value, PhysicalInterpolation mode) {\
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);\
    if (impl != nullptr) {\
        impl->SET_TARGET_FUNCTION_NAME(z)(value, mode);\
    }\
}
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME
#undef SET_PHYSICAL_TARGET_FUNCTION_NAME


#define GET_PHYSICAL_TARGET_FUNCTION_NAME(x) physicalModel_getTarget##x
#define GET_TARGET_FUNCTION_NAME(x) getTarget##x
#define PHYSICAL_PARAMETER_(x,y,z,w) w GET_PHYSICAL_TARGET_FUNCTION_NAME(z)(\
        PhysicalModel* model) {\
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);\
    w result;\
    if (impl != nullptr) {\
        result = impl->GET_TARGET_FUNCTION_NAME(z)();\
    } else {\
        result = {0.f};\
    }\
    return result;\
}
PHYSICAL_PARAMETERS_LIST
#undef PHYSICAL_PARAMETER_
#undef SET_TARGET_FUNCTION_NAME
#undef SET_PHYSICAL_TARGET_FUNCTION_NAME

#define OVERRIDE_FUNCTION_NAME(x) override##x
#define PHYSICAL_OVERRIDE_FUNCTION_NAME(x) physicalModel_override##x
#define SENSOR_(x,y,z,v,w) void PHYSICAL_OVERRIDE_FUNCTION_NAME(z)(\
        PhysicalModel* model,\
        v override_value) {\
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);\
    if (impl != nullptr) {\
        impl->OVERRIDE_FUNCTION_NAME(z)(override_value);\
    }\
}
SENSORS_LIST
#undef SENSOR_
#undef PHYSICAL_OVERRIDE_FUNCTION_NAME
#undef OVERRIDE_FUNCTION_NAME

#define GET_FUNCTION_NAME(x) get##x
#define PHYSICAL_GET_FUNCTION_NAME(x) physicalModel_get##x
#define SENSOR_(x,y,z,v,w) v PHYSICAL_GET_FUNCTION_NAME(z)(\
        PhysicalModel* model, long* measurement_id) {\
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);\
    *measurement_id = 0L;\
    return impl != nullptr ?\
            impl->GET_FUNCTION_NAME(z)(measurement_id) :\
            v{0.f};\
}
SENSORS_LIST
#undef SENSOR_
#undef PHYSICAL_GET_FUNCTION_NAME
#undef GET_FUNCTION_NAME

void physicalModel_setPhysicalStateAgent(PhysicalModel* model,
        const QAndroidPhysicalStateAgent* agent) {
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);
    if (impl != nullptr) {
        impl->setPhysicalStateAgent(agent);
    }
}

void physicalModel_save(PhysicalModel* model, Stream* f) {
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);
    if (impl != nullptr) {
        impl->save(f);
    }
}

int physicalModel_load(PhysicalModel* model, Stream* f) {
    PhysicalModelImpl* impl = PhysicalModelImpl::getImpl(model);
    if (impl != nullptr) {
        return impl->load(f);
    } else {
        return -EIO;
    }
}

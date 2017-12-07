// Copyright (C) 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
#include "android/skin/qt/extended-pages/virtual-sensors-page.h"

#include "android/emulation/control/sensors_agent.h"
#include "android/hw-sensors.h"
#include "android/metrics/PeriodicReporter.h"
#include "android/metrics/proto/studio_stats.pb.h"
#include "android/physics/Physics.h"
#include "android/skin/ui.h"

#include "android/skin/qt/stylesheet.h"

#include <QDesktopServices>
#include <QTextStream>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <array>
#include <cassert>

constexpr float kMetersPerInch = 0.0254f;

VirtualSensorsPage::VirtualSensorsPage(QWidget* parent)
    : QWidget(parent), mUi(new Ui::VirtualSensorsPage()) {
    mQAndroidPhysicalStateAgent.onTargetStateChanged = onTargetStateChanged;
    mQAndroidPhysicalStateAgent.onPhysicalStateChanging =
            onPhysicalStateChanging;
    mQAndroidPhysicalStateAgent.onPhysicalStateStabilized =
            onPhysicalStateStabilized;
    mQAndroidPhysicalStateAgent.context = reinterpret_cast<void*>(this);

    mUi->setupUi(this);
    // The initial values are set here to match the initial
    // values reported by an AVD.
    mUi->temperatureSensorValueWidget->setRange(-273.1, 100.0);
    mUi->temperatureSensorValueWidget->setValue(0.0);
    mUi->lightSensorValueWidget->setRange(0, 40000.0);
    mUi->lightSensorValueWidget->setValue(0.0);
    mUi->pressureSensorValueWidget->setRange(0, 1100);
    mUi->pressureSensorValueWidget->setValue(0.0);
    mUi->humiditySensorValueWidget->setRange(0, 100);
    mUi->humiditySensorValueWidget->setValue(0);
    mUi->proximitySensorValueWidget->setRange(0, 10);
    mUi->proximitySensorValueWidget->setValue(1);
    mUi->magNorthWidget->setLocale(QLocale::c());
    mUi->magEastWidget->setLocale(QLocale::c());
    mUi->magVerticalWidget->setLocale(QLocale::c());

    connect(mUi->accelWidget, SIGNAL(targetRotationChanged()), this,
            SLOT(propagateAccelWidgetChange()));
    connect(mUi->accelWidget, SIGNAL(targetPositionChanged()), this,
            SLOT(propagateAccelWidgetChange()));

    connect(this, &VirtualSensorsPage::updateResultingValuesRequired,
            this, &VirtualSensorsPage::updateResultingValues);

    connect(this, &VirtualSensorsPage::startSensorUpdateTimerRequired,
            this, &VirtualSensorsPage::startSensorUpdateTimer);

    connect(this, &VirtualSensorsPage::stopSensorUpdateTimerRequired,
            this, &VirtualSensorsPage::stopSensorUpdateTimer);

    connect(&mAccelerationTimer, SIGNAL(timeout()),
            this, SLOT(updateSensorValuesInUI()));
    mAccelerationTimer.setInterval(33);
    mAccelerationTimer.stop();

    mUi->zRotSlider->setRange(-180.0, 180.0);
    mUi->xRotSlider->setRange(-180.0, 180.0);
    mUi->yRotSlider->setRange(-180.0, 180.0);
    mUi->positionXSlider->setRange(Accelerometer3DWidget::MinX,
                                   Accelerometer3DWidget::MaxX);
    mUi->positionYSlider->setRange(Accelerometer3DWidget::MinY,
                                   Accelerometer3DWidget::MaxY);
    mUi->positionZSlider->setRange(Accelerometer3DWidget::MinZ,
                                   Accelerometer3DWidget::MaxZ);

    using android::metrics::PeriodicReporter;
    mMetricsReportingToken = PeriodicReporter::get().addCancelableTask(
            60 * 10 * 1000,  // reporting period
            [this](android_studio::AndroidStudioEvent* event) {
                if (mVirtualSensorsUsed) {
                    event->mutable_emulator_details()
                            ->mutable_used_features()
                            ->set_sensors(true);
                    mMetricsReportingToken.reset();  // Report it only once.
                    return true;
                }
                return false;
    });
}

VirtualSensorsPage::~VirtualSensorsPage() {
    // Unregister for physical state change callbacks.
    if (mSensorsAgent != nullptr) {
        mSensorsAgent->setPhysicalStateAgent(nullptr);
    }
}

void VirtualSensorsPage::on_rotateToPortrait_clicked() {
    setCoarseOrientation(ANDROID_COARSE_PORTRAIT);
}

void VirtualSensorsPage::on_rotateToLandscape_clicked() {
    setCoarseOrientation(ANDROID_COARSE_LANDSCAPE);
}

void VirtualSensorsPage::on_rotateToReversePortrait_clicked() {
    setCoarseOrientation(ANDROID_COARSE_REVERSE_PORTRAIT);
}

void VirtualSensorsPage::on_rotateToReverseLandscape_clicked() {
    setCoarseOrientation(ANDROID_COARSE_REVERSE_LANDSCAPE);
}

void VirtualSensorsPage::setSensorsAgent(const QAndroidSensorsAgent* agent) {

    if (mSensorsAgent != nullptr) {
        mSensorsAgent->setPhysicalStateAgent(nullptr);
    }
    mSensorsAgent = agent;

    mSensorsAgent->setPhysicalStateAgent(&mQAndroidPhysicalStateAgent);
    mUi->accelWidget->setSensorsAgent(agent);
}

// Helper function
void VirtualSensorsPage::setPhysicalParameterTarget(
        PhysicalParameter parameter_id,
        PhysicalInterpolation mode,
        double v1,
        double v2,
        double v3) {
    if (mSensorsAgent) {
        mIsUIModifyingPhysicalState = true;
        mSensorsAgent->setPhysicalParameterTarget(
                parameter_id,
                static_cast<float>(v1),
                static_cast<float>(v2),
                static_cast<float>(v3),
                mode);
        mIsUIModifyingPhysicalState = false;
    }
}

// Helper function
void VirtualSensorsPage::setCoarseOrientation(
        AndroidCoarseOrientation orientation) {
    if (mSensorsAgent) {
        mSensorsAgent->setCoarseOrientation(static_cast<int>(orientation));
    }
}

void VirtualSensorsPage::on_temperatureSensorValueWidget_valueChanged(
        double value) {
    if (!mFirstShow) mVirtualSensorsUsed = true;
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_TEMPERATURE,
            PHYSICAL_INTERPOLATION_SMOOTH,
            value);
}

void VirtualSensorsPage::on_proximitySensorValueWidget_valueChanged(
        double value) {
    if (!mFirstShow) mVirtualSensorsUsed = true;
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_PROXIMITY,
            PHYSICAL_INTERPOLATION_SMOOTH,
            value);
}

void VirtualSensorsPage::on_lightSensorValueWidget_valueChanged(double value) {
    if (!mFirstShow) mVirtualSensorsUsed = true;
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_LIGHT,
            PHYSICAL_INTERPOLATION_SMOOTH,
            value);
}

void VirtualSensorsPage::on_pressureSensorValueWidget_valueChanged(
    double value) {
    if (!mFirstShow) mVirtualSensorsUsed = true;
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_PRESSURE,
            PHYSICAL_INTERPOLATION_SMOOTH,
            value);
}

void VirtualSensorsPage::on_humiditySensorValueWidget_valueChanged(
    double value) {
    if (!mFirstShow) mVirtualSensorsUsed = true;
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_HUMIDITY,
            PHYSICAL_INTERPOLATION_SMOOTH,
            value);
}

void VirtualSensorsPage::on_magNorthWidget_valueChanged(double value) {
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_MAGNETIC_FIELD,
            PHYSICAL_INTERPOLATION_SMOOTH,
            mUi->magNorthWidget->value(),
            mUi->magEastWidget->value(),
            mUi->magVerticalWidget->value());
}

void VirtualSensorsPage::on_magEastWidget_valueChanged(double value) {
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_MAGNETIC_FIELD,
            PHYSICAL_INTERPOLATION_SMOOTH,
            mUi->magNorthWidget->value(),
            mUi->magEastWidget->value(),
            mUi->magVerticalWidget->value());
}

void VirtualSensorsPage::on_magVerticalWidget_valueChanged(double value) {
    setPhysicalParameterTarget(
            PHYSICAL_PARAMETER_MAGNETIC_FIELD,
            PHYSICAL_INTERPOLATION_SMOOTH,
            mUi->magNorthWidget->value(),
            mUi->magEastWidget->value(),
            mUi->magVerticalWidget->value());
}

void VirtualSensorsPage::on_zRotSlider_valueChanged(double) {
    propagateSlidersChange();
}

void VirtualSensorsPage::on_xRotSlider_valueChanged(double) {
    propagateSlidersChange();
}

void VirtualSensorsPage::on_yRotSlider_valueChanged(double) {
    propagateSlidersChange();
}

void VirtualSensorsPage::on_positionXSlider_valueChanged(double) {
    propagateSlidersChange();
}

void VirtualSensorsPage::on_positionYSlider_valueChanged(double) {
    propagateSlidersChange();
}

void VirtualSensorsPage::on_positionZSlider_valueChanged(double) {
    propagateSlidersChange();
}

void VirtualSensorsPage::onTargetStateChanged() {
    glm::vec3 position;
    mSensorsAgent->getPhysicalParameter(PHYSICAL_PARAMETER_POSITION,
                                        &position.x, &position.y, &position.z,
                                        PARAMETER_VALUE_TYPE_TARGET);
    position *= (1.f / kMetersPerInch);

    glm::vec3 eulerDegrees;
    mSensorsAgent->getPhysicalParameter(
            PHYSICAL_PARAMETER_ROTATION, &eulerDegrees.x, &eulerDegrees.y,
            &eulerDegrees.z, PARAMETER_VALUE_TYPE_TARGET);

    const glm::quat rotation = glm::eulerAngleXYZ(
            glm::radians(eulerDegrees.x), glm::radians(eulerDegrees.y),
            glm::radians(eulerDegrees.z));

    mUi->accelWidget->setTargetPosition(position);
    mUi->accelWidget->setTargetRotation(rotation);

    // Convert the rotation to a quaternion so to simplify comparing for
    // equality.
    const glm::quat oldRotation =
            glm::eulerAngleXYZ(glm::radians(mSlidersTargetRotation.x),
                               glm::radians(mSlidersTargetRotation.y),
                               glm::radians(mSlidersTargetRotation.z));

    mSlidersUseCurrent = !vecNearEqual(position, mSlidersTargetPosition) ||
                         !quaternionNearEqual(rotation, oldRotation);

    if (!mIsUIModifyingPhysicalState) {
        updateUIFromModelCurrentState();
    }
}

void VirtualSensorsPage::startSensorUpdateTimer() {
    mAccelerationTimer.start();
}

void VirtualSensorsPage::stopSensorUpdateTimer() {
    mAccelerationTimer.stop();
    updateSensorValuesInUI();
}

void VirtualSensorsPage::onPhysicalStateChanging() {
    emit startSensorUpdateTimerRequired();
}

void VirtualSensorsPage::onPhysicalStateStabilized() {
    emit stopSensorUpdateTimerRequired();
}

void VirtualSensorsPage::onTargetStateChanged(void* context) {
    if (context != nullptr) {
        VirtualSensorsPage* virtual_sensors_page =
                reinterpret_cast<VirtualSensorsPage*>(context);
        virtual_sensors_page->onTargetStateChanged();
    }
}

void VirtualSensorsPage::onPhysicalStateChanging(void* context) {
    if (context != nullptr) {
        VirtualSensorsPage* virtual_sensors_page =
                reinterpret_cast<VirtualSensorsPage*>(context);
        virtual_sensors_page->onPhysicalStateChanging();
    }
}

void VirtualSensorsPage::onPhysicalStateStabilized(void* context) {
    if (context != nullptr) {
        VirtualSensorsPage* virtual_sensors_page =
                reinterpret_cast<VirtualSensorsPage*>(context);
        virtual_sensors_page->onPhysicalStateStabilized();
    }
}

void VirtualSensorsPage::updateResultingValues(glm::vec3 acceleration,
                                               glm::vec3 gyroscope,
                                               glm::vec3 device_magnetic_vector) {

    static const QString rotation_labels[] = {
        "ROTATION_0",
        "ROTATION_90",
        "ROTATION_180",
        "ROTATION_270"
    };

    // Update labels with new values.
    QString table_html;
    QTextStream table_html_stream(&table_html);
    table_html_stream.setRealNumberPrecision(2);
    table_html_stream.setNumberFlags(table_html_stream.numberFlags() |
                                     QTextStream::ForcePoint);
    table_html_stream.setRealNumberNotation(QTextStream::FixedNotation);
    table_html_stream
        << "<table border=\"0\""
        << "       cellpadding=\"3\" style=\"font-size:" <<
           Ui::stylesheetFontSize(Ui::FontSize::Medium) << "\">"
        << "<tr>"
        << "<td>" << tr("Accelerometer (m/s<sup>2</sup>)") << ":</td>"
        << "<td align=left>" << acceleration.x << "</td>"
        << "<td align=left>" << acceleration.y << "</td>"
        << "<td align=left>" << acceleration.z << "</td></tr>"
        << "<tr>"
        << "<td>" << tr("Gyroscope (rad/s)") << ":</td>"
        << "<td align=left>" << gyroscope.x << "</td>"
        << "<td align=left>" << gyroscope.y << "</td>"
        << "<td align=left>" << gyroscope.z << "</td></tr>"
        << "<tr>"
        << "<td>" << tr("Magnetometer (&mu;T)") << ":</td>"
        << "<td align=left>" << device_magnetic_vector.x << "</td>"
        << "<td align=left>" << device_magnetic_vector.y << "</td>"
        << "<td align=left>" << device_magnetic_vector.z << "</td></tr>"
        << "<tr><td>" << tr("Rotation")
        << ":</td><td colspan = \"3\" align=left>"
        << rotation_labels[mCoarseOrientation - SKIN_ROTATION_0]
        << "</td></tr>"
        << "</table>";
    mUi->resultingAccelerometerValues->setText(table_html);
}

/*
 * Propagate a UI change from the accel widget to the sliders and model.
 */
void VirtualSensorsPage::propagateAccelWidgetChange() {
    updateModelFromAccelWidget(PHYSICAL_INTERPOLATION_SMOOTH);
}

/*
 * Propagate a UI change from the sliders to the accel widget and model.
 */
void VirtualSensorsPage::propagateSlidersChange() {
    updateModelFromSliders(PHYSICAL_INTERPOLATION_SMOOTH);
}

/*
 * Send the accel widget's position and rotation to the model as the new
 * targets.
 */
void VirtualSensorsPage::updateModelFromAccelWidget(
        PhysicalInterpolation mode) {
    const glm::vec3& position =
            kMetersPerInch * mUi->accelWidget->targetPosition();
    glm::vec3 rotationRadians;
    glm::extractEulerAngleXYZ(
            glm::mat4_cast(mUi->accelWidget->targetRotation()),
            rotationRadians.x, rotationRadians.y, rotationRadians.z);
    const glm::vec3 rotationDegrees = glm::degrees(rotationRadians);

    setPhysicalParameterTarget(PHYSICAL_PARAMETER_POSITION, mode, position.x,
                               position.y, position.z);
    setPhysicalParameterTarget(PHYSICAL_PARAMETER_ROTATION, mode,
                               rotationDegrees.x, rotationDegrees.y,
                               rotationDegrees.z);
}

/*
 * Send the slider position and rotation to the model as the new targets.
 */
void VirtualSensorsPage::updateModelFromSliders(PhysicalInterpolation mode) {
    glm::vec3 position(mUi->positionXSlider->getValue(),
                       mUi->positionYSlider->getValue(),
                       mUi->positionZSlider->getValue());

    const glm::vec3 rotationDegrees(mUi->xRotSlider->getValue(),
                                    mUi->yRotSlider->getValue(),
                                    mUi->zRotSlider->getValue());

    mSlidersTargetPosition = position;
    mSlidersTargetRotation = rotationDegrees;

    position = kMetersPerInch * position;

    setPhysicalParameterTarget(PHYSICAL_PARAMETER_POSITION, mode,
            position.x, position.y, position.z);
    setPhysicalParameterTarget(PHYSICAL_PARAMETER_ROTATION, mode,
            rotationDegrees.x, rotationDegrees.y, rotationDegrees.z);
}

/*
 * Update the UI to reflect the underlying model state.
 */
void VirtualSensorsPage::updateUIFromModelCurrentState() {
    if (mSensorsAgent != nullptr) {
        glm::vec3 position;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_POSITION, &position.x, &position.y,
                &position.z, PARAMETER_VALUE_TYPE_CURRENT);
        position = (1.f / kMetersPerInch) * position;

        glm::vec3 eulerDegrees;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_ROTATION, &eulerDegrees.x, &eulerDegrees.y,
                &eulerDegrees.z, PARAMETER_VALUE_TYPE_CURRENT);

        mUi->accelWidget->update();

        if (mSlidersUseCurrent) {
            mUi->xRotSlider->setValue(eulerDegrees.x, false);
            mUi->yRotSlider->setValue(eulerDegrees.y, false);
            mUi->zRotSlider->setValue(eulerDegrees.z, false);

            mUi->positionXSlider->setValue(position.x, false);
            mUi->positionYSlider->setValue(position.y, false);
            mUi->positionZSlider->setValue(position.z, false);
        }

        float scratch0, scratch1;

        float temperature;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_TEMPERATURE, &temperature,
                &scratch0, &scratch1, PARAMETER_VALUE_TYPE_TARGET);
        mUi->temperatureSensorValueWidget->setValue(temperature, false);

        glm::vec3 magneticField;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_MAGNETIC_FIELD,
                &magneticField.x, &magneticField.y, &magneticField.z,
                PARAMETER_VALUE_TYPE_TARGET);
        mUi->magNorthWidget->setValue(magneticField.x);
        mUi->magEastWidget->setValue(magneticField.y);
        mUi->magVerticalWidget->setValue(magneticField.z);

        float proximity;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_PROXIMITY, &proximity,
                &scratch0, &scratch1,
                PARAMETER_VALUE_TYPE_TARGET);
        mUi->proximitySensorValueWidget->setValue(proximity, false);

        float light;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_LIGHT, &light,
                &scratch0, &scratch1,
                PARAMETER_VALUE_TYPE_TARGET);
        mUi->lightSensorValueWidget->setValue(light, false);

        float pressure;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_PRESSURE, &pressure,
                &scratch0, &scratch1,
                PARAMETER_VALUE_TYPE_TARGET);
        mUi->pressureSensorValueWidget->setValue(pressure, false);

        float humidity;
        mSensorsAgent->getPhysicalParameter(
                PHYSICAL_PARAMETER_HUMIDITY, &humidity,
                &scratch0, &scratch1,
                PARAMETER_VALUE_TYPE_TARGET);
        mUi->humiditySensorValueWidget->setValue(humidity, false);
    }
}

/*
 * Update the sensor readings in the UI to match the current readings from the
 * inertial model.
 */
void VirtualSensorsPage::updateSensorValuesInUI() {
    updateUIFromModelCurrentState();

    if (mSensorsAgent != nullptr) {
        glm::vec3 gravity_vector(0.0f, 9.81f, 0.0f);

        glm::vec3 device_accelerometer;
        mSensorsAgent->getSensor(ANDROID_SENSOR_ACCELERATION,
                &device_accelerometer.x,
                &device_accelerometer.y,
                &device_accelerometer.z);
        glm::vec3 normalized_accelerometer =
            glm::normalize(device_accelerometer);

        // Update the "rotation" label according to the current acceleraiton.
        static const std::array<std::pair<glm::vec3, SkinRotation>, 4> directions {
            std::make_pair(glm::vec3(0.0f, 1.0f, 0.0f), SKIN_ROTATION_0),
            std::make_pair(glm::vec3(-1.0f, 0.0f, 0.0f), SKIN_ROTATION_90),
            std::make_pair(glm::vec3(0.0f, -1.0f, 0.0f), SKIN_ROTATION_180),
            std::make_pair(glm::vec3(1.0f, 0.0f, 0.0f), SKIN_ROTATION_270)
        };
        QString rotation_label;
        SkinRotation coarse_orientation = mCoarseOrientation;
        for (const auto& v : directions) {
            if (fabs(glm::dot(normalized_accelerometer, v.first) - 1.f) <
                    0.1f) {
                coarse_orientation = v.second;
                break;
            }
        }
        if (coarse_orientation != mCoarseOrientation) {
            mCoarseOrientation = coarse_orientation;
            // Signal to the extended-window to rotate the emulator window
            // since an orientation has been detected in the sensor values.
            emit(coarseOrientationChanged(mCoarseOrientation));
        }

        glm::vec3 device_magnetometer;
        mSensorsAgent->getSensor(ANDROID_SENSOR_MAGNETIC_FIELD,
                &device_magnetometer.x,
                &device_magnetometer.y,
                &device_magnetometer.z);

        glm::vec3 device_gyroscope;
        mSensorsAgent->getSensor(ANDROID_SENSOR_GYROSCOPE,
                &device_gyroscope.x,
                &device_gyroscope.y,
                &device_gyroscope.z);

        // Emit a signal to update the UI. We cannot just update
        // the UI here because the current function is sometimes
        // called from a non-Qt thread.
        // We only block signals for this widget if it's running on the Qt
        // thread, so it's Ok to call the connected function directly.
        if (signalsBlocked()) {
            updateResultingValues(
                    device_accelerometer,
                    device_gyroscope,
                    device_magnetometer);
        } else {
            emit updateResultingValuesRequired(
                    device_accelerometer,
                    device_gyroscope,
                    device_magnetometer);
        }
    }
}

void VirtualSensorsPage::on_accelModeRotate_toggled() {
    if (mUi->accelModeRotate->isChecked()) {
        mUi->accelWidget->setOperationMode(
            Accelerometer3DWidget::OperationMode::Rotate);
        mUi->accelerometerSliders->setCurrentIndex(0);
    }
}

void VirtualSensorsPage::on_accelModeMove_toggled() {
    if (mUi->accelModeMove->isChecked()) {
        mUi->accelWidget->setOperationMode(
            Accelerometer3DWidget::OperationMode::Move);
        mUi->accelerometerSliders->setCurrentIndex(1);
    }
}

void VirtualSensorsPage::on_helpMagneticField_clicked() {
    QDesktopServices::openUrl(QUrl::fromEncoded(
            "https://developer.android.com/reference/android/hardware/Sensor.html#TYPE_MAGNETIC_FIELD"));
}

void VirtualSensorsPage::on_helpLight_clicked() {
    QDesktopServices::openUrl(QUrl::fromEncoded(
            "https://developer.android.com/reference/android/hardware/Sensor.html#TYPE_LIGHT"));
}

void VirtualSensorsPage::on_helpPressure_clicked() {
    QDesktopServices::openUrl(QUrl::fromEncoded(
            "https://developer.android.com/reference/android/hardware/Sensor.html#TYPE_PRESSURE"));
}

void VirtualSensorsPage::on_helpAmbientTemp_clicked() {
  QDesktopServices::openUrl(QUrl::fromEncoded(
            "https://developer.android.com/reference/android/hardware/Sensor.html#TYPE_AMBIENT_TEMPERATURE"));
}

void VirtualSensorsPage::on_helpProximity_clicked() {
  QDesktopServices::openUrl(QUrl::fromEncoded(
            "https://developer.android.com/reference/android/hardware/Sensor.html#TYPE_PROXIMITY"));
}

void VirtualSensorsPage::on_helpHumidity_clicked() {
  QDesktopServices::openUrl(QUrl::fromEncoded(
            "https://developer.android.com/reference/android/hardware/Sensor.html#TYPE_RELATIVE_HUMIDITY"));
}

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
#pragma once

#include "ui_virtual-sensors-page.h"

#include "android/skin/file.h"

#include <QDoubleValidator>
#include <QTimer>
#include <QWidget>

#include <memory>

struct QAndroidSensorsAgent;
class VirtualSensorsPage : public QWidget
{
    Q_OBJECT

public:
    explicit VirtualSensorsPage(QWidget* parent = 0);

    void setSensorsAgent(const QAndroidSensorsAgent* agent);
    void setLayoutChangeNotifier(QObject* layout_change_notifier);

private slots:
    void on_temperatureSensorValueWidget_valueChanged(double value);
    void on_proximitySensorValueWidget_valueChanged(double value);
    void on_lightSensorValueWidget_valueChanged(double value);
    void on_pressureSensorValueWidget_valueChanged(double value);
    void on_humiditySensorValueWidget_valueChanged(double value);
    void on_accelModeRotate_toggled();
    void on_accelModeMove_toggled();

    void onMagVectorChanged();
    void updateLinearAcceleration();
    void onPhoneRotationChanged();
    void onPhonePositionChanged();
    void onDragStarted() { mAccelerationTimer.start(); }
    void onDragStopped() {
        mLinearAcceleration = QVector3D(0, 0, 0);
        updateAccelerometerValues();
        mAccelerationTimer.stop();
    }
    void onSkinLayoutChange(bool next);

private slots:
    void on_rotateToPortrait_clicked();
    void on_rotateToLandscape_clicked();
    void on_rotateToReversePortrait_clicked();
    void on_rotateToReverseLandscape_clicked();
    void on_helpMagneticField_clicked();
    void on_helpLight_clicked();
    void on_helpPressure_clicked();

private:
    void showEvent(QShowEvent*) override;

    void resetAccelerometerRotation(const QQuaternion&);
    void resetAccelerometerRotationFromSkinLayout(const SkinLayout*);
    void updateAccelerometerValues();

    std::unique_ptr<Ui::VirtualSensorsPage> mUi;
    QDoubleValidator mMagFieldValidator;
    const QAndroidSensorsAgent* mSensorsAgent;
    QVector3D mLinearAcceleration;
    QVector3D mPrevPosition;
    QVector3D mCurrentPosition;
    QTimer mAccelerationTimer;
    bool mFirstShow = true;
};


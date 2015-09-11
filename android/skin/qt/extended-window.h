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

#ifndef ANDROID_SKIN_QT_EXTENDED_WINDOW_H
#define ANDROID_SKIN_QT_EXTENDED_WINDOW_H

#include "android/ui-emu-agent.h"

#include <QFile>
#include <QFrame>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTimer>
#include <QValidator>

#include "android/battery-agent.h"

class EmulatorQtWindow;
class ToolWindow;

namespace Ui {
    class ExtendedControls;
}

class ExtendedWindow : public QFrame
{
    Q_OBJECT

public:
    explicit ExtendedWindow(EmulatorQtWindow *eW, ToolWindow *tW, const UiEmuAgent *agentPtr);

    void     completeInitialization();

private:

    ~ExtendedWindow();
    void closeEvent(QCloseEvent *ce);

    EmulatorQtWindow   *mParentWindow;
    ToolWindow         *mToolWindow;
    bool                mThemeIsDark;

    class BatteryState {
    public:
        bool          mIsCharging;
        int           mChargeLevel; // Percent
        BatteryHealth mHealth;
        BatteryStatus mStatus;

        BatteryState() :
            mIsCharging(true),
            mChargeLevel(50),
            mHealth(BATTERY_HEALTH_GOOD),
            mStatus(BATTERY_STATUS_CHARGING) { }
    };

    void initBattery();

    BatteryState    mBatteryState;

    const BatteryAgent    *mBatteryAgent;

    Ui::ExtendedControls *mExtendedUi;

    void    adjustTabs(QPushButton *thisButton, int thisIndex);

    void    setButtonEnabled(QPushButton *theButton, bool isEnabled);

private slots:
    void on_theme_pushButton_clicked(); // ?? Temporary

    // Master tabs
    void on_batteryButton_clicked();
    void on_cellularButton_clicked();
    void on_fingerButton_clicked();
    void on_locationButton_clicked();
    void on_messageButton_clicked();
    void on_sdButton_clicked();
    void on_telephoneButton_clicked();

    // Battery
    void on_bat_chargeCkBox_toggled(bool checked);
    void on_bat_levelSlider_valueChanged(int value);
    void on_bat_healthBox_currentIndexChanged(int index);
    void on_bat_statusBox_currentIndexChanged(int index);
};

#endif // ANDROID_SKIN_QT_EXTENDED_WINDOW_H

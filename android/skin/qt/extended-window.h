/* Copyright (C) 2015-2016 The Android Open Source Project
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

#include "android/emulation/control/location_agent.h"
#include "android/emulation/control/sensors_agent.h"
#include "android/emulation/control/user_event_agent.h"
#include "android/gps/GpsFix.h"
#include "android/hw-sensors.h"
#include "android/settings-agent.h"
#include "android/skin/qt/extended-window-styles.h"
#include "android/skin/qt/shortcut-key-store.h"
#include "android/skin/qt/size-tweaker.h"
#include "android/skin/qt/qt-ui-commands.h"
#include "android/skin/qt/extended-pages/common.h"
#include "android/ui-emu-agent.h"
#include "android/utils/path.h"

#include <QFile>
#include <QFrame>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QDoubleValidator>
#include <QShowEvent>

#include <vector>
#include <map>

class EmulatorQtWindow;
class ToolWindow;
class GeoDataLoaderThread;

namespace Ui {
    class ExtendedControls;
}

class ExtendedWindow : public QFrame
{
    Q_OBJECT

public:
    ExtendedWindow(
        EmulatorQtWindow *eW,
        ToolWindow *tW,
        const UiEmuAgent *agentPtr,
        const ShortcutKeyStore<QtUICommand>* shortcuts);

    void showPane(ExtendedWindowPane pane);
   
private slots:
    void switchOnTop(bool isOntop);
    void switchToTheme(SettingsTheme theme);

    // Master tabs
    void on_batteryButton_clicked();
    void on_cellularButton_clicked();
    void on_dpadButton_clicked();
    void on_fingerButton_clicked();
    void on_helpButton_clicked();
    void on_locationButton_clicked();
    void on_settingsButton_clicked();
    void on_telephoneButton_clicked();
    
private:
    void closeEvent(QCloseEvent *ce) override;
    void adjustTabs(ExtendedWindowPane thisIndex);
    void showEvent(QShowEvent* e) override;

    EmulatorQtWindow *mEmulatorWindow;
    ToolWindow *mToolWindow;
    std::map<ExtendedWindowPane, QPushButton*> mPaneButtonMap;
    const ShortcutKeyStore<QtUICommand>* mQtUIShortcuts;
    Ui::ExtendedControls *mExtendedUi;
    bool mFirstShowEvent = true;
    SizeTweaker mSizeTweaker;
};


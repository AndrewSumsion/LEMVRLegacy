// Copyright (C) 2020 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/skin/qt/extended-pages/car-rotary-page.h"

#include <QtCore/qglobal.h>                              // for Q_OS_MAC
#include <qcoreevent.h>                                  // for QEvent (ptr only)
#include <qstring.h>                                     // for operator+, operat...

#include <QBitmap>                                       // for QBitmap
#include <QEvent>                                        // for QEvent
#include <QPixmap>                                       // for QPixmap
#include <QPushButton>                                   // for QPushButton

#include "android/base/system/System.h"                  // for System
#include "android/emulation/control/adb/AdbInterface.h"  // for AdbInterface
#include "android/globals.h"                             // for android_hw
#include "android/settings-agent.h"                      // for SettingsTheme
#include "android/skin/qt/emulator-qt-window.h"          // for EmulatorQtWindow
#include "android/skin/qt/extended-pages/common.h"       // for getSelectedTheme
#include "android/skin/qt/stylesheet.h"                  // for stylesheetValues

class QObject;
class QPushButton;
class QWidget;

using android::base::System;
using android::emulation::AdbInterface;
using android::emulation::OptionalAdbCommandResult;
using std::string;

/* set to 1 for debugging */
#define DEBUG 0

#if DEBUG >= 1
#define D(...) VERBOSE_PRINT(car_rotary, __VA_ARGS__)
#else
#define D(...) (void)0
#endif

static const System::Duration kAdbCommandTimeoutMs = 3000;

CarRotaryPage::CarRotaryPage(QWidget* parent)
        : QWidget(parent), mUi(new Ui::CarRotaryPage()), mEmulatorWindow(NULL) {
    mUi->setupUi(this);

    mAdbExecuteIsActive = false;

    // Temporarily hide mouse wheel widgets until mouse wheel scroll is implemented.
    // TODO(agathaman): renable widgets when mouse wheel scroll is implemented.
    mUi->carrotary_enableMouseWheel_label->setVisible(false);
    mUi->carrotary_enableMouseWheel_checkbox->setVisible(false);

    const struct {
        QPushButton* button;
        string cmd;
        string label;
    } buttons[] {
            {mUi->carrotary_upButton, "cmd car_service inject-key 280", "Up"},
            {mUi->carrotary_downButton, "cmd car_service inject-key 281", "Down"},
            {mUi->carrotary_leftButton, "cmd car_service inject-key 282", "Left"},
            {mUi->carrotary_rightButton, "cmd car_service inject-key 283", "Right"},
            {mUi->carrotary_clickButton, "cmd car_service inject-key 23", "Click"},
            {mUi->carrotary_counterclockwiseButton,
                    "cmd car_service inject-rotary", "CCW rotation"},
            {mUi->carrotary_clockwiseButton,
                    "cmd car_service inject-rotary -c true", "CW rotation"},
            {mUi->carrotary_backButton, "input keyevent 4", "Back"},
            {mUi->carrotary_homeButton, "input keyevent 3", "Home"}};

    for (const auto& button_info : buttons) {
        QPushButton* button = button_info.button;
        const string cmd = button_info.cmd;
        const string label = button_info.label;
        connect(button, &QPushButton::pressed, [button, cmd, label, this]() {
            toggleButtonPressed(button, cmd, label);
        });
        connect(button, &QPushButton::released, [button, cmd, label, this]() {
            toggleButtonReleased(button, cmd, label);
        });
    }

    remaskButtons();
    installEventFilter(this);
}

void CarRotaryPage::setAdbInterface(android::emulation::AdbInterface* adb) {
    mAdb = adb;
}

void CarRotaryPage::setEmulatorWindow(EmulatorQtWindow* eW) {
    mEmulatorWindow = eW;
}

void CarRotaryPage::toggleButtonPressed(QPushButton* button, const string cmd,
        const string label) {
    // Toggle icon to pressed icon theme
    toggleIconTheme(button, /* pressed= */ true);

    // Only handle long press for rotation buttons. Other buttons are handled by released.
    if (button != mUi->carrotary_counterclockwiseButton &&
            button != mUi->carrotary_clockwiseButton) {
        return;
    }

    int longPressIntervalMs = mUi->carrotary_longPressInterval->text().toInt();

    // If the long-press interval is zero, return since we don't want to be sending rotation
    // events every 0 seconds. Zero could be a result of the parse from carrotary_longPressInterval
    // failing or the user inputting the value 0 into the Qtextedit.
    if (longPressIntervalMs == 0) {
        return;
    }

    mLastPushButtonCmd = cmd;

    QObject::connect(&mLongPressTimer, &QTimer::timeout, this,
                                     &CarRotaryPage::executeLastPushButtonCmd);
    mLongPressTimer.setSingleShot(false);
    mLongPressTimer.setInterval(longPressIntervalMs);
    mLongPressTimer.start();
}

void CarRotaryPage::toggleButtonReleased(
        QPushButton* button, const string cmd, const string label) {
    D("%s button released", label.c_str());
    toggleIconTheme(button, /* pressed= */ false);
    if (mLongPressTimer.isActive()) {
        mLongPressTimer.stop();
    }
    mLastPushButtonCmd = cmd;
    executeLastPushButtonCmd();
}

void CarRotaryPage::toggleIconTheme(QPushButton* button, const bool pressed) {
    SettingsTheme theme = getSelectedTheme();
    QString iconName =
            button->property(pressed ? "themeIconNamePressed" : "themeIconName").toString();
    if (!iconName.isNull()) {
        QString resName = ":/" + Ui::stylesheetValues(theme)[Ui::THEME_PATH_VAR] + "/" + iconName;
        button->setIcon(QIcon(resName));
    }
}

void CarRotaryPage::executeLastPushButtonCmd() {
    if (!mLastPushButtonCmd.empty()) {
        mAdbExecuteIsActive = true;
        mAdbExecuteTime.restart();
        D("Executing cmd: %s", mLastPushButtonCmd.c_str());
        mAdb->runAdbCommand(
                {"shell", mLastPushButtonCmd},
                [this](const OptionalAdbCommandResult& result) {
                    mAdbExecuteIsActive = false;
                    D("ADB execute elapsed: %i", mAdbExecuteTime.elapsed());

                    string line;
                    if (result && result->output && getline(*(result->output), line)) {
                        D("ADB cmd completed. Result: %s", line.c_str());
                    } else {
                        D("ADB cmd completed with error.");
                    }
                },
                kAdbCommandTimeoutMs, true);
    }
}

void CarRotaryPage::remaskButtons() {
    for (QPushButton* button : findChildren<QPushButton*>()) {
        const QString icon_name = button->property("themeIconName").toString();
        if (!icon_name.isNull()) {
            QPixmap pixmap(":/dark/" + icon_name);
            button->setMask(pixmap.mask().scaled(button->size()));
            button->setStyleSheet("border: none;");
        }
    }
}

bool CarRotaryPage::eventFilter(QObject* o, QEvent* event) {
    if (event->type() == QEvent::ScreenChangeInternal) {
        // When moved across screens, masks on buttons need to
        // be adjusted according to screen density.
        remaskButtons();
    }
    return QWidget::eventFilter(o, event);
}

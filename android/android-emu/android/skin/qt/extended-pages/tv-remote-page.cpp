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

#include "android/skin/qt/extended-pages/tv-remote-page.h"

#include <QtCore/qglobal.h>                         // for Q_OS_MAC
#include <qcoreevent.h>                             // for QEvent (ptr only)
#include <qsize.h>                                  // for operator*
#include <qstring.h>                                // for operator+, operat...
#include <stddef.h>                                 // for NULL
#include <QAbstractButton>                          // for QAbstractButton
#include <QBitmap>                                  // for QBitmap
#include <QDesktopWidget>                           // for QDesktopWidget
#include <QEvent>                                   // for QEvent
#include <QHash>                                    // for QHash
#include <QIcon>                                    // for QIcon
#include <QLabel>                                   // for QLabel
#include <QList>                                    // for QList
#include <QPixmap>                                  // for QPixmap
#include <QPushButton>                              // for QPushButton
#include <QSettings>                                // for QSettings
#include <QSize>                                    // for QSize
#include <QVariant>                                 // for QVariant

#include "android/globals.h"                        // for android_hw
#include "android/settings-agent.h"                 // for SettingsTheme
#include "android/skin/event.h"                     // for SkinEvent, (anony...
#include "android/skin/keycode.h"                   // for kKeyCodeDpadCenter
#include "android/skin/qt/emulator-qt-window.h"     // for EmulatorQtWindow
#include "android/skin/qt/extended-pages/common.h"  // for getSelectedTheme
#include "android/skin/qt/stylesheet.h"             // for stylesheetValues

class QObject;
class QPushButton;
class QWidget;

#ifndef __APPLE__
#include <QApplication>
#include <QScreen>
#endif

TvRemotePage::TvRemotePage(QWidget *parent) :
    QWidget(parent),
    mUi(new Ui::TvRemotePage()),
    mEmulatorWindow(NULL)
{
    mUi->setupUi(this);
    const struct {
        QPushButton* button;
        SkinKeyCode key_code;
    } buttons[] = {
        {mUi->tvRemote_leftButton, kKeyCodeDpadLeft},
        {mUi->tvRemote_upButton, kKeyCodeDpadUp},
        {mUi->tvRemote_rightButton, kKeyCodeDpadRight},
        {mUi->tvRemote_downButton, kKeyCodeDpadDown},
        {mUi->tvRemote_selectButton, kKeyCodeDpadCenter},
        {mUi->tvRemote_backButton, kKeyCodeBack},
        {mUi->tvRemote_homeButton, kKeyCodeHome},
    };

    for (const auto& button_info : buttons) {
        QPushButton* button = button_info.button;
        const SkinKeyCode key_code = button_info.key_code;
        connect(button, &QPushButton::pressed,
                [button, key_code, this]() { toggleButtonEvent(button, key_code, kEventKeyDown); });
        connect(button, &QPushButton::released,
                [button, key_code, this]() { toggleButtonEvent(button, key_code, kEventKeyUp); });
    }

    remaskButtons();
    installEventFilter(this);
}

void TvRemotePage::setEmulatorWindow(EmulatorQtWindow* eW)
{
    mEmulatorWindow = eW;
}

void TvRemotePage::toggleButtonEvent(
    QPushButton* button,
    const SkinKeyCode key_code,
    const SkinEventType event_type) {
    if (mEmulatorWindow) {
        SkinEvent* skin_event = new SkinEvent();
        skin_event->type = event_type;
        skin_event->u.key.keycode = key_code;
        skin_event->u.key.mod = 0;
        mEmulatorWindow->queueSkinEvent(skin_event);
    }

    QSettings settings;
    SettingsTheme theme = getSelectedTheme();

    QString iconName =
        button->property(
            event_type == kEventKeyDown ? "themeIconNamePressed" : "themeIconName").toString();
    if ( !iconName.isNull() ) {
        QString resName =
            ":/" + Ui::stylesheetValues(theme)[Ui::THEME_PATH_VAR] + "/" + iconName;
        button->setIcon(QIcon(resName));
    }
}

void TvRemotePage::remaskButtons() {
    for (QPushButton* button : findChildren<QPushButton*>()) {
        const QString icon_name = button->property("themeIconName").toString();

        if (!icon_name.isNull()) {
            QPixmap pixmap(":/light/" + icon_name);
            button->setMask(pixmap.mask().scaled(button->size()));
            button->setStyleSheet("border: none;");
        }
    }
}

bool TvRemotePage::eventFilter(QObject* o, QEvent* event) {
    if (event->type() == QEvent::ScreenChangeInternal) {
        // When moved across screens, masks on buttons need to
        // be adjusted according to screen density.
        remaskButtons();
    }
    return QWidget::eventFilter(o, event);
}

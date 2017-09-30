// Copyright (C) 2016 The Android Open Source Project
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

#include "android/base/memory/OnDemand.h"
#include "android/skin/qt/OverlayMessageCenter.h"

#include <QObject>
#include <QScrollArea>
#include <QTimer>
#include <QWidget>
#include <QtCore>

class EmulatorQtWindow;
namespace Ui {
class ModalOverlay;
}

class EmulatorContainer : public QScrollArea {
    Q_OBJECT

public:
    explicit EmulatorContainer(EmulatorQtWindow* window);
    virtual ~EmulatorContainer();

    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* e) override;
    void focusInEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

    void showMinimized();

    void stopResizeTimer();
    QSize viewportSize() const;

    void prepareForRotation() { mRotating = true; }

    Ui::OverlayMessageCenter& messageCenter();

signals:
    void showModalOverlay(QString text);
    void hideModalOverlay();

private slots:
    void slot_resizeDone();
    void slot_showModalOverlay(QString text);
    void slot_hideModalOverlay();
    void slot_messagesResized();

private:
    void startResizeTimer();
    void adjustModalOverlayGeometry();
    void adjustMessagesOverlayGeometry();

    EmulatorQtWindow* mEmulatorWindow;
    Ui::ModalOverlay* mModalOverlay = nullptr;
    android::base::MemberOnDemandT<Ui::OverlayMessageCenter, QWidget*>
            mMessages;
    QList<QEvent::Type> mEventBuffer;
    QTimer mResizeTimer;
    bool mRotating = false;
};

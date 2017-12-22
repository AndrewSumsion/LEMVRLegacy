// Copyright (C) 2017 The Android Open Source Project
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

#include "android/skin/qt/qt-ui-commands.h"
#include "android/skin/qt/size-tweaker.h"
#include "android/ui-emu-agent.h"

#include "ui_virtualscene-controls.h"

#include <QFrame>
#include <QObject>
#include <QWidget>
#include <QtCore>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <memory>

class ToolWindow;

// Design requested a max width of 700 dp, and offset of 16 from the emulator
// window.  This is defined here.
static constexpr int kVirtualSceneControlWindowMaxWidth = 700;
static constexpr int kVirtualSceneControlWindowOffset = 16;

class VirtualSceneControlWindow : public QFrame {
    Q_OBJECT

public:
    explicit VirtualSceneControlWindow(ToolWindow* toolWindow, QWidget* parent);
    virtual ~VirtualSceneControlWindow();

    bool handleQtKeyEvent(QKeyEvent* event, QtKeyEventSource source);
    void updateTheme(const QString& styleSheet);

    void setAgent(const UiEmuAgent* agentPtr);
    void setWidth(int width);
    void setCaptureMouse(bool capture);

    bool eventFilter(QObject* target, QEvent* event) override;

    void hideEvent(QHideEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent*) override;

    void setActive(bool active);
    bool isActive();

signals:
    void virtualSceneControlsEngaged(bool engaged);

private slots:
    void slot_mousePoller();

private:
    void updateMouselook();
    void updateHighlightStyle();
    QString getInfoText();

    // Returns true if the event was handled.
    bool handleKeyEvent(QKeyEvent* event);
    void updateVelocity();

    QPoint getMouseCaptureCenter();

    ToolWindow* mToolWindow = nullptr;
    SizeTweaker mSizeTweaker;
    std::unique_ptr<Ui::VirtualSceneControls> mControlsUi;

    bool mCaptureMouse = false;
    QTimer mMousePoller;
    QPoint mOriginalMousePosition;
    QPoint mPreviousMousePosition;

    bool mIsActive = false;

    const QAndroidSensorsAgent* mSensorsAgent = nullptr;
    glm::vec3 mVelocity = glm::vec3();
    glm::vec3 mEulerRotationRadians = glm::vec3();

    enum KeysHeldIndex {
        Held_W,
        Held_A,
        Held_S,
        Held_D,
        Held_Q,
        Held_E,
        Held_Count
    };

    bool mKeysHeld[Held_Count] = {};
};

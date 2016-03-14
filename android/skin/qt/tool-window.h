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

#pragma once

#include "android/base/containers/CircularBuffer.h"
#include "android/skin/event.h"
#include "android/skin/qt/extended-window-styles.h"
#include "android/skin/qt/size-tweaker.h"
#include "android/skin/qt/qt-ui-commands.h"
#include "android/skin/qt/set-ui-emu-agent.h"
#include "android/skin/qt/shortcut-key-store.h"
#include "android/skin/qt/ui-event-recorder.h"
#include "android/utils/compiler.h"

#include <QDir>
#include <QErrorMessage>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QKeyEvent>
#include <QMap>
#include <QMessageBox>
#include <QPair>
#include <QProcess>
#include <QProgressDialog>
#include <QToolButton>
#include <QUrl>
#include <QFrame>
#include <QQueue>

#include <memory>

#define REMOTE_DOWNLOADS_DIR "/sdcard/Download"
#define REMOTE_SCREENSHOT_FILE "/data/local/tmp/screen.png"

namespace Ui {
    class ToolControls;
}

class EmulatorQtWindow;
class ExtendedWindow;

typedef void(EmulatorQtWindow::*EmulatorQtWindowSlot)();

class ToolWindow : public QFrame
{
    Q_OBJECT
    using UIEventRecorderPtr =
        std::weak_ptr<UIEventRecorder<android::base::CircularBuffer>>;
public:
    ToolWindow(
            EmulatorQtWindow *emulatorWindow,
            QWidget *parent,
            UIEventRecorderPtr event_recorder);
    ~ToolWindow();

    void hide();
    void show();
    void dockMainWindow();
    void raiseMainWindow();
    void extendedIsClosing() { extendedWindow = NULL; }

    void setToolEmuAgent(const UiEmuAgent* agPtr) { uiEmuAgent = agPtr; }
    const UiEmuAgent* getUiEmuAgent() const { return uiEmuAgent; }

    QString getAdbFullPath(QStringList *args);
    QString getScreenshotSaveFile();

    void runAdbInstall(const QString &path);
    void runAdbPush(const QList<QUrl> &urls);
    void runAdbShellStopAndQuit();

    bool handleQtKeyEvent(QKeyEvent* event);

    // The designers want a gap between the main emulator
    // window and the tool bar. This is how big that gap is.
    static const int toolGap = 10;

    void setupAdbPath();

private:
    bool isAdbVersionCurrent(const std::string& sdkRootDirectory) const;
    void showAdbWarning();

    int adbShellStopRunner();
    void handleUICommand(QtUICommand cmd, bool down);

    template <class Action>
    void uiAgentAction(Action a) const;

    // Helper method, calls handleUICommand with
    // down equal to true and down equal to false.
    void handleUICommand(QtUICommand cmd) {
        handleUICommand(cmd, true);
        handleUICommand(cmd, false);
    }

    QToolButton *addButton(QGridLayout *layout, int row, int col,
                           const char *iconPath, QString tip,
                           EmulatorQtWindowSlot slot);
    void showOrRaiseExtendedWindow(ExtendedWindowPane pane);

    virtual void closeEvent(QCloseEvent* ce) override;
    virtual void mousePressEvent(QMouseEvent *event) override;
    virtual void paintEvent(QPaintEvent*) override;
    virtual void hideEvent(QHideEvent* event) override;

    QWidget *button_area;
    EmulatorQtWindow *emulator_window;
    ExtendedWindow *extendedWindow;
    QBoxLayout *top_layout;
    const UiEmuAgent *uiEmuAgent;
    Ui::ToolControls *toolsUi;
    QProcess mInstallProcess;
    QProcess mPushProcess;
    bool mStartedAdbStopProcess = false;
    QProgressDialog mPushDialog;
    QProgressDialog mInstallDialog;
    QQueue<QUrl> mFilesToPush;
    ShortcutKeyStore<QtUICommand> mShortcutKeyStore;
    bool mIsExtendedWindowActiveOnHide = false;
    QString mDetectedAdbPath;
    std::weak_ptr<UIEventRecorder<android::base::CircularBuffer>> mUIEventRecorder;
    SizeTweaker mSizeTweaker;
    QMessageBox mAdbWarningBox;

private slots:
    void on_back_button_pressed();
    void on_back_button_released();
    void on_close_button_clicked();
    void on_home_button_pressed();
    void on_home_button_released();
    void on_minimize_button_clicked();
    void on_more_button_clicked();
    void on_power_button_pressed();
    void on_power_button_released();
    void on_overview_button_pressed();
    void on_overview_button_released();
    void on_prev_layout_button_clicked();
    void on_next_layout_button_clicked();
    void on_scrShot_button_clicked();
    void on_volume_down_button_pressed();
    void on_volume_down_button_released();
    void on_volume_up_button_pressed();
    void on_volume_up_button_released();
    void on_zoom_button_clicked();

    void slot_installCanceled();
    void slot_installFinished(int exitStatus);

    void slot_pushCanceled();
    void slot_pushFinished(int exitStatus);

    void slot_adbWarningMessageAccepted();
};

typedef void(ToolWindow::*ToolWindowSlot)();

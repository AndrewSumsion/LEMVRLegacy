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

#include <QCoreApplication>
#include <QDateTime>
#include <QPushButton>
#include <QSettings>
#include <QtWidgets>

#include "android/android.h"
#include "android/avd/info.h"
#include "android/base/files/PathUtils.h"
#include "android/base/system/System.h"
#include "android/emulation/ConfigDirs.h"
#include "android/emulation/control/clipboard_agent.h"
#include "android/emulator-window.h"
#include "android/featurecontrol/FeatureControl.h"
#include "android/globals.h"
#include "android/hw-events.h"
#include "android/hw-sensors.h"
#include "android/main-common.h"
#include "android/skin/event.h"
#include "android/skin/keycode.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/error-dialog.h"
#include "android/skin/qt/extended-pages/common.h"
#include "android/skin/qt/extended-pages/location-page.h"
#include "android/skin/qt/extended-window.h"
#include "android/skin/qt/extended-window-styles.h"
#include "android/skin/qt/extended-window.h"
#include "android/skin/qt/qt-settings.h"
#include "android/skin/qt/qt-ui-commands.h"
#include "android/skin/qt/stylesheet.h"
#include "android/skin/qt/tool-window.h"
#include "android/utils/debug.h"

#include <cassert>
#include <string>

using Ui::Settings::SaveSnapshotOnExit;

ToolWindow::ExtendedWindowHolder::ExtendedWindowHolder(ToolWindow* tw)
    : mWindow(new ExtendedWindow(tw->mEmulatorWindow,
                                 tw,
                                 &tw->mShortcutKeyStore)) {
    if (auto recorderPtr = tw->mUIEventRecorder.lock()) {
        recorderPtr->startRecording(mWindow);
    }
    if (auto userActionsCounter = tw->mUserActionsCounter.lock()) {
        userActionsCounter->startCountingForExtendedWindow(mWindow);
    }

    if (tw->mUiEmuAgent) {
        mWindow->setAgent(tw->mUiEmuAgent);
    }

    // If extended window is created before the "..." button is pressed, it
    // should be hid until that button is actually pressed.
    mWindow->hide();
}

ToolWindow::ExtendedWindowHolder::~ExtendedWindowHolder() {
    // ExtendedWindow has slots with subscribers, so use deleteLater()
    // instead of a regular delete for it.
    mWindow->deleteLater();
}

extern "C" void qemu_system_powerdown_request();

ToolWindow::ToolWindow(EmulatorQtWindow* window,
                       QWidget* parent,
                       ToolWindow::UIEventRecorderPtr event_recorder,
                       ToolWindow::UserActionsCounterPtr user_actions_counter)
    : QFrame(parent),
      mEmulatorWindow(window),
      mExtendedWindow([this] { return std::make_tuple(this); }),
      mVirtualSceneControlWindow(this, parent),
      mUiEmuAgent(nullptr),
      mToolsUi(new Ui::ToolControls),
      mUIEventRecorder(event_recorder),
      mUserActionsCounter(user_actions_counter),
      mSizeTweaker(this) {
// "Tool" type windows live in another layer on top of everything in OSX, which
// is undesirable because it means the extended window must be on top of the
// emulator window. However, on Windows and Linux, "Tool" type windows are the
// only way to make a window that does not have its own taskbar item.
#ifdef __APPLE__
    Qt::WindowFlags flag = Qt::Dialog;
#else
    Qt::WindowFlags flag = Qt::Tool;
#endif
    setWindowFlags(flag | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    mToolsUi->setupUi(this);

    mToolsUi->mainLayout->setAlignment(Qt::AlignCenter);
    mToolsUi->winButtonsLayout->setAlignment(Qt::AlignCenter);
    mToolsUi->controlsLayout->setAlignment(Qt::AlignCenter);

    // Get the latest user selections from the user-config code.
    SettingsTheme theme = getSelectedTheme();
    adjustAllButtonsForTheme(theme);
    updateTheme(Ui::stylesheetForTheme(theme));

    QString default_shortcuts =
            "Ctrl+Shift+L SHOW_PANE_LOCATION\n"
            "Ctrl+Shift+C SHOW_PANE_CELLULAR\n"
            "Ctrl+Shift+B SHOW_PANE_BATTERY\n"
            "Ctrl+Shift+U SHOW_PANE_BUGREPORT\n"
            "Ctrl+Shift+P SHOW_PANE_PHONE\n"
            "Ctrl+Shift+M SHOW_PANE_MICROPHONE\n"
            "Ctrl+Shift+V SHOW_PANE_VIRTSENSORS\n"
            "Ctrl+Shift+F SHOW_PANE_FINGER\n"
            "Ctrl+Shift+D SHOW_PANE_DPAD\n"
            "Ctrl+Shift+S SHOW_PANE_SETTINGS\n"
#ifdef __APPLE__
            "Ctrl+/     SHOW_PANE_HELP\n"
#else
            "F1         SHOW_PANE_HELP\n"
#endif
            "Ctrl+S     TAKE_SCREENSHOT\n"
            "Ctrl+Z     ENTER_ZOOM\n"
            "Ctrl+Up    ZOOM_IN\n"
            "Ctrl+Down  ZOOM_OUT\n"
            "Ctrl+Shift+Up    PAN_UP\n"
            "Ctrl+Shift+Down  PAN_DOWN\n"
            "Ctrl+Shift+Left  PAN_LEFT\n"
            "Ctrl+Shift+Right PAN_RIGHT\n"
            "Ctrl+=     VOLUME_UP\n"
            "Ctrl+-     VOLUME_DOWN\n"
            "Ctrl+P     POWER\n"
            "Ctrl+M     MENU\n"
            "Ctrl+T     TOGGLE_TRACKBALL\n"
#ifndef __APPLE__
            "Ctrl+H     HOME\n"
#else
            "Ctrl+Shift+H  HOME\n"
#endif
            "Ctrl+O     OVERVIEW\n"
            "Ctrl+Backspace BACK\n"
            "Ctrl+Left ROTATE_LEFT\n"
            "Ctrl+Right ROTATE_RIGHT\n";

    if (android::featurecontrol::isEnabled(android::featurecontrol::PlayStoreImage)) {
        default_shortcuts += "Ctrl+Shift+G SHOW_PANE_GPLAY\n";
    }

    QTextStream stream(&default_shortcuts);
    mShortcutKeyStore.populateFromTextStream(stream, parseQtUICommand);
    // Need to add this one separately because QKeySequence cannot parse
    // the string "Ctrl".
    mShortcutKeyStore.add(QKeySequence(Qt::Key_Control | Qt::ControlModifier),
                          QtUICommand::SHOW_MULTITOUCH);

    // Update tool tips on all push buttons.
    const QList<QPushButton*> childButtons =
            findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
    for (auto button : childButtons) {
        QVariant uiCommand = button->property("uiCommand");
        if (uiCommand.isValid()) {
            QtUICommand cmd;
            if (parseQtUICommand(uiCommand.toString(), &cmd)) {
                QVector<QKeySequence>* shortcuts =
                        mShortcutKeyStore.reverseLookup(cmd);
                if (shortcuts && shortcuts->length() > 0) {
                    button->setToolTip(getQtUICommandDescription(cmd) + " (" +
                                       shortcuts->at(0).toString(
                                               QKeySequence::NativeText) +
                                       ")");
                }
            }
        } else if (button != mToolsUi->close_button &&
                   button != mToolsUi->minimize_button &&
                   button != mToolsUi->more_button) {
            // Almost all toolbar buttons are required to have a uiCommand
            // property.
            // Unfortunately, we have no way of enforcing it at compile time.
            assert(0);
        }
    }

    // Make sure we create the extended window even if user didn't open it.
    auto self = QPointer<ToolWindow>(this);
    QObject::connect(
        &mExtendedWindowCreateTimer, &QTimer::timeout,
        [self] {
            if (self && !self->isExiting()) self->mExtendedWindow.get();
        });
    mExtendedWindowCreateTimer.setSingleShot(true);
    mExtendedWindowCreateTimer.start(10 * 1000);

    if (android_hw->hw_arc) {
        // Chrome OS doesn't support rotation now.
        mToolsUi->prev_layout_button->setHidden(true);
        mToolsUi->next_layout_button->setHidden(true);
    } else {
        // Android doesn't support tablet mode now.
        mToolsUi->tablet_mode_button->setHidden(true);
    }
#ifndef Q_OS_MAC
    // Swap minimize and close buttons on non-apple OSes
    auto closeBtn = mToolsUi->winButtonsLayout->takeAt(0);
    mToolsUi->winButtonsLayout->insertItem(1, closeBtn);
#endif
}

ToolWindow::~ToolWindow() {
    stopExtendedWindowCreation();
}

void ToolWindow::raise() {
    QFrame::raise();
    if (mVirtualSceneControlWindow.isVisible()) {
        mVirtualSceneControlWindow.raise();
    }
    if (mTopSwitched) {
        mExtendedWindow.get()->raise();
        mExtendedWindow.get()->activateWindow();
        mTopSwitched = false;
    }
}

void ToolWindow::switchClipboardSharing(bool enabled) {
    if (mUiEmuAgent && mUiEmuAgent->clipboard) {
        mUiEmuAgent->clipboard->setEnabled(enabled);
    }
}

void ToolWindow::showVirtualSceneControls(bool show) {
    if (show) {
        mVirtualSceneControlWindow.show();
    } else {
        mVirtualSceneControlWindow.hide();
    }
}

void ToolWindow::stopExtendedWindowCreation() {
    mExtendedWindowCreateTimer.stop();
    mExtendedWindowCreateTimer.disconnect();
}

void ToolWindow::hide() {
    QFrame::hide();
    mVirtualSceneControlWindow.hide();
    if (mExtendedWindow.hasInstance()) {
        mExtendedWindow.get()->hide();
    }
}

void ToolWindow::closeEvent(QCloseEvent* ce) {
    mIsExiting = true;
    // make sure only parent processes the event - otherwise some
    // siblings won't get it, e.g. main window
    ce->ignore();

    stopExtendedWindowCreation();
}

void ToolWindow::mousePressEvent(QMouseEvent* event) {
    raiseMainWindow();
    QFrame::mousePressEvent(event);
}

void ToolWindow::hideEvent(QHideEvent*) {
    mIsExtendedWindowVisibleOnShow =
            mExtendedWindow.hasInstance() && mExtendedWindow.get()->isVisible();
}

void ToolWindow::show() {
    QFrame::show();
    setFixedSize(size());

    if (mVirtualSceneControlWindow.isVisible()) {
        mVirtualSceneControlWindow.show();
    }

    if (mIsExtendedWindowVisibleOnShow) {
        mExtendedWindow.get()->show();
    }
}

void ToolWindow::handleUICommand(QtUICommand cmd, bool down) {
    switch (cmd) {
        case QtUICommand::SHOW_PANE_LOCATION:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_LOCATION);
            }
            break;
        case QtUICommand::SHOW_PANE_CELLULAR:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_CELLULAR);
            }
            break;
        case QtUICommand::SHOW_PANE_BATTERY:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_BATTERY);
            }
            break;
        case QtUICommand::SHOW_PANE_BUGREPORT:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_BUGREPORT);
            }
            break;
        case QtUICommand::SHOW_PANE_PHONE:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_TELEPHONE);
            }
            break;
        case QtUICommand::SHOW_PANE_MICROPHONE:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_MICROPHONE);
            }
            break;
        case QtUICommand::SHOW_PANE_VIRTSENSORS:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_VIRT_SENSORS);
            }
            break;
        case QtUICommand::SHOW_PANE_DPAD:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_DPAD);
            }
            break;
        case QtUICommand::SHOW_PANE_FINGER:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_FINGER);
            }
            break;
        case QtUICommand::SHOW_PANE_GPLAY:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_GOOGLE_PLAY);
            }
            break;
        case QtUICommand::SHOW_PANE_SETTINGS:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_SETTINGS);
            }
            break;
        case QtUICommand::SHOW_PANE_HELP:
            if (down) {
                showOrRaiseExtendedWindow(PANE_IDX_HELP);
            }
            break;
        case QtUICommand::TAKE_SCREENSHOT:
            if (down) {
                mEmulatorWindow->screenshot();
            }
            break;
        case QtUICommand::ENTER_ZOOM:
            if (down) {
                mEmulatorWindow->toggleZoomMode();
            }
            mToolsUi->zoom_button->setChecked(mEmulatorWindow->isInZoomMode());
            break;
        case QtUICommand::ZOOM_IN:
            if (down) {
                if (mEmulatorWindow->isInZoomMode()) {
                    mEmulatorWindow->zoomIn();
                } else {
                    mEmulatorWindow->scaleUp();
                }
            }
            break;
        case QtUICommand::ZOOM_OUT:
            if (down) {
                if (mEmulatorWindow->isInZoomMode()) {
                    mEmulatorWindow->zoomOut();
                } else {
                    mEmulatorWindow->scaleDown();
                }
            }
            break;
        case QtUICommand::PAN_UP:
            if (down) {
                mEmulatorWindow->panVertical(true);
            }
            break;
        case QtUICommand::PAN_DOWN:
            if (down) {
                mEmulatorWindow->panVertical(false);
            }
            break;
        case QtUICommand::PAN_LEFT:
            if (down) {
                mEmulatorWindow->panHorizontal(true);
            }
            break;
        case QtUICommand::PAN_RIGHT:
            if (down) {
                mEmulatorWindow->panHorizontal(false);
            }
            break;
        case QtUICommand::VOLUME_UP:
            forwardKeyToEmulator(LINUX_KEY_VOLUMEUP, down);
            break;
        case QtUICommand::VOLUME_DOWN:
            forwardKeyToEmulator(LINUX_KEY_VOLUMEDOWN, down);
            break;
        case QtUICommand::POWER:
            if (android_hw->hw_arc) {
                // Only send out request when user releases key.
                if (!down) {
                    qemu_system_powerdown_request();
                }
            } else {
                forwardKeyToEmulator(LINUX_KEY_POWER, down);
            }
            break;
        case QtUICommand::TABLET_MODE:
            if (android_hw->hw_arc) {
                forwardGenericEventToEmulator(EV_SW, SW_TABLET_MODE, down);
                forwardGenericEventToEmulator(EV_SYN, 0, 0);
            }
            break;
        case QtUICommand::MENU:
            forwardKeyToEmulator(LINUX_KEY_SOFT1, down);
            break;
        case QtUICommand::HOME:
            forwardKeyToEmulator(LINUX_KEY_HOME, down);
            break;
        case QtUICommand::BACK:
            forwardKeyToEmulator(LINUX_KEY_BACK, down);
            break;
        case QtUICommand::OVERVIEW:
            forwardKeyToEmulator(KEY_APPSWITCH, down);
            break;
        case QtUICommand::ROTATE_RIGHT:
        case QtUICommand::ROTATE_LEFT:
            if (down) {
                emulator_window_rotate_90(cmd == QtUICommand::ROTATE_RIGHT);
            }
            break;
        case QtUICommand::TOGGLE_TRACKBALL:
            if (down) {
                SkinEvent* skin_event = new SkinEvent();
                skin_event->type = kEventToggleTrackball;
                mEmulatorWindow->queueSkinEvent(skin_event);
            }
            break;
        case QtUICommand::SHOW_MULTITOUCH:
        // Multitouch is handled in EmulatorQtWindow, and doesn't
        // really need an element in the QtUICommand enum. This
        // enum element exists solely for the purpose of displaying
        // it in the list of keyboard shortcuts in the Help page.
        default:;
    }
}

void ToolWindow::forwardGenericEventToEmulator(int type,
                                               int code,
                                               int value) {
    SkinEvent* skin_event = new SkinEvent();
    skin_event->type = kEventGeneric;
    SkinEventGenericData& genericData = skin_event->u.generic_event;
    genericData.type = type;
    genericData.code = code;
    genericData.value = value;
    mEmulatorWindow->queueSkinEvent(skin_event);
}

void ToolWindow::forwardKeyToEmulator(uint32_t keycode, bool down) {
    SkinEvent* skin_event = new SkinEvent();
    skin_event->type = down ? kEventKeyDown : kEventKeyUp;
    skin_event->u.key.keycode = keycode;
    skin_event->u.key.mod = 0;
    mEmulatorWindow->queueSkinEvent(skin_event);
}

bool ToolWindow::handleQtKeyEvent(QKeyEvent* event) {
    // See if this key is handled by the virtual scene control window first.
    if (mVirtualSceneControlWindow.isVisible()) {
        if (mVirtualSceneControlWindow.handleQtKeyEvent(event)) {
            return true;
        }
    }

    // We don't care about the keypad modifier for anything, and it gets added
    // to the arrow keys of OSX by default, so remove it.
    QKeySequence event_key_sequence(event->key() +
                                    (event->modifiers() & ~Qt::KeypadModifier));
    bool down = event->type() == QEvent::KeyPress;
    bool h = mShortcutKeyStore.handle(event_key_sequence,
                                      [this, down](QtUICommand cmd) {
                                          if (down) {
                                              handleUICommand(cmd, true);
                                              handleUICommand(cmd, false);
                                          }
                                      });
    return h;
}

void ToolWindow::closeExtendedWindow() {
    // If user is clicking the 'x' button like crazy, we may get multiple
    // close events here, so make sure the function doesn't screw the state for
    // a next call.
    mExtendedWindow.clear();
}

void ToolWindow::dockMainWindow() {
    // Align horizontally relative to the main window's frame.
    // Align vertically to its contents.
    // If we're frameless, adjust for a transparent border
    // around the skin.
    move(parentWidget()->frameGeometry().right()
             + toolGap - mEmulatorWindow->getRightTransparency(),
         parentWidget()->geometry().top()
             + mEmulatorWindow->getTopTransparency());

    mVirtualSceneControlWindow.setWidth(
            parentWidget()->frameGeometry().width() -
            mEmulatorWindow->getLeftTransparency() -
            mEmulatorWindow->getRightTransparency());
    mVirtualSceneControlWindow.move(
            parentWidget()->frameGeometry().left() +
                    mEmulatorWindow->getLeftTransparency(),
            parentWidget()->geometry().bottom() -
                    mEmulatorWindow->getBottomTransparency() + toolGap);
}

void ToolWindow::raiseMainWindow() {
    mEmulatorWindow->raise();
    mEmulatorWindow->activateWindow();
}

void ToolWindow::updateTheme(const QString& styleSheet) {
    mVirtualSceneControlWindow.updateTheme(styleSheet);
    setStyleSheet(styleSheet);
}

void ToolWindow::setToolEmuAgent(const UiEmuAgent* agPtr) {
    mUiEmuAgent = agPtr;

    mVirtualSceneControlWindow.setAgent(agPtr);
    if (mExtendedWindow.hasInstance()) {
        mExtendedWindow.get()->setAgent(agPtr);
    }

    if (agPtr->clipboard) {
        connect(this, SIGNAL(guestClipboardChanged(QString)), this,
                SLOT(onGuestClipboardChanged(QString)), Qt::QueuedConnection);
        agPtr->clipboard->setGuestClipboardCallback(
                [](void* context, const uint8_t* data, size_t size) {
                    auto self = static_cast<ToolWindow*>(context);
                    emit self->guestClipboardChanged(
                            QString::fromUtf8((const char*)data, size));
                },
                this);
        connect(QApplication::clipboard(), SIGNAL(dataChanged()), this,
                SLOT(onHostClipboardChanged()));
    }

    emit haveClipboardSharingKnown(agPtr->clipboard != nullptr);
}

void ToolWindow::on_back_button_pressed() {
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::BACK, true);
}

void ToolWindow::on_back_button_released() {
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::BACK, false);
}

// If we need to ask about saving a snapshot,
// ask here, then set an avdParams flag to
// indicate the choice.
// If we don't need to ask, the avdParams flag
// should already be set.
// If the user cancels the pop-up, return
// 'false' to say we should NOT exit now.
bool ToolWindow::askWhetherToSaveSnapshot() {
    mExtendedWindow.get();
    // Check the UI setting
    const char* avdPath = path_getAvdContentPath(android_hw->avd_name);
    if (!avdPath) {
        // Can't find the setting! Assume it's not ASK: just return;
        return true;
    }
    QString avdSettingsFile = avdPath + QString(Ui::Settings::PER_AVD_SETTINGS_NAME);
    QSettings avdSpecificSettings(avdSettingsFile, QSettings::IniFormat);

    SaveSnapshotOnExit saveOnExitChoice =
        static_cast<SaveSnapshotOnExit>(
            avdSpecificSettings.value(
                Ui::Settings::SAVE_SNAPSHOT_ON_EXIT,
                static_cast<int>(SaveSnapshotOnExit::Always)).toInt());

    if (saveOnExitChoice != SaveSnapshotOnExit::Ask) {
        // The flag should already be set
        return true;
    }

    // The UI setting is ASK.
    // But don't ask if the command line was used. That overrides the UI.
    if (android_cmdLineOptions->no_snapshot_save) {
        return true;
    }

    QMessageBox msgBox(QMessageBox::Question,
                       tr("Save quick-boot state"),
                       tr("Do you want to save the current state for the next quick boot?"),
                       (QMessageBox::Yes | QMessageBox::No),
                       this);
    // Add a Cancel button to enable the MessageBox's X.
    QPushButton *cancelButton = msgBox.addButton(QMessageBox::Cancel);
    // Hide the Cancel button so X is the only way to cancel.
    cancelButton->setHidden(true);

    int selection = msgBox.exec();

    if (selection == QMessageBox::Cancel) {
        return false;
    }

    if (selection == QMessageBox::Yes) {
        android_avdParams->flags &= !AVDINFO_NO_SNAPSHOT_SAVE_ON_EXIT;
    } else {
        android_avdParams->flags |= AVDINFO_NO_SNAPSHOT_SAVE_ON_EXIT;
    }
    return true;
}

void ToolWindow::on_close_button_clicked() {
    bool actuallyExit = askWhetherToSaveSnapshot();
    if (actuallyExit) {
        parentWidget()->close();
    }
}

void ToolWindow::on_home_button_pressed() {
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::HOME, true);
}

void ToolWindow::on_home_button_released() {
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::HOME, false);
}

void ToolWindow::on_minimize_button_clicked() {
    // showMinimized() on OSX will put the toolbar in the minimized state,
    // which is undesired. We only want the main window to minimize, so
    // hide it instead.
    this->hide();
    mEmulatorWindow->showMinimized();
}

void ToolWindow::on_power_button_pressed() {
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::POWER, true);
}

void ToolWindow::on_power_button_released() {
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::POWER, false);
}

void ToolWindow::on_tablet_mode_button_toggled(bool checked) {
    handleUICommand(QtUICommand::TABLET_MODE, checked);
}

void ToolWindow::on_volume_up_button_pressed() {
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::VOLUME_UP, true);
}
void ToolWindow::on_volume_up_button_released() {
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::VOLUME_UP, false);
}
void ToolWindow::on_volume_down_button_pressed() {
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::VOLUME_DOWN, true);
}
void ToolWindow::on_volume_down_button_released() {
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::VOLUME_DOWN, false);
}

void ToolWindow::on_overview_button_pressed() {
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::OVERVIEW, true);
}

void ToolWindow::on_overview_button_released() {
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::OVERVIEW, false);
}

void ToolWindow::on_prev_layout_button_clicked() {
    handleUICommand(QtUICommand::ROTATE_LEFT);
}

void ToolWindow::on_next_layout_button_clicked() {
    handleUICommand(QtUICommand::ROTATE_RIGHT);
}

void ToolWindow::on_scrShot_button_clicked() {
    handleUICommand(QtUICommand::TAKE_SCREENSHOT, true);
}
void ToolWindow::on_zoom_button_clicked() {
    handleUICommand(QtUICommand::ENTER_ZOOM, true);
}

void ToolWindow::onGuestClipboardChanged(QString text) {
    QSignalBlocker blockSignals(QApplication::clipboard());
    QApplication::clipboard()->setText(text);
}

void ToolWindow::onHostClipboardChanged() {
    QByteArray bytes = QApplication::clipboard()->text().toUtf8();
    mUiEmuAgent->clipboard->setGuestClipboardContents(
                (const uint8_t*)bytes.data(), bytes.size());
}

void ToolWindow::showOrRaiseExtendedWindow(ExtendedWindowPane pane) {
    // Show the tabbed pane
    mExtendedWindow.get()->showPane(pane);
    mExtendedWindow.get()->raise();
    mExtendedWindow.get()->activateWindow();
}

void ToolWindow::on_more_button_clicked() {
    mExtendedWindow.get()->show();
    mExtendedWindow.get()->raise();
    mExtendedWindow.get()->activateWindow();
}

void ToolWindow::paintEvent(QPaintEvent*) {
    QPainter p;
    QPen pen(Qt::SolidLine);
    pen.setColor(Qt::black);
    pen.setWidth(1);
    p.begin(this);
    p.setPen(pen);

    double dpr = 1.0;
    int primary_screen_idx = qApp->desktop()->screenNumber(this);
    if (primary_screen_idx < 0) {
        primary_screen_idx = qApp->desktop()->primaryScreen();
    }
    const auto screens = QApplication::screens();
    if (primary_screen_idx >= 0 && primary_screen_idx < screens.size()) {
        const QScreen* const primary_screen = screens.at(primary_screen_idx);
        if (primary_screen) {
            dpr = primary_screen->devicePixelRatio();
        }
    }

    if (dpr > 1.0) {
        // Normally you'd draw the border with a (0, 0, w-1, h-1) rectangle.
        // However, there's some weirdness going on with high-density displays
        // that makes a single-pixel "slack" appear at the left and bottom
        // of the border. This basically adds 1 to compensate for it.
        p.drawRect(contentsRect());
    } else {
        p.drawRect(QRect(0, 0, width() - 1, height() - 1));
    }
    p.end();
}

void ToolWindow::notifySwitchOnTop() {
#ifdef _WIN32
    mTopSwitched = true;
#endif
}

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
#include "android/base/files/PathUtils.h"
#include "android/base/system/System.h"
#include "android/base/threads/Async.h"
#include "android/emulation/ConfigDirs.h"
#include "android/globals.h"
#include "android/main-common.h"
#include "android/skin/event.h"
#include "android/skin/keycode.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/error-dialog.h"
#include "android/skin/qt/extended-pages/common.h"
#include "android/skin/qt/extended-window.h"
#include "android/skin/qt/extended-window-styles.h"
#include "android/skin/qt/stylesheet.h"
#include "android/skin/qt/qt-settings.h"
#include "android/skin/qt/qt-ui-commands.h"
#include "android/skin/qt/tool-window.h"
#include "android/utils/debug.h"

#include <cassert>
#include <string>

using namespace android::base;

static ToolWindow *twInstance = NULL;

extern "C" void setUiEmuAgent(const UiEmuAgent *agentPtr) {
    if (twInstance) {
        twInstance->setToolEmuAgent(agentPtr);
    }
}

ToolWindow::ToolWindow(EmulatorQtWindow* window,
                       QWidget* parent,
                       ToolWindow::UIEventRecorderPtr event_recorder)
    : QFrame(parent),
      mEmulatorWindow(window),
      mExtendedWindow(nullptr),
      mUiEmuAgent(nullptr),
      mToolsUi(new Ui::ToolControls),
      mUIEventRecorder(event_recorder),
      mSizeTweaker(this),
      mAdbWarningBox(QMessageBox::Warning,
                     tr("Detected ADB"),
                     tr(""),
                     QMessageBox::Ok,
                     this) {
    twInstance = this;

    // "Tool" type windows live in another layer on top of everything in OSX, which is undesirable
    // because it means the extended window must be on top of the emulator window. However, on
    // Windows and Linux, "Tool" type windows are the only way to make a window that does not have
    // its own taskbar item.
#ifdef __APPLE__
    Qt::WindowFlags flag = Qt::Dialog;
#else
    Qt::WindowFlags flag = Qt::Tool;
#endif
    setWindowFlags(flag | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    mToolsUi->setupUi(this);

    // Get the latest user selections from the user-config code.
    QSettings settings;
    SettingsTheme theme = (SettingsTheme)settings.
                            value(Ui::Settings::UI_THEME, 0).toInt();
    if (theme < 0 || theme >= SETTINGS_THEME_NUM_ENTRIES) {
        theme = (SettingsTheme)0;
        settings.setValue(Ui::Settings::UI_THEME, 0);
    }

    adjustAllButtonsForTheme(theme);
    this->setStyleSheet(Ui::stylesheetForTheme(theme));

    QString default_shortcuts =
        "Ctrl+Shift+L SHOW_PANE_LOCATION\n"
        "Ctrl+Shift+C SHOW_PANE_CELLULAR\n"
        "Ctrl+Shift+B SHOW_PANE_BATTERY\n"
        "Ctrl+Shift+P SHOW_PANE_PHONE\n"
// TODO(grigoryj): re-enable this when the virtual sensors UI is complete.
#if 0
        "Ctrl+Shift+V SHOW_PANE_VIRTSENSORS\n"
#endif
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
#ifndef __APPLE__
        "Ctrl+H     HOME\n"
#else
        "Ctrl+Shift+H  HOME\n"
#endif
        "Ctrl+O     OVERVIEW\n"
        "Ctrl+Backspace BACK\n"
        "Ctrl+Left ROTATE_LEFT\n"
        "Ctrl+Right ROTATE_RIGHT\n";

    QTextStream stream(&default_shortcuts);
    mShortcutKeyStore.populateFromTextStream(stream, parseQtUICommand);
    // Need to add this one separately because QKeySequence cannot parse
    // the string "Ctrl".
    mShortcutKeyStore.add(QKeySequence(Qt::Key_Control | Qt::ControlModifier),
                          QtUICommand::SHOW_MULTITOUCH);

    // Update tool tips on all push buttons.
    const QList<QPushButton*> childButtons =
            findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
    for(auto button : childButtons) {
        QVariant uiCommand = button->property("uiCommand");
        if (uiCommand.isValid()) {
            QtUICommand cmd;
            if (parseQtUICommand(uiCommand.toString(), &cmd)) {
                QVector<QKeySequence>* shortcuts = mShortcutKeyStore.reverseLookup(cmd);
                if (shortcuts && shortcuts->length() > 0) {
                    button->setToolTip(
                        getQtUICommandDescription(cmd) +
                        " (" + shortcuts->at(0).toString(QKeySequence::NativeText) + ")");
                }
            }
        } else if (button != mToolsUi->close_button &&
                   button != mToolsUi->minimize_button &&
                   button != mToolsUi->more_button) {
            // Almost all toolbar buttons are required to have a uiCommand property.
            // Unfortunately, we have no way of enforcing it at compile time.
            assert(0);
        }
    }

#ifndef Q_OS_MAC
    // Swap minimize and close buttons on non-apple OSes
    int tmp_x = mToolsUi->close_button->x();
    mToolsUi->close_button->move(mToolsUi->minimize_button->x(),
                                 mToolsUi->close_button->y());
    mToolsUi->minimize_button->move(tmp_x, mToolsUi->minimize_button->y());
#endif

    QObject::connect(this, SIGNAL(createExtendedWindow()), this, SLOT(slot_createExtendedWindow()));
}

ToolWindow::~ToolWindow() {
}

void ToolWindow::checkAdbVersionAndWarn() {
    QSettings settings;
    if (!mAdbInterface.isAdbVersionCurrent() &&
        settings.value(Ui::Settings::AUTO_FIND_ADB, true).toBool()) {
        mAdbWarningBox.setText(tr("The ADB binary found at ") +
                               mDetectedAdbPath +
                               tr(" is obsolete and has serious performance "
                                  "problems with the Android Emulator. "
                                  "Please update to a newer version to get "
                                  "significantly faster app/file transfer."));
        showAdbWarning();
    }
}

void ToolWindow::showAdbWarning() {
    QSettings settings;
    if (settings.value(Ui::Settings::SHOW_ADB_WARNING, true).toBool()) {
        QObject::connect(&mAdbWarningBox,
                         SIGNAL(buttonClicked(QAbstractButton*)), this,
                         SLOT(slot_adbWarningMessageAccepted()));

        QCheckBox* checkbox = new QCheckBox(tr("Never show this again."));
        checkbox->setCheckState(Qt::Unchecked);
        mAdbWarningBox.setWindowModality(Qt::NonModal);
        mAdbWarningBox.setCheckBox(checkbox);
        mAdbWarningBox.show();
    }
}

void ToolWindow::hide()
{
    QFrame::hide();
    if (mExtendedWindow) {
        mExtendedWindow->hide();
    }
}

void ToolWindow::closeEvent(QCloseEvent* ce) {
    // make sure only parent processes the event - otherwise some
    // siblings won't get it, e.g. main window
    ce->ignore();
}

void ToolWindow::mousePressEvent(QMouseEvent *event)
{
    raiseMainWindow();
    QFrame::mousePressEvent(event);
}

void ToolWindow::hideEvent(QHideEvent*) {
    if (mExtendedWindow) {
        mIsExtendedWindowVisibleOnShow = mExtendedWindow->isVisible();
    }
}

void ToolWindow::show()
{
    QFrame::show();
    setFixedSize(size());

    if (mExtendedWindow && mIsExtendedWindowVisibleOnShow) {
        mExtendedWindow->show();
    }
}

QString ToolWindow::getAdbFullPath(QStringList* args) {
    QString adbPath = QString::null;
    QSettings settings;

    if (settings.value(Ui::Settings::AUTO_FIND_ADB, true).toBool()) {
        if (!mAdbInterface.detectedAdbPath().empty()) {
            adbPath = QString::fromStdString(mAdbInterface.detectedAdbPath());
        } else {
            showErrorDialog(tr("Could not automatically find ADB.<br>"
                               "Please use the settings page to manually set "
                               "an ADB path."),
                            tr("ADB"));
        }
    } else {
        adbPath = settings.value(Ui::Settings::ADB_PATH, "").toString();
    }

    if (adbPath.isNull()) {
        return QString::null;
    }

    // Enqueue arguments for adb to ensure it finds the right emulator.
    *args << "-s";
    *args << "emulator-" + QString::number(android_base_port);
    return adbPath;
}

void ToolWindow::runAdbShellStopAndQuit()
{
    // we need to run it only once, so don't ever reset this
    if (mStartedAdbStopProcess) {
        return;
    }

    if (async([this] { this->adbShellStopRunner(); })) {
        mStartedAdbStopProcess = true;
    } else {
        mEmulatorWindow->queueQuitEvent();
    }
}

void ToolWindow::adbShellStopRunner() {
    QStringList args;
    const auto command = getAdbFullPath(&args);
    if (command.isNull()) {
        mEmulatorWindow->queueQuitEvent();
        return;
    }

    // convert the command + arguments to the format needed in System class call
    std::vector<std::string> fullArgs;
    fullArgs.push_back(command.toUtf8().constData());
    for (const auto& arg : args) {
        fullArgs.push_back(arg.toUtf8().constData());
    }
    fullArgs.push_back("shell");
    fullArgs.push_back("stop");

    System::get()->runCommand(fullArgs, RunOptions::WaitForCompletion |
                                                RunOptions::HideAllOutput);

    mEmulatorWindow->queueQuitEvent();
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
    case QtUICommand::SHOW_PANE_PHONE:
        if (down) {
            showOrRaiseExtendedWindow(PANE_IDX_TELEPHONE);
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
        forwardKeyToEmulator(KEY_VOLUMEUP, down);
        break;
    case QtUICommand::VOLUME_DOWN:
        forwardKeyToEmulator(KEY_VOLUMEDOWN, down);
        break;
    case QtUICommand::POWER:
        forwardKeyToEmulator(KEY_POWER, down);
        break;
    case QtUICommand::MENU:
        forwardKeyToEmulator(KEY_SOFT1, down);
        break;
    case QtUICommand::HOME:
        forwardKeyToEmulator(KEY_HOME, down);
        break;
    case QtUICommand::BACK:
        forwardKeyToEmulator(KEY_BACK, down);
        break;
    case QtUICommand::OVERVIEW:
        forwardKeyToEmulator(KEY_APPSWITCH, down);
        break;
    case QtUICommand::ROTATE_RIGHT:
    case QtUICommand::ROTATE_LEFT:
        if (down) {
            // TODO: remove this after we preserve zoom after rotate
            if (mEmulatorWindow->isInZoomMode()) {
                mToolsUi->zoom_button->click();
            }

            // Rotating the emulator preserves size, but this can be a problem
            // if, for example, a very-wide emulator in landscape is rotated to
            // portrait. To avoid this situation (which makes the scroll bars
            // appear), force a resize to the new size.
            QSize containerSize = mEmulatorWindow->containerSize();
            mEmulatorWindow->doResize(
                    QSize(containerSize.height(), containerSize.width()), true,
                    true);

            SkinEvent* skin_event = new SkinEvent();
            skin_event->type =
                cmd == QtUICommand::ROTATE_RIGHT ?
                    kEventLayoutNext :
                    kEventLayoutPrev;

            // TODO(grigoryj): debug output needed for investigating the rotation bug.
            if (VERBOSE_CHECK(rotation)) {
                qWarning("Queuing skin event for %s",
                         cmd == QtUICommand::ROTATE_RIGHT ? "ROTATE_RIGHT" : "ROTATE_LEFT");
            }
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

void ToolWindow::forwardKeyToEmulator(uint32_t keycode, bool down) {
    SkinEvent* skin_event = new SkinEvent();
    skin_event->type = down ? kEventKeyDown : kEventKeyUp;
    skin_event->u.key.keycode = keycode;
    skin_event->u.key.mod = 0;
    mEmulatorWindow->queueSkinEvent(skin_event);
}

bool ToolWindow::handleQtKeyEvent(QKeyEvent* event) {
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

void ToolWindow::dockMainWindow()
{
#ifdef __linux__
    // On Linux, the gap between the main window and
    // the tool bar is 8 pixels bigger than is expected.
    // Kludge a correction.
    const static int gapAdjust = -8;
#else
    // Windows and OSX are OK
    const static int gapAdjust =  0;
#endif

    // Align horizontally relative to the main window's frame.
    // Align vertically to its contents.

    move(parentWidget()->frameGeometry().right() + toolGap + gapAdjust,
         parentWidget()->geometry().top() );
}

void ToolWindow::raiseMainWindow()
{
    mEmulatorWindow->raise();
    mEmulatorWindow->activateWindow();
}

void ToolWindow::setToolEmuAgent(const UiEmuAgent* agPtr) {
    mUiEmuAgent = agPtr;

    // The extended window requires the UiEmuAgent to be created and
    // initialized properly, but that needs to happen on the UI thread, so
    // we make it happen via a signal.
    this->createExtendedWindow();
}

void ToolWindow::on_back_button_pressed()
{
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::BACK, true);
}

void ToolWindow::on_back_button_released()
{
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::BACK, false);
}

void ToolWindow::on_close_button_clicked()
{
    if (mExtendedWindow) {
        mExtendedWindow->close();
        delete mExtendedWindow;
    }
    parentWidget()->close();
}

void ToolWindow::on_home_button_pressed()
{
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::HOME, true);
}

void ToolWindow::on_home_button_released()
{
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::HOME, false);
}

void ToolWindow::on_minimize_button_clicked()
{
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

void ToolWindow::on_volume_up_button_pressed()
{
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::VOLUME_UP, true);
}
void ToolWindow::on_volume_up_button_released()
{
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::VOLUME_UP, false);
}
void ToolWindow::on_volume_down_button_pressed()
{
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::VOLUME_DOWN, true);
}
void ToolWindow::on_volume_down_button_released()
{
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::VOLUME_DOWN, false);
}

void ToolWindow::on_overview_button_pressed()
{
    mEmulatorWindow->raise();
    handleUICommand(QtUICommand::OVERVIEW, true);
}

void ToolWindow::on_overview_button_released()
{
    mEmulatorWindow->activateWindow();
    handleUICommand(QtUICommand::OVERVIEW, false);
}

void ToolWindow::on_prev_layout_button_clicked()
{
    handleUICommand(QtUICommand::ROTATE_LEFT);
}

void ToolWindow::on_next_layout_button_clicked()
{
    handleUICommand(QtUICommand::ROTATE_RIGHT);
}

void ToolWindow::on_scrShot_button_clicked()
{
    handleUICommand(QtUICommand::TAKE_SCREENSHOT, true);
}
void ToolWindow::on_zoom_button_clicked()
{
    handleUICommand(QtUICommand::ENTER_ZOOM, true);
}

void ToolWindow::showOrRaiseExtendedWindow(ExtendedWindowPane pane) {
    // Show the tabbed pane
    if (mExtendedWindow) {
        mExtendedWindow->showPane(pane);
        mExtendedWindow->raise();
        mExtendedWindow->activateWindow();
        return;
    }
}

void ToolWindow::on_more_button_clicked()
{
    if (mExtendedWindow) {
        mExtendedWindow->show();
        mExtendedWindow->raise();
        mExtendedWindow->activateWindow();
    }
}

void ToolWindow::slot_adbWarningMessageAccepted() {
    QCheckBox* checkbox = mAdbWarningBox.checkBox();
    if (checkbox->checkState() == Qt::Checked) {
        QSettings settings;
        settings.setValue(Ui::Settings::SHOW_ADB_WARNING, false);
    }
}

void ToolWindow::slot_createExtendedWindow() {
    mExtendedWindow = new ExtendedWindow(mEmulatorWindow, this, mUiEmuAgent,
                                         &mShortcutKeyStore);
    if (auto recorder_ptr = mUIEventRecorder.lock()) {
        recorder_ptr->startRecording(mExtendedWindow);
    }

    // The extended window is created before the "..." button is pressed, so it
    // should be hid until that button is actually pressed.
    mExtendedWindow->hide();
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
    QScreen* primary_screen = QApplication::screens().at(primary_screen_idx);
    if (primary_screen) {
        dpr = primary_screen->devicePixelRatio();
    }
    if (dpr > 1.0) {
        // Normally you'd draw the border with a (0, 0 - w-1, h-1) rectangle.
        // However, there's some weirdness going on with high-density displays
        // that makes a single-pixel "slack" appear at the left and bottom
        // of the border. This basically adds 1 to compensate for it.
        p.drawRect(contentsRect());
    } else {
        p.drawRect(QRect(0, 0, width() - 1, height() - 1));
    }
    p.end();
}

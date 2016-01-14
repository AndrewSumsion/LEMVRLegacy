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
#include "android/avd/util.h"
#include "android/base/files/PathUtils.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/base/system/System.h"
#include "android/base/threads/Async.h"
#include "android/globals.h"
#include "android/main-common.h"
#include "android/skin/event.h"
#include "android/skin/keycode.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/error-dialog.h"
#include "android/skin/qt/extended-pages/common.h"
#include "android/skin/qt/extended-window.h"
#include "android/skin/qt/extended-window-styles.h"
#include "android/skin/qt/qt-settings.h"
#include "android/skin/qt/qt-ui-commands.h"
#include "android/skin/qt/tool-window.h"
#include "ui_tools.h"

#include <cassert>

using namespace android::base;

static ToolWindow *twInstance = NULL;

extern "C" void setUiEmuAgent(const UiEmuAgent *agentPtr) {
    if (twInstance) {
        twInstance->setToolEmuAgent(agentPtr);
    }
}

ToolWindow::ToolWindow(EmulatorQtWindow* window, QWidget* parent)
    : QFrame(parent),
      emulator_window(window),
      extendedWindow(NULL),
      uiEmuAgent(NULL),
      toolsUi(new Ui::ToolControls),
      mPushDialog(this),
      mInstallDialog(this) {
    Q_INIT_RESOURCE(resources);
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
    setWindowFlags(flag | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint | Qt::Drawer);
    toolsUi->setupUi(this);

    // Initialize some values in the QCoreApplication so we can easily
    // and consistently access QSettings to save and restore user settings
    QCoreApplication::setOrganizationName(Ui::Settings::ORG_NAME);
    QCoreApplication::setOrganizationDomain(Ui::Settings::ORG_DOMAIN);
    QCoreApplication::setApplicationName(Ui::Settings::APP_NAME);

    // TODO: make this affected by themes and changes
    mInstallDialog.setWindowTitle(tr("APK Installer"));
    mInstallDialog.setLabelText(tr("Installing APK..."));
    mInstallDialog.setRange(0, 0); // Makes it a "busy" dialog
    mInstallDialog.close();
    QObject::connect(&mInstallDialog, SIGNAL(canceled()), this, SLOT(slot_installCanceled()));
    QObject::connect(&mInstallProcess, SIGNAL(finished(int)), this, SLOT(slot_installFinished(int)));

    mPushDialog.setWindowTitle(tr("File Copy"));
    mPushDialog.setLabelText(tr("Copying files..."));
    mPushDialog.setRange(0, 0);
    mPushDialog.close();
    QObject::connect(&mPushDialog, SIGNAL(canceled()), this, SLOT(slot_pushCanceled()));
    QObject::connect(&mPushProcess, SIGNAL(finished(int)), this, SLOT(slot_pushFinished(int)));

    // Get the latest user selections from the
    // user-config code.
    QSettings settings;
    QString sdkPath = settings.value(Ui::Settings::SDK_PATH, "").toString();
    if ( sdkPath.isEmpty() ) {
        // Initialize the path
        sdkPath = findAndroidSdkRoot();
        // Whatever it is, save it
        settings.setValue(Ui::Settings::SDK_PATH, sdkPath);
    }

    SettingsTheme theme = (SettingsTheme)settings.
                            value(Ui::Settings::UI_THEME, 0).toInt();
    if (theme < 0 || theme >= SETTINGS_THEME_NUM_ENTRIES) {
        theme = (SettingsTheme)0;
        settings.setValue(Ui::Settings::UI_THEME, 0);
    }

    switchAllIconsForTheme(theme);

    if (theme == SETTINGS_THEME_DARK) {
        this->setStyleSheet(QT_STYLE(DARK));
    } else {
        this->setStyleSheet(QT_STYLE(LIGHT));
    }

    QString default_shortcuts =
        "Ctrl+Alt+L SHOW_PANE_LOCATION\n"
        "Ctrl+Alt+C SHOW_PANE_CELLULAR\n"
        "Ctrl+Alt+B SHOW_PANE_BATTERY\n"
        "Ctrl+Alt+P SHOW_PANE_PHONE\n"
        "Ctrl+Alt+V SHOW_PANE_VIRTSENSORS\n"
        "Ctrl+Alt+D SHOW_PANE_DPAD\n"
        "Ctrl+Alt+S SHOW_PANE_SETTINGS\n"
#ifdef __APPLE__
        "Ctrl+/     SHOW_PANE_HELP\n"
#else
        "F1         SHOW_PANE_HELP\n"
#endif
        "Ctrl+S     TAKE_SCREENSHOT\n"
        "Ctrl+Z     ENTER_ZOOM\n"
#ifdef __APPLE__
        "Ctrl+Num+Up        ZOOM_IN\n"
        "Ctrl+Num+Down      ZOOM_OUT\n"
        "Ctrl+Shift+Num+Up    PAN_UP\n"
        "Ctrl+Shift+Num+Down  PAN_DOWN\n"
        "Ctrl+Shift+Num+Left  PAN_LEFT\n"
        "Ctrl+Shift+Num+Right PAN_RIGHT\n"
#else
        "Ctrl+Up    ZOOM_IN\n"
        "Ctrl+Down  ZOOM_OUT\n"
        "Ctrl+Shift+Up    PAN_UP\n"
        "Ctrl+Shift+Down  PAN_DOWN\n"
        "Ctrl+Shift+Left  PAN_LEFT\n"
        "Ctrl+Shift+Right PAN_RIGHT\n"
#endif
        "Ctrl+G     GRAB_KEYBOARD\n"
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
#ifdef __APPLE__
        "Ctrl+Num+Left ROTATE_LEFT\n"
        "Ctrl+Num+Right ROTATE_RIGHT\n";
#else
        "Ctrl+Left ROTATE_LEFT\n"
        "Ctrl+Right ROTATE_RIGHT\n";
#endif

    QTextStream stream(&default_shortcuts);
    mShortcutKeyStore.populateFromTextStream(stream, parseQtUICommand);
    // Need to add this one separately because QKeySequence cannot parse
    // the string "Ctrl+Alt".
    mShortcutKeyStore.add(
            QKeySequence(Qt::Key_Alt | Qt::AltModifier | Qt::ControlModifier),
            QtUICommand::UNGRAB_KEYBOARD);

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
        } else if (
            button != toolsUi->close_button &&
            button != toolsUi->minimize_button &&
            button != toolsUi->more_button)
                {
            // Almost all toolbar buttons are required to have a uiCommand property.
            // Unfortunately, we have no way of enforcing it at compile time.
            assert(0);
        }
    }
}

void ToolWindow::hide()
{
    QFrame::hide();
    if (extendedWindow) {
        extendedWindow->hide();
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
    mIsExtendedWindowActiveOnHide =
            extendedWindow != nullptr
            && QApplication::activeWindow() == extendedWindow;
}

void ToolWindow::show()
{
    dockMainWindow();
    setFixedSize(size());
    QFrame::show();

    if (extendedWindow) {
        extendedWindow->show();

        if (mIsExtendedWindowActiveOnHide) {
            extendedWindow->raise();
            extendedWindow->activateWindow();
        }
    }
}

QString ToolWindow::findAndroidSdkRoot()
{
    char isFromEnv = 0;
    const ScopedCPtr<const char> sdkRoot(path_getSdkRoot(&isFromEnv));
    if (!sdkRoot) {
        showErrorDialog(tr("The ANDROID_SDK_ROOT environment variable must be "
                           "set to use this."),
                        tr("Android SDK Root"));
        return QString::null;
    }
    return QString::fromUtf8(sdkRoot.get());
}

QString ToolWindow::getAdbFullPath(QStringList *args)
{
    // Find adb first
    QSettings settings;
    QString sdkRoot = settings.value(Ui::Settings::SDK_PATH, "").toString();
    if (sdkRoot.isNull()) {
        return QString::null;
    }

    StringVector adbVector;
    adbVector.push_back(String(sdkRoot.toStdString().data()));
    adbVector.push_back(String("platform-tools"));
    adbVector.push_back(String("adb"));
    String adbPath = PathUtils::recompose(adbVector);

    // TODO: is this safe cross-platform?
    *args << "-s";
    *args << "emulator-" + QString::number(android_base_port);
    return adbPath.c_str();
}

QString ToolWindow::getScreenshotSaveDirectory()
{
    QSettings settings;
    QString savePath = settings.value(Ui::Settings::SAVE_PATH, "").toString();

    // Check if this path is writable
    QFileInfo fInfo(savePath);
    if ( !fInfo.isDir() || !fInfo.isWritable() ) {

        // Clear this, so we'll try the default instead
        savePath = "";
    }

    if (savePath.isEmpty()) {

        // We have no path. Try to determine the path to the desktop.
        QStringList paths =
                QStandardPaths::standardLocations(
                    QStandardPaths::DesktopLocation);
        if (paths.size() > 0) {
            savePath = QDir::toNativeSeparators(paths[0]);

            // Save this for future reference
            settings.setValue(Ui::Settings::SAVE_PATH, savePath);
        }
    }

    return savePath;
}

QString ToolWindow::getScreenshotSaveFile()
{
    QDateTime currentTime = QDateTime::currentDateTime();
    QString fileName = "Screenshot_" + currentTime.toString("yyyyMMdd-HHmmss") + ".png";
    QString dirName = getScreenshotSaveDirectory();

    // An empty directory means the designated save location is not valid.
    if (dirName.isEmpty()) {
        return dirName;
    }

    return QDir::toNativeSeparators(QDir(dirName).filePath(fileName));
}

void ToolWindow::runAdbInstall(const QString &path)
{
    if (mInstallProcess.state() != QProcess::NotRunning) {
        showErrorDialog(tr("Another APK install is currently pending.<br/>"
                           "Try again after current APK installation completes."),
                        tr("APK Installer"));
        return;
    }

    // Default the -r flag to replace the current version
    // TODO: is replace the desired default behavior?
    // TODO: enable other flags? -lrstdg available
    QStringList args;
    QString command = getAdbFullPath(&args);
    if (command.isNull()) {
        return;
    }

    args << "install";  // The desired command
    args << "-r";       // The flags for adb install
    args << path;       // The path to the APK to install

    // Show a dialog so the user knows something is happening
    mInstallDialog.show();

    // Keep track of this process
    mInstallProcess.start(command, args);
    mInstallProcess.waitForStarted();
}


void ToolWindow::runAdbShellStopAndQuit()
{
    // we need to run it only once, so don't ever reset this
    if (mStartedAdbStopProcess) {
        return;
    }

    if (async([this] { return this->adbShellStopRunner(); })) {
        mStartedAdbStopProcess = true;
    } else {
        emulator_window->queueQuitEvent();
    }
}

int ToolWindow::adbShellStopRunner() {
    QStringList args;
    const auto command = getAdbFullPath(&args);
    if (command.isNull()) {
        emulator_window->queueQuitEvent();
        return 1;
    }

    // convert the command + arguments to the format needed in System class call
    StringVector fullArgs;
    fullArgs.push_back(command.toUtf8().constData());
    for (const auto& arg : args) {
        fullArgs.push_back(arg.toUtf8().constData());
    }
    fullArgs.push_back("shell");
    fullArgs.push_back("stop");

    System::get()->runCommand(fullArgs, RunOptions::WaitForCompletion |
                                                RunOptions::HideAllOutput);

    emulator_window->queueQuitEvent();
    return 0;
}

void ToolWindow::runAdbPush(const QList<QUrl> &urls)
{
    // Queue up the next set of files
    for (int i = 0; i < urls.length(); i++) {
        mFilesToPush.enqueue(urls[i]);
    }
    mPushDialog.setMaximum(mPushDialog.maximum() + urls.size());

    if (mPushProcess.state() == QProcess::NotRunning) {

        // Show a dialog so the user knows something is happening
        mPushDialog.show();

        // Begin the cascading push
        slot_pushFinished(0);
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
    case QtUICommand::SHOW_PANE_SETTINGS:
        if (down) {
            showOrRaiseExtendedWindow(PANE_IDX_SETTINGS);
        }
        break;
    case QtUICommand::SHOW_PANE_HELP:
        if (down) {
            showOrRaiseExtendedWindow(PANE_IDX_HELP);
        }
    case QtUICommand::TAKE_SCREENSHOT:
        if (down) {
            emulator_window->screenshot();
        }
        break;
    case QtUICommand::ENTER_ZOOM:
        if (down) {
            emulator_window->toggleZoomMode();
            toolsUi->zoom_button->setDown(emulator_window->isInZoomMode());
        }
        break;
    case QtUICommand::ZOOM_IN:
        if (down) {
            if (emulator_window->isInZoomMode()) {
                emulator_window->zoomIn();
            } else {
                emulator_window->scaleUp();
            }
        }
        break;
    case QtUICommand::ZOOM_OUT:
        if (down) {
            if (emulator_window->isInZoomMode()) {
                emulator_window->zoomOut();
            } else {
                emulator_window->scaleDown();
            }
        }
        break;
    case QtUICommand::PAN_UP:
        if (down) {
            emulator_window->panVertical(true);
        }
        break;
    case QtUICommand::PAN_DOWN:
        if (down) {
            emulator_window->panVertical(false);
        }
        break;
    case QtUICommand::PAN_LEFT:
        if (down) {
            emulator_window->panHorizontal(true);
        }
        break;
    case QtUICommand::PAN_RIGHT:
        if (down) {
            emulator_window->panHorizontal(false);
        }
        break;
    case QtUICommand::GRAB_KEYBOARD:
        if (down) {
            emulator_window->setGrabKeyboardInput(true);
        }
        break;
    case QtUICommand::VOLUME_UP:
        uiAgentAction([down](const UiEmuAgent& agent) {
            agent.userEvents->sendKey(kKeyCodeVolumeUp, down);
        });
        break;
    case QtUICommand::VOLUME_DOWN:
        uiAgentAction([down](const UiEmuAgent& agent) {
            agent.userEvents->sendKey(kKeyCodeVolumeDown, down);
        });
        break;
    case QtUICommand::POWER:
        uiAgentAction([down](const UiEmuAgent& agent) {
            agent.userEvents->sendKey(kKeyCodePower, down);
        });
        break;
    case QtUICommand::MENU:
        uiAgentAction([down](const UiEmuAgent& agent) {
            agent.userEvents->sendKey(kKeyCodeMenu, down);
        });
        break;
    case QtUICommand::HOME:
        uiAgentAction([down](const UiEmuAgent& agent) {
            agent.userEvents->sendKey(kKeyCodeHome, down);
        });
        break;
    case QtUICommand::BACK:
        uiAgentAction([down](const UiEmuAgent& agent) {
            agent.userEvents->sendKey(kKeyCodeBack, down);
        });
        break;
    case QtUICommand::OVERVIEW:
        uiAgentAction([down](const UiEmuAgent& agent) {
            agent.userEvents->sendKey(kKeyCodeAppSwitch, down);
        });
        break;
    case QtUICommand::ROTATE_RIGHT:
    case QtUICommand::ROTATE_LEFT:
        if (down) {
            // TODO: remove this after we preserve zoom after rotate
            if (emulator_window->isInZoomMode()) {
                toolsUi->zoom_button->click();
            }
            SkinEvent* skin_event = new SkinEvent();
            skin_event->type =
                cmd == QtUICommand::ROTATE_RIGHT ?
                    kEventLayoutNext :
                    kEventLayoutPrev;
            skinUIEvent(skin_event);
        }
        break;
    case QtUICommand::UNGRAB_KEYBOARD:
        // Ungrabbing is handled in EmulatorQtWindow, and doesn't
        // really need an element in the QtUICommand enum. This
        // enum element exists solely for the purpose of displaying
        // it in the list of keyboard shortcuts in the Help page.
    default:;
    }
}

template <class Action>
void ToolWindow::uiAgentAction(Action a) const {
    if (uiEmuAgent) {
        a(*uiEmuAgent);
    }
}

bool ToolWindow::handleQtKeyEvent(QKeyEvent* event) {
    QKeySequence event_key_sequence(event->key() + event->modifiers());
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
    move(parentWidget()->geometry().right() + 10, parentWidget()->geometry().top());
}

void ToolWindow::raiseMainWindow()
{
    emulator_window->raise();
    emulator_window->activateWindow();
}

void ToolWindow::on_back_button_pressed()
{
    emulator_window->raise();
    handleUICommand(QtUICommand::BACK, true);
}

void ToolWindow::on_back_button_released()
{
    emulator_window->activateWindow();
    handleUICommand(QtUICommand::BACK, false);
}

void ToolWindow::on_close_button_clicked()
{
    parentWidget()->close();
}

void ToolWindow::on_home_button_pressed()
{
    emulator_window->raise();
    handleUICommand(QtUICommand::HOME, true);
}

void ToolWindow::on_home_button_released()
{
   emulator_window->activateWindow();
   handleUICommand(QtUICommand::HOME, false);
}

void ToolWindow::on_minimize_button_clicked()
{
    if (extendedWindow) {
        extendedWindow->hide();
    }
    this->hide();
    emulator_window->showMinimized();
}

void ToolWindow::on_power_button_pressed() {
    emulator_window->raise();
    handleUICommand(QtUICommand::POWER, true);
}

void ToolWindow::on_power_button_released() {
    emulator_window->activateWindow();
    handleUICommand(QtUICommand::POWER, false);
}

void ToolWindow::on_volume_up_button_pressed()
{
    emulator_window->raise();
    handleUICommand(QtUICommand::VOLUME_UP, true);
}
void ToolWindow::on_volume_up_button_released()
{
    emulator_window->activateWindow();
    handleUICommand(QtUICommand::VOLUME_UP, false);
}
void ToolWindow::on_volume_down_button_pressed()
{
    emulator_window->raise();
    handleUICommand(QtUICommand::VOLUME_DOWN, true);
}
void ToolWindow::on_volume_down_button_released()
{
    emulator_window->activateWindow();
    handleUICommand(QtUICommand::VOLUME_DOWN, false);
}

void ToolWindow::on_overview_button_pressed()
{
    emulator_window->raise();
    handleUICommand(QtUICommand::OVERVIEW, true);
}

void ToolWindow::on_overview_button_released()
{
    emulator_window->activateWindow();
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
    if (extendedWindow) {
        // It already exists. Don't create another.
        // (But raise it in case it's hidden.)
        extendedWindow->raise();
        extendedWindow->showPane(pane);
        return;
    }

    extendedWindow = new ExtendedWindow(emulator_window, this, uiEmuAgent,
                                        &mShortcutKeyStore);
    extendedWindow->show();
    extendedWindow->showPane(pane);
    extendedWindow->raise();
}

void ToolWindow::on_more_button_clicked()
{
    showOrRaiseExtendedWindow(PANE_IDX_LOCATION);
    extendedWindow->activateWindow();
}

void ToolWindow::slot_installCanceled()
{
    if (mInstallProcess.state() != QProcess::NotRunning) {
        mInstallProcess.kill();
    }
}

void ToolWindow::slot_installFinished(int exitStatus)
{
    mInstallDialog.close();

    if (exitStatus) {
        showErrorDialog(tr("The APK failed to install: adb could not connect to the emulator."),
                        tr("APK Installer"));
        return;
    }

    // "adb install" does not return a helpful exit status, so instead we parse the standard
    // output of the process looking for "Failure \[(.*)\]"

    QString output = QString(mInstallProcess.readAllStandardOutput());
    QRegularExpression regex("Failure \\[(.*)\\]");
    QRegularExpressionMatch match = regex.match(output);

    if (match.hasMatch()) {
        QString msg = tr("The APK failed to install. Error code: ") + match.captured(1);
        showErrorDialog(msg, tr("APK Installer"));
    }
}

void ToolWindow::slot_pushCanceled()
{
    if (mPushProcess.state() != QProcess::NotRunning) {
        mPushProcess.kill();
    }
    mPushDialog.setMaximum(0); // Reset the dialog for next time.
    mFilesToPush.clear();
}

void ToolWindow::slot_pushFinished(int exitStatus)
{
    if (exitStatus) {
        QByteArray er = mPushProcess.readAllStandardError();
        er = er.replace('\n', "<br/>");
        QString msg = tr("Unable to copy files. Output:<br/><br/>") + QString(er);
        showErrorDialog(msg, tr("File Copy"));
    }

    if (mFilesToPush.isEmpty()) {
        mPushDialog.setMaximum(0); // Reset the dialog for next time.
        mPushDialog.close();
    } else {
        mPushDialog.setValue(mPushDialog.value() + 1);

        // Prepare the base command
        QStringList args;
        QString command = getAdbFullPath(&args);
        if (command.isNull()) {
            return;
        }
        args << "push";
        args << mFilesToPush.dequeue().toLocalFile();
        args << REMOTE_DOWNLOADS_DIR;

        // Keep track of this process
        mPushProcess.start(command, args);
        mPushProcess.waitForStarted();
    }
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

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

#include "android/skin/qt/extended-window.h"
#include "android/skin/qt/extended-window-styles.h"

#include "android/main-common.h"
#include "android/skin/keyset.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/extended-pages/common.h"
#include "android/skin/qt/qt-settings.h"
#include "android/skin/qt/stylesheet.h"
#include "android/skin/qt/tool-window.h"

#include "ui_extended.h"

#include <QDesktopWidget>

ExtendedWindow::ExtendedWindow(
    EmulatorQtWindow *eW,
    ToolWindow *tW,
    const UiEmuAgent *agentPtr,
    const ShortcutKeyStore<QtUICommand>* shortcuts) :
    QFrame(nullptr),
    mEmulatorWindow(eW),
    mToolWindow(tW),
    mExtendedUi(new Ui::ExtendedControls),
    mSizeTweaker(this),
    mSidebarButtons(this)
{
    // "Tool" type windows live in another layer on top of everything in OSX, which is undesirable
    // because it means the extended window must be on top of the emulator window. However, on
    // Windows and Linux, "Tool" type windows are the only way to make a window that does not have
    // its own taskbar item.
#ifdef __APPLE__
    Qt::WindowFlags flag = Qt::Dialog;
#else
    Qt::WindowFlags flag = Qt::Tool;
#endif

    setWindowFlags(flag | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);

    QSettings settings;
    bool onTop = settings.value(Ui::Settings::ALWAYS_ON_TOP, false).toBool();
    setFrameOnTop(this, onTop);

    mExtendedUi->setupUi(this);
    mExtendedUi->cellular_page->setCellularAgent(agentPtr->cellular);
    mExtendedUi->batteryPage->setBatteryAgent(agentPtr->battery);
    mExtendedUi->telephonyPage->setTelephonyAgent(agentPtr->telephony);
    mExtendedUi->finger_page->setFingerAgent(agentPtr->finger);
    mExtendedUi->helpPage->initialize(shortcuts);
    mExtendedUi->dpadPage->setEmulatorWindow(mEmulatorWindow);
    mExtendedUi->location_page->setLocationAgent(agentPtr->location);
    mExtendedUi->virtualSensorsPage->setSensorsAgent(agentPtr->sensors);
    mExtendedUi->virtualSensorsPage->setLayoutChangeNotifier(eW);

    connect(
        mExtendedUi->settingsPage, SIGNAL(onTopChanged(bool)),
        this, SLOT(switchOnTop(bool)));

    connect(
        mExtendedUi->settingsPage, SIGNAL(onForwardShortcutsToDeviceChanged(int)),
        mEmulatorWindow, SLOT(setForwardShortcutsToDevice(int)));

    connect(
        mExtendedUi->settingsPage, SIGNAL(themeChanged(SettingsTheme)),
        this, SLOT(switchToTheme(SettingsTheme)));

    mPaneButtonMap = {
        {PANE_IDX_LOCATION,  mExtendedUi->locationButton},
        {PANE_IDX_CELLULAR,  mExtendedUi->cellularButton},
        {PANE_IDX_BATTERY,   mExtendedUi->batteryButton},
        {PANE_IDX_TELEPHONE, mExtendedUi->telephoneButton},
        {PANE_IDX_DPAD,      mExtendedUi->dpadButton},
        {PANE_IDX_FINGER,    mExtendedUi->fingerButton},
        {PANE_IDX_VIRT_SENSORS, mExtendedUi->virtSensorsButton},
        {PANE_IDX_SETTINGS,  mExtendedUi->settingsButton},
        {PANE_IDX_HELP,      mExtendedUi->helpButton},
    };

    setObjectName("ExtendedControls");

    mSidebarButtons.addButton(mExtendedUi->locationButton);
    mSidebarButtons.addButton(mExtendedUi->cellularButton);
    mSidebarButtons.addButton(mExtendedUi->batteryButton);
    mSidebarButtons.addButton(mExtendedUi->telephoneButton);
    mSidebarButtons.addButton(mExtendedUi->dpadButton);
    mSidebarButtons.addButton(mExtendedUi->fingerButton);
    mSidebarButtons.addButton(mExtendedUi->virtSensorsButton);
    mSidebarButtons.addButton(mExtendedUi->settingsButton);
    mSidebarButtons.addButton(mExtendedUi->helpButton);

    for (QWidget* w : findChildren<QWidget*>()) {
        w->setAttribute(Qt::WA_MacShowFocusRect, false);
    }
}

ExtendedWindow::~ExtendedWindow() {
    mExtendedUi->location_page->requestStopLoadingGeoData();
}

void ExtendedWindow::show() {
    QFrame::show();

    // Verify that the extended pane is fully visible (otherwise it may be
    // impossible for the user to move it)
    QDesktopWidget *desktop = static_cast<QApplication*>(
                                     QApplication::instance() )->desktop();
    int screenNum = desktop->screenNumber(this); // Screen holding most of this

    QRect screenGeo = desktop->screenGeometry(screenNum);
    QRect myGeo = geometry();

    bool moved = false;
    // Leave some padding between the window and the edge of the screen.
    // This distance isn't precise--it's mainly to prevent the window from
    // looking like it's a little off screen.
    static const int gap = 10;
    if (myGeo.x() + myGeo.width() > screenGeo.x() + screenGeo.width() - gap) {
        // Right edge is off the screen
        myGeo.setX(screenGeo.x() + screenGeo.width() - myGeo.width() - gap);
        moved = true;
    }
    if (myGeo.y() + myGeo.height() > screenGeo.y() + screenGeo.height() - gap) {
        // Bottom edge is off the screen
        myGeo.setY(screenGeo.y() + screenGeo.height() - myGeo.height() - gap);
        moved = true;
    }
    if (myGeo.x() < screenGeo.x() + gap) {
        // Top edge is off the screen
        myGeo.setX(screenGeo.x() + gap);
        moved = true;
    }
    if (myGeo.y() < screenGeo.y() + gap) {
        // Left edge is off the screen
        myGeo.setY(screenGeo.y() + gap);
        moved = true;
    }
    if (moved) {
        setGeometry(myGeo);
    }
}

void ExtendedWindow::showPane(ExtendedWindowPane pane) {
    show();
    adjustTabs(pane);
}

void ExtendedWindow::closeEvent(QCloseEvent *e) {
    // Merely hide the window the widget is closed, do not destroy state.
    e->ignore();
    hide();
}

void ExtendedWindow::keyPressEvent(QKeyEvent* e) {
    mToolWindow->handleQtKeyEvent(e);
}

// Tab buttons. Each raises its stacked pane to the top.
void ExtendedWindow::on_batteryButton_clicked()     { adjustTabs(PANE_IDX_BATTERY); }
void ExtendedWindow::on_cellularButton_clicked()    { adjustTabs(PANE_IDX_CELLULAR); }
void ExtendedWindow::on_dpadButton_clicked()        { adjustTabs(PANE_IDX_DPAD); }
void ExtendedWindow::on_fingerButton_clicked()      { adjustTabs(PANE_IDX_FINGER); }
void ExtendedWindow::on_helpButton_clicked()        { adjustTabs(PANE_IDX_HELP); }
void ExtendedWindow::on_locationButton_clicked()    { adjustTabs(PANE_IDX_LOCATION); }
void ExtendedWindow::on_settingsButton_clicked()    { adjustTabs(PANE_IDX_SETTINGS); }
void ExtendedWindow::on_telephoneButton_clicked()   { adjustTabs(PANE_IDX_TELEPHONE); }
void ExtendedWindow::on_virtSensorsButton_clicked() { adjustTabs(PANE_IDX_VIRT_SENSORS); }


void ExtendedWindow::adjustTabs(ExtendedWindowPane thisIndex) {
    auto it = mPaneButtonMap.find(thisIndex);
    if (it == mPaneButtonMap.end()) {
        return;
    }
    QPushButton* thisButton = it->second;
    thisButton->toggle();
    thisButton->clearFocus(); // It looks better when not highlighted
    mExtendedUi->stackedWidget->setCurrentIndex(static_cast<int>(thisIndex));
}

void ExtendedWindow::switchOnTop(bool isOnTop) {
    mEmulatorWindow->setOnTop(isOnTop);
    setFrameOnTop(this, isOnTop);
}

void ExtendedWindow::switchToTheme(SettingsTheme theme) {
    // Switch to the icon images that are appropriate for this theme.
    adjustAllButtonsForTheme(theme);

    // Set the Qt style.

    // The first part is based on the display's pixel density.
    // Most displays give 1.0; high density displays give 2.0.
    double densityFactor = 1.0;
    if (skin_winsys_get_device_pixel_ratio(&densityFactor) != 0) {
        // Failed: use 1.0
        densityFactor = 1.0;
    }
    QString styleString = Ui::fontStylesheet(densityFactor > 1.5);

    // The second part is based on the theme
    // Set the style for this theme
    styleString += Ui::stylesheetForTheme(theme);

    // Apply this style to the extended window (this),
    // and to the main tool-bar.
    this->setStyleSheet(styleString);
    mToolWindow->setStyleSheet(styleString);

    // Force a re-draw to make the new style take effect
    this->style()->unpolish(mExtendedUi->stackedWidget);
    this->style()->polish(mExtendedUi->stackedWidget);
    this->update();

    // Make the Settings pane active (still)
    adjustTabs(PANE_IDX_SETTINGS);
}

void ExtendedWindow::showEvent(QShowEvent* e) {
    if (mFirstShowEvent && !e->spontaneous()) {
        // This function has things that must be performed
        // after the ctor and after show() is called
        switchToTheme(getSelectedTheme());

        // Set the first tab active
        on_locationButton_clicked();

        mFirstShowEvent = false;

        // There is a gap between the main window and the tool bar. Use the same
        // gap between the tool bar and the extended window.
        move(mToolWindow->geometry().right() + ToolWindow::toolGap,
             mToolWindow->geometry().top());
    }
    QFrame::showEvent(e);
}

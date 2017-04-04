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

#include "android/emulation/control/AdbBugReportServices.h"
#include "android/skin/qt/emulator-qt-window.h"

#include "ui_bug-report-window.h"

#include <QFrame>
#include <QMessageBox>

#include <memory>
#include <string>

class BugReportWindow : public QFrame {
    Q_OBJECT

public:
    explicit BugReportWindow(EmulatorQtWindow* eW, QWidget* parent = 0);
    void showEvent(QShowEvent* event);

private:
    void loadAdbBugreport();
    void loadAdbLogcat();
    void loadAvdDetails();
    void loadScreenshotImage();
    void loadScreenshotImageDone(
            android::emulation::ScreenCapturer::Result result,
            android::base::StringView filePath);
    bool eventFilter(QObject* object, QEvent* event) override;

    EmulatorQtWindow* mEmulatorWindow;
    android::emulation::AdbInterface* mAdb;
    android::emulation::AdbBugReportServices mBugReportServices;
    android::emulation::ScreenCapturer* mScreenCapturer;
    QMessageBox* mDeviceDetailsDialog;
    std::unique_ptr<Ui::BugReportWindow> mUi;
    std::atomic_bool mBugReportSucceed;
    std::atomic_bool mScreenshotSucceed;
    std::string mBugReportSaveLocation;
    android::base::StringView mAdbBugreportFilePath;
    android::base::StringView mScreenshotFilePath;
    std::string mEmulatorVer;
    std::string mAndroidVer;
    std::string mHostOsName;
    std::string mDeviceName;
    bool mFirstShowEvent = true;
    std::string mAvdDetails;
};
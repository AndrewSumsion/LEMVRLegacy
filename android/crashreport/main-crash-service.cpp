// Copyright 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * This is the source for the crash service that is spawned by the main
 * emulator.  It is spawned once per emulator instance and services that
 * emulator instance in case of a crash.
 *
 * Confirmation to send the crash dump is requested by a gui element.
 *
 * Once confirmation is given, the crash dump is curl'd to google crash servers.
 */
#include "android/crashreport/CrashService.h"
#include "android/crashreport/CrashSystem.h"
#include "android/crashreport/ui/ConfirmDialog.h"
#include "android/qt/qt_path.h"
#include "android/skin/qt/qt-settings.h"
#include "android/utils/debug.h"
#include "android/version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QProgressDialog>
#include <QSettings>
#include <QTimer>
#include <QThread>
#include <stdio.h>
#include <stdlib.h>
#include <memory>

#define E(...) derror(__VA_ARGS__)
#define W(...) dwarning(__VA_ARGS__)
#define D(...) VERBOSE_PRINT(init, __VA_ARGS__)
#define I(...) printf(__VA_ARGS__)

static bool displayConfirmDialog(
        android::crashreport::CrashService* crashservice,
        Ui::Settings::CRASHREPORT_PREFERENCE_VALUE reportPreference) {
    ConfirmDialog msgBox(nullptr, crashservice, reportPreference);

    if (reportPreference == Ui::Settings::CRASHREPORT_PREFERENCE_ASK) {
        msgBox.show();
        int ret = msgBox.exec();
        return ret == ConfirmDialog::Accepted;
    } else if (reportPreference == Ui::Settings::CRASHREPORT_PREFERENCE_ALWAYS) {
        msgBox.sendReport();
        return true;
    } else if (reportPreference == Ui::Settings::CRASHREPORT_PREFERENCE_NEVER) {
        return false;
    }
}

static void InitQt(int argc, char** argv) {
    Q_INIT_RESOURCE(resources);

    // Give Qt the fonts from our resource file
    QFontDatabase fontDb;
    int fontId = fontDb.addApplicationFont(":/lib/fonts/Roboto");
    if (fontId < 0) {
        D("Count not load font resource: \":/lib/fonts/Roboto");
    }
    fontId = fontDb.addApplicationFont(":/lib/fonts/Roboto-Bold");
    if (fontId < 0) {
        D("Count not load font resource: \":/lib/fonts/Roboto-Bold");
    }
    fontId = fontDb.addApplicationFont(":/lib/fonts/Roboto-Medium");
    if (fontId < 0) {
        D("Count not load font resource: \":/lib/fonts/Roboto-Medium");
    }
}

/* Main routine */
int main(int argc, char** argv) {
    // Parse args
    const char* service_arg = nullptr;
    const char* dump_file = nullptr;
    const char* data_dir = nullptr;
    int ppid = 0;
    for (int nn = 1; nn < argc; nn++) {
        const char* opt = argv[nn];
        if (!strcmp(opt, "-pipe")) {
            if (nn + 1 < argc) {
                nn++;
                service_arg = argv[nn];
            }
        } else if (!strcmp(opt, "-dumpfile")) {
            if (nn + 1 < argc) {
                nn++;
                dump_file = argv[nn];
            }
        } else if (!strcmp(opt, "-ppid")) {
            if (nn + 1 < argc) {
                nn++;
                ppid = atoi(argv[nn]);
            }
        } else if (!strcmp(opt, "-data-dir")) {
            if (nn + 1 < argc) {
                nn++;
                data_dir = argv[nn];
            }
        }
    }

    auto crashservice = ::android::crashreport::CrashService::makeCrashService(
            EMULATOR_VERSION_STRING, EMULATOR_BUILD_STRING, data_dir);
    if (dump_file &&
        ::android::crashreport::CrashSystem::get()->isDump(dump_file)) {
        crashservice->setDumpFile(dump_file);
    } else if (service_arg && ppid) {
        if (!crashservice->startCrashServer(service_arg)) {
            return 1;
        }
        if (crashservice->waitForDumpFile(ppid) == -1) {
            return 1;
        }
        crashservice->stopCrashServer();
        if (crashservice->getDumpFile().empty()) {
            // No crash dump created
            return 0;
        }
    } else {
        E("Must supply a dump path\n");
        return 1;
    }

    if (!crashservice->validDumpFile()) {
        E("CrashPath '%s' is invalid\n", crashservice->getDumpFile().c_str());
        return 1;
    }

    crashservice->collectDataFiles();

    QCoreApplication::setOrganizationName(Ui::Settings::ORG_NAME);
    QCoreApplication::setOrganizationDomain(Ui::Settings::ORG_DOMAIN);
    QCoreApplication::setApplicationName(Ui::Settings::APP_NAME);

    QSettings settings;
    auto reportPreference =
        static_cast<Ui::Settings::CRASHREPORT_PREFERENCE_VALUE>(
            settings.value(Ui::Settings::CRASHREPORT_PREFERENCE, 0).toInt());

    QApplication app(argc, argv);

    InitQt(argc, argv);

    if (!displayConfirmDialog(crashservice.get(),
                              reportPreference)) {
        return 1;
    }

    return 0;
}

/* Copyright (C) 2014-2015 The Android Open Source Project
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
#include <stdio.h>
#ifdef CONFIG_POSIX
#include <pthread.h>
#endif

#include "android/base/system/System.h"
#include "android/qt/qt_path.h"
#include "android/skin/rect.h"
#include "android/skin/resource.h"
#include "android/skin/winsys.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/emulator-qt-no-window.h"
#include "android/utils/setenv.h"
#include "android/main-common-ui.h"

#include <QtCore>
#include <QApplication>
#include <QCoreApplication>
#include <QDesktopWidget>
#include <QFontDatabase>
#include <QMenu>
#include <QMenuBar>
#include <QRect>
#include <QSemaphore>
#include <QThread>
#include <QWidget>

#ifdef Q_OS_LINUX
// This include needs to be after all the Qt includes
// because it defines macros/types that conflict with
// qt's own macros/types.
#include <X11/Xlib.h>
#endif

using android::base::System;
using android::base::String;

#define  DEBUG  1

#if DEBUG
#include "android/utils/debug.h"

#define  D(...)   VERBOSE_PRINT(surface,__VA_ARGS__)
#else
#define  D(...)   ((void)0)
#endif

struct GlobalState {
    int argc;
    char** argv;
    QCoreApplication* app;
    bool window_pos_saved;
    int window_pos_x;
    int window_pos_y;
};

static GlobalState* globalState() {
    static GlobalState sGlobalState = {
        .argc = 0,
        .argv = NULL,
        .app = NULL,
        .window_pos_saved = false,
        .window_pos_x = 0,
        .window_pos_y = 0,
    };
    return &sGlobalState;
}

extern void skin_winsys_enter_main_loop(bool no_window, int argc, char** argv) {
    D("Starting QT main loop\n");

    // Make Qt look at the libraries within this installation
    String qtPath = androidQtGetLibraryDir();
    QStringList pathList(qtPath.c_str());
    QCoreApplication::setLibraryPaths(pathList);
    D("Qt lib path: %s\n", qtPath.c_str());

    if (!no_window) {
        // Give Qt the fonts from our resource file
        QFontDatabase fontDb;
        int fontId = fontDb.addApplicationFont(":/lib/fonts/Roboto");
        if (fontId < 0) {
            D("Could not load font resource: \":/lib/fonts/Roboto");
        }

        fontId = fontDb.addApplicationFont(":/lib/fonts/Roboto-Bold");
        if (fontId < 0) {
            D("Could not load font resource: \":/lib/fonts/Roboto-Bold");
        }

        fontId = fontDb.addApplicationFont(":/lib/fonts/Roboto-Medium");
        if (fontId < 0) {
            D("Could not load font resource: \":/lib/fonts/Roboto-Medium");
        }
    }

    GlobalState* g = globalState();
    g->argc = argc;
    g->argv = argv;
    g->app->exec();
    D("Finished QT main loop\n");
}

extern void skin_winsys_get_monitor_rect(SkinRect *rect)
{
    QRect qrect;
    QSemaphore semaphore;
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return;
    }
    window->getScreenDimensions(&qrect, &semaphore);
    semaphore.acquire();
    rect->pos.x = qrect.left();
    rect->pos.y = qrect.top();
    rect->size.w = qrect.width();
    rect->size.h = qrect.height();
    D("%s: (%d,%d) %dx%d", __FUNCTION__, rect->pos.x, rect->pos.y,
      rect->size.w, rect->size.h);
}

extern int skin_winsys_get_device_pixel_ratio(double *dpr)
{
    D("skin_winsys_get_device_pixel_ratio");
    QSemaphore semaphore;
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return -1;
    }
    window->getDevicePixelRatio(dpr, &semaphore);
    semaphore.acquire();
    D("%s: result=%f", __FUNCTION__, *dpr);
    return 0;
}

extern int skin_winsys_get_monitor_dpi(int *x, int *y)
{
    D("skin_winsys_get_monitor_dpi");
    QSemaphore semaphore;
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return -1;
    }
    int value;
    window->getMonitorDpi(&value, &semaphore);
    semaphore.acquire();
    *x = *y = value;
    D("%s: result=%d", __FUNCTION__, value);
    return 0;
}

extern void *skin_winsys_get_window_handle(void)
{
    D("skin_winsys_get_window_handle");
    WId handle;
    QSemaphore semaphore;
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return NULL;
    }
    window->getWindowId(&handle, &semaphore);
    semaphore.acquire();
    D("%s: result = 0x%p", __FUNCTION__, (void*)handle);
    return (void*)handle;
}

extern void skin_winsys_get_window_pos(int *x, int *y)
{
    D("skin_winsys_get_window_pos");
    GlobalState* g = globalState();
    if (g->window_pos_saved) {
        *x = g->window_pos_x;
        *y = g->window_pos_y;
    } else {
        QSemaphore semaphore;
        EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
        if (window == NULL) {
            D("%s: Could not get window handle", __FUNCTION__);
            return;
        }
        window->getWindowPos(x, y, &semaphore);
        semaphore.acquire();
    }
    D("%s: x=%d y=%d", __FUNCTION__, *x, *y);
}

extern void skin_winsys_save_window_pos() {
    int x = 0, y = 0;
    skin_winsys_get_window_pos(&x, &y);
    GlobalState* g = globalState();
    g->window_pos_saved = true;
    g->window_pos_x = x;
    g->window_pos_y = y;
}

extern bool skin_winsys_is_window_fully_visible()
{
    D("skin_winsys_is_window_fully_visible");
    QSemaphore semaphore;
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return true;
    }
    bool value;
    window->isWindowFullyVisible(&value, &semaphore);
    semaphore.acquire();
    D("%s: result = %s", __FUNCTION__, value ? "true" : "false");
    return value;
}

extern void skin_winsys_quit_request()
{
    D(__FUNCTION__);
    auto window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return;
    }
    window->requestClose();
}

void skin_winsys_destroy() {
    D(__FUNCTION__);

    // Mac is still causing us troubles - it somehow manages to not call the
    // main window destructor (in qemi1 only!) and crashes if QApplication
    // is destroyed right here. So let's delay the deletion for now
#ifdef __APPLE__
    atexit([] {
        delete globalState()->app;
        globalState()->app = nullptr;
    });
#else
    delete globalState()->app;
    globalState()->app = nullptr;
#endif
}

extern void skin_winsys_set_relative_mouse_mode(bool)
{
    D("skin_winsys_set_relative_mouse_mode");
}

extern void skin_winsys_set_window_icon(const unsigned char *data, size_t size)
{
    D("skin_winsys_set_window_icon");
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return;
    }
    window->setWindowIcon(data, size);
}

extern void skin_winsys_set_window_pos(int x, int y)
{
    D("skin_winsys_set_window_pos %d, %d", x, y);
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return;
    }
    QSemaphore semaphore;
    window->setWindowPos(x, y, &semaphore);
    semaphore.acquire();
}

void skin_winsys_get_window_borders(int *left, int *right, int *top, int *bottom) {
    // this function is for backward compatibility with SDL windows,
    // where window border is not accounted for in window dimensions
    // and is required when re-positioning windows in _WIN32
    *left = *right = *top = *bottom = 0;

    return;
}


extern void skin_winsys_set_window_title(const char *title)
{
    D("skin_winsys_set_window_title [%s]", title);
    QSemaphore semaphore;
    EmulatorQtWindow *window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return;
    }
    QString qtitle(title);
    window->setTitle(&qtitle, &semaphore);
    semaphore.acquire();
}

extern void skin_winsys_spawn_thread(bool no_window,
                                     StartFunction f,
                                     int argc,
                                     char** argv) {
    D("skin_spawn_thread");
    if (no_window) {
        EmulatorQtNoWindow* guiless_window = EmulatorQtNoWindow::getInstance();
        if (guiless_window == NULL) {
            D("%s: Could not get window handle", __FUNCTION__);
            return;
        }
        guiless_window->startThread([f, argc, argv] { f(argc, argv); });
    } else {
        EmulatorQtWindow* window = EmulatorQtWindow::getInstance();
        if (window == NULL) {
            D("%s: Could not get window handle", __FUNCTION__);
            return;
        }
        window->startThread(f, argc, argv);
    }
}

extern void skin_winsys_start(bool no_window, bool raw_keys) {
    GlobalState* g = globalState();
#ifdef Q_OS_LINUX
    // This call is required to make doing OpenGL stuff on the UI
    // thread safe. The AA_X11InitThreads flag in Qt does not actually
    // work (confirmed by grepping through Qt code).
    XInitThreads();
#endif
    if (no_window) {
        g->app = new QCoreApplication(g->argc, g->argv);
        EmulatorQtNoWindow::create();
    } else {
        g->app = new QApplication(g->argc, g->argv);
        g->app->setAttribute(Qt::AA_UseHighDpiPixmaps);
#ifdef __APPLE__
        // On OS X, Qt automatically generates an application menu with a "Quit"
        // item. For whatever reason, the auto-generated "quit" does not work,
        // or works intermittently.
        // For that reason, we explicitly create a "Quit" action for Qt to use
        // instead of the auto-generated one, and connect it to the QApplication's
        // quit() slot.
        // Note: the objects pointed to by quitMenu, quitAction and mainBar will remain
        // for the entire lifetime of the application so we don't bother cleaning
        // them up.
        QMenu* quitMenu = new QMenu(nullptr);
        QAction* quitAction = new QAction(g->app->tr("Quit Emulator"), quitMenu);
        QMenuBar* mainBar = new QMenuBar(nullptr);
        QObject::connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
        quitMenu->addAction(quitAction);
        mainBar->addMenu(quitMenu);
        qt_mac_set_dock_menu(quitMenu);
#endif
        EmulatorQtWindow::create();
    }
}

extern void skin_winsys_run_ui_update(SkinGenericFunction f, void* data) {
    D(__FUNCTION__);
    QSemaphore semaphore;
    EmulatorQtWindow* const window = EmulatorQtWindow::getInstance();
    if (window == NULL) {
        D("%s: Could not get window handle", __FUNCTION__);
        return;
    }
    window->runOnUiThread(&f, data, &semaphore);
    semaphore.acquire();
}

#ifdef _WIN32
extern "C" int qt_main(int, char**);

int qMain(int argc, char** argv) {
    return qt_main(argc, argv);
}
#endif  // _WIN32

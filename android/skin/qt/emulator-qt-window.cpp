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

#include <QtCore>
#include <QAbstractSlider>
#include <QCheckBox>
#include <QCursor>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSemaphore>
#include <QWindow>

#include "android/base/files/PathUtils.h"
#include "android/base/memory/LazyInstance.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/cpu_accelerator.h"
#include "android/emulation/control/user_event_agent.h"
#include "android/emulator-window.h"

#include "android/skin/event.h"
#include "android/skin/keycode.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/qt-settings.h"
#include "android/skin/qt/winsys-qt.h"
#include "android/ui-emu-agent.h"

#if defined(_WIN32)
#include "android/skin/qt/windows-native-window.h"
#endif

#define  DEBUG  1

#if DEBUG
#include "android/utils/debug.h"
#define  D(...)   VERBOSE_PRINT(surface,__VA_ARGS__)
#else
#define  D(...)   ((void)0)
#endif

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

using namespace android::base;

// Make sure it is POD here
static LazyInstance<EmulatorQtWindow::Ptr> sInstance = LAZY_INSTANCE_INIT;

void EmulatorQtWindow::create()
{
    sInstance.get() = Ptr(new EmulatorQtWindow());
}

EmulatorQtWindow::EmulatorQtWindow(QWidget *parent) :
        QFrame(parent),
        mStartupDialog(this),
        mContainer(this),
        mOverlay(this, &mContainer),
        mZoomFactor(1.0),
        mInZoomMode(false),
        mNextIsZoom(false),
        mGrabKeyboardInput(false),
        mMouseInside(false),
        mPrevMousePosition(0, 0),
        mMainLoopThread(nullptr),
        mAvdWarningBox(QMessageBox::Information,
                       tr("Recommended AVD"),
                       tr("Running an x86 based Android Virtual Device (AVD) is 10x faster.<br/>"
                          "We strongly recommend creating a new AVD."),
                       QMessageBox::Ok,
                       this),
        mFirstShowEvent(true)
{
    // Start a timer. If the main window doesn't
    // appear before the timer expires, show a
    // pop-up to let the user know we're still
    // working.
    QObject::connect(&mStartupTimer, &QTimer::timeout,
                     this, &EmulatorQtWindow::slot_startupTick);
    mStartupTimer.setSingleShot(true);
    mStartupTimer.setInterval(500); // Half a second
    mStartupTimer.start();

    backing_surface = NULL;
    batteryState    = NULL;

    tool_window = new ToolWindow(this, &mContainer);

    this->setAcceptDrops(true);

    QObject::connect(this, &EmulatorQtWindow::blit, this, &EmulatorQtWindow::slot_blit);
    QObject::connect(this, &EmulatorQtWindow::createBitmap, this, &EmulatorQtWindow::slot_createBitmap);
    QObject::connect(this, &EmulatorQtWindow::fill, this, &EmulatorQtWindow::slot_fill);
    QObject::connect(this, &EmulatorQtWindow::getBitmapInfo, this, &EmulatorQtWindow::slot_getBitmapInfo);
    QObject::connect(this, &EmulatorQtWindow::getDevicePixelRatio, this, &EmulatorQtWindow::slot_getDevicePixelRatio);
    QObject::connect(this, &EmulatorQtWindow::getMonitorDpi, this, &EmulatorQtWindow::slot_getMonitorDpi);
    QObject::connect(this, &EmulatorQtWindow::getScreenDimensions, this, &EmulatorQtWindow::slot_getScreenDimensions);
    QObject::connect(this, &EmulatorQtWindow::getWindowId, this, &EmulatorQtWindow::slot_getWindowId);
    QObject::connect(this, &EmulatorQtWindow::getWindowPos, this, &EmulatorQtWindow::slot_getWindowPos);
    QObject::connect(this, &EmulatorQtWindow::isWindowFullyVisible, this, &EmulatorQtWindow::slot_isWindowFullyVisible);
    QObject::connect(this, &EmulatorQtWindow::pollEvent, this, &EmulatorQtWindow::slot_pollEvent);
    QObject::connect(this, &EmulatorQtWindow::queueEvent, this, &EmulatorQtWindow::slot_queueEvent);
    QObject::connect(this, &EmulatorQtWindow::releaseBitmap, this, &EmulatorQtWindow::slot_releaseBitmap);
    QObject::connect(this, &EmulatorQtWindow::requestClose, this, &EmulatorQtWindow::slot_requestClose);
    QObject::connect(this, &EmulatorQtWindow::requestUpdate, this, &EmulatorQtWindow::slot_requestUpdate);
    QObject::connect(this, &EmulatorQtWindow::setWindowIcon, this, &EmulatorQtWindow::slot_setWindowIcon);
    QObject::connect(this, &EmulatorQtWindow::setWindowPos, this, &EmulatorQtWindow::slot_setWindowPos);
    QObject::connect(this, &EmulatorQtWindow::setTitle, this, &EmulatorQtWindow::slot_setWindowTitle);
    QObject::connect(this, &EmulatorQtWindow::showWindow, this, &EmulatorQtWindow::slot_showWindow);
    QObject::connect(this, &EmulatorQtWindow::runOnUiThread, this, &EmulatorQtWindow::slot_runOnUiThread);
    QObject::connect(QApplication::instance(), &QCoreApplication::aboutToQuit, this, &EmulatorQtWindow::slot_clearInstance);

    QObject::connect(&mScreencapProcess, SIGNAL(finished(int)), this, SLOT(slot_screencapFinished(int)));
    QObject::connect(&mScreencapPullProcess,
                     SIGNAL(error(QProcess::ProcessError)), this,
                     SLOT(slot_showProcessErrorDialog(QProcess::ProcessError)));
    QObject::connect(&mScreencapPullProcess, SIGNAL(finished(int)), this, SLOT(slot_screencapPullFinished(int)));
    QObject::connect(&mScreencapPullProcess,
                     SIGNAL(error(QProcess::ProcessError)), this,
                     SLOT(slot_showProcessErrorDialog(QProcess::ProcessError)));

    QObject::connect(mContainer.horizontalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(slot_horizontalScrollChanged(int)));
    QObject::connect(mContainer.verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(slot_verticalScrollChanged(int)));
    QObject::connect(mContainer.horizontalScrollBar(), SIGNAL(rangeChanged(int, int)), this, SLOT(slot_scrollRangeChanged(int,int)));
    QObject::connect(mContainer.verticalScrollBar(), SIGNAL(rangeChanged(int, int)), this, SLOT(slot_scrollRangeChanged(int,int)));
    QObject::connect(tool_window, SIGNAL(skinUIEvent(SkinEvent*)), this, SLOT(slot_queueEvent(SkinEvent*)));

    mFlashAnimation.setStartValue(250);
    mFlashAnimation.setEndValue(0);
    mFlashAnimation.setEasingCurve(QEasingCurve::Linear);
    QObject::connect(&mFlashAnimation, SIGNAL(finished()), this, SLOT(slot_animationFinished()));
    QObject::connect(&mFlashAnimation, SIGNAL(valueChanged(QVariant)), this, SLOT(slot_animationValueChanged(QVariant)));

    QSettings settings;
    bool onTop = settings.value(Ui::Settings::ALWAYS_ON_TOP, false).toBool();
    setOnTop(onTop);

    mResizeTimer.setSingleShot(true);
    QObject::connect(&mResizeTimer, SIGNAL(timeout()), this, SLOT(slot_resizeDone()));
}

EmulatorQtWindow::Ptr EmulatorQtWindow::getInstancePtr()
{
    return sInstance.get();
}

EmulatorQtWindow* EmulatorQtWindow::getInstance()
{
    return getInstancePtr().get();
}

EmulatorQtWindow::~EmulatorQtWindow()
{
    if (tool_window) {
        delete tool_window;
        tool_window = NULL;
    }

    delete mMainLoopThread;
}

void EmulatorQtWindow::showAvdArchWarning()
{
    ScopedCPtr<char> arch(avdInfo_getTargetCpuArch(android_avdInfo));
    if ( !strcmp(arch.get(), "x86") || !strcmp(arch.get(), "x86_64")) {
        return;
    }

    // The following statuses indicate that the machine hardware does not support hardware
    // acceleration. These machines should never show a popup indicating to switch to x86.
    static const AndroidCpuAcceleration badStatuses[] = {
        ANDROID_CPU_ACCELERATION_NESTED_NOT_SUPPORTED,  // HAXM doesn't support nested VM
        ANDROID_CPU_ACCELERATION_INTEL_REQUIRED,        // HAXM requires GeniuneIntel processor
        ANDROID_CPU_ACCELERATION_NO_CPU_SUPPORT,        // CPU doesn't support required features (VT-x or SVM)
        ANDROID_CPU_ACCELERATION_NO_CPU_VTX_SUPPORT,    // CPU doesn't support VT-x
        ANDROID_CPU_ACCELERATION_NO_CPU_NX_SUPPORT,     // CPU doesn't support NX
    };

    AndroidCpuAcceleration cpuStatus = androidCpuAcceleration_getStatus(nullptr);
    for (AndroidCpuAcceleration status : badStatuses) {
        if (cpuStatus == status) {
            return;
        }
    }

    QSettings settings;
    if (settings.value(Ui::Settings::SHOW_AVD_ARCH_WARNING, true).toBool()) {
        QObject::connect(&mAvdWarningBox, SIGNAL(buttonClicked(QAbstractButton*)),
                         this, SLOT(slot_avdArchWarningMessageAccepted()));

        QCheckBox *checkbox = new QCheckBox(tr("Never show this again."));
        checkbox->setCheckState(Qt::Unchecked);
        mAvdWarningBox.setWindowModality(Qt::NonModal);
        mAvdWarningBox.setCheckBox(checkbox);
        mAvdWarningBox.show();
    }
}

void EmulatorQtWindow::slot_showProcessErrorDialog(
        QProcess::ProcessError exitStatus) {
    QString msg;
    switch (exitStatus) {
        case QProcess::Timedout:
            // Our wait for process starting is best effort. If we timed out,
            // meh.
            return;
        case QProcess::FailedToStart:
            msg =
                    tr("Failed to start process.<br/>"
                       "Check settings to verify that your chosen SDK path "
                       "is valid.");
            break;
        default:
            msg = tr("Unexpected error occured while grabbing screenshot.");
    }
    showErrorDialog(msg, tr("Screenshot"));
}

void EmulatorQtWindow::slot_startupTick() {
    // It's been a while since we were launched, and the main
    // window still hasn't appeared.
    // Show a pop-up that lets the user know we are working.

    mStartupDialog.setWindowTitle(tr("Android Emulator"));
    // Hide close/minimize/maximize buttons
    mStartupDialog.setWindowFlags(Qt::Dialog |
                                  Qt::CustomizeWindowHint |
                                  Qt::WindowTitleHint);
    // Make sure the icon is the same as in the main window
    mStartupDialog.setWindowIcon(QApplication::windowIcon());

    // Emulator logo
    QLabel *label = new QLabel();
    label->setAlignment(Qt::AlignCenter);
    QSize size;
    size.setWidth(mStartupDialog.size().width() / 2);
    size.setHeight(size.width());
    QPixmap pixmap = windowIcon().pixmap(size);
    label->setPixmap(pixmap);
    mStartupDialog.setLabel(label);

    // The default progress bar on Windows isn't centered for some reason
    QProgressBar *bar =  new QProgressBar();
    bar->setAlignment(Qt::AlignHCenter);
    mStartupDialog.setBar(bar);

    mStartupDialog.setRange(0, 0); // Don't show % complete
    mStartupDialog.setCancelButton(0);   // No "cancel" button
    mStartupDialog.show();
}

void EmulatorQtWindow::slot_avdArchWarningMessageAccepted()
{
    QCheckBox *checkbox = mAvdWarningBox.checkBox();
    if (checkbox->checkState() == Qt::Checked) {
        QSettings settings;
        settings.setValue(Ui::Settings::SHOW_AVD_ARCH_WARNING, false);
    }
}

void EmulatorQtWindow::closeEvent(QCloseEvent *event)
{
    if (mMainLoopThread && mMainLoopThread->isRunning()) {
        // run "adb shell stop" and call queueQuitEvent afterwards
        tool_window->runAdbShellStopAndQuit();
        event->ignore();
    } else {
        event->accept();
    }
}

void EmulatorQtWindow::queueQuitEvent()
{
    queueEvent(createSkinEvent(kEventQuit));
}

void EmulatorQtWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // Accept all drag enter events with any URL, then filter more in drop events
    // TODO: check this with hasFormats() using MIME type for .apk?
    if (event->mimeData() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void EmulatorQtWindow::dropEvent(QDropEvent *event)
{
    // Get the first url - if it's an APK and the only file, attempt to install it
    QList<QUrl> urls = event->mimeData()->urls();
    QString url = urls[0].toLocalFile();

    if (url.endsWith(".apk") && urls.length() == 1) {
        tool_window->runAdbInstall(url);
        return;
    } else {

        // If any of the files is an APK, intent was ambiguous
        for (int i = 0; i < urls.length(); i++) {
            if (urls[i].path().endsWith(".apk")) {
                showErrorDialog(tr("Drag-and-drop can either install a single APK"
                                   " file or copy one or more non-APK files to the"
                                   " Emulator SD card."),
                                tr("Drag and Drop"));
                return;
            }
        }
        tool_window->runAdbPush(urls);
    }
}

void EmulatorQtWindow::keyPressEvent(QKeyEvent *event)
{
    handleKeyEvent(kEventKeyDown, event);
}

void EmulatorQtWindow::keyReleaseEvent(QKeyEvent *event)
{
    handleKeyEvent(kEventKeyUp, event);

    // If the key event generated any text, we need
    // to send an additional TextInput event to the emulator.
    if (event->text().length() > 0) {
        SkinEvent *skin_event = createSkinEvent(kEventTextInput);
        skin_event->u.text.down = false;
        strncpy((char*)skin_event->u.text.text,
                (const char*)event->text().toUtf8().constData(),
                sizeof(skin_event->u.text.text) - 1);
        // Ensure the event's text is 0-terminated
        skin_event->u.text.text[sizeof(skin_event->u.text.text)-1] = 0;
        queueEvent(skin_event);
    }

    // If we enabled trackball mode, tell Qt to always forward mouse movement
    // events. Otherwise, Qt will forward them only when a button is pressed.
    EmulatorWindow* ew = emulator_window_get();
    bool trackballActive = skin_ui_is_trackball_active(ew->ui);
    if ( trackballActive != hasMouseTracking() ) {
        setMouseTracking(trackballActive);
    }
}
void EmulatorQtWindow::mouseMoveEvent(QMouseEvent *event)
{
    handleMouseEvent(kEventMouseMotion,
                     getSkinMouseButton(event),
                     event->pos());
}

void EmulatorQtWindow::mousePressEvent(QMouseEvent *event)
{
    QSettings settings;
    if (settings.value(Ui::Settings::ALLOW_KEYBOARD_GRAB, false).toBool()) {
        mGrabKeyboardInput = true;
    }
    handleMouseEvent(kEventMouseButtonDown,
                     getSkinMouseButton(event),
                     event->pos());
}

void EmulatorQtWindow::mouseReleaseEvent(QMouseEvent *event)
{
    handleMouseEvent(kEventMouseButtonUp,
                     getSkinMouseButton(event),
                     event->pos());
}

void EmulatorQtWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    QRect bg(QPoint(0, 0), this->size());

    painter.fillRect(bg, Qt::black);

    // Ensure we actually have a valid bitmap before attempting to
    // rescale
    if (backing_surface && !backing_surface->bitmap->isNull()) {
        QRect r(0, 0, backing_surface->w, backing_surface->h);
        // Rescale with smooth transformation to avoid aliasing
        QImage scaled_bitmap =
                backing_surface->bitmap->scaled(r.size() * devicePixelRatio(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
        scaled_bitmap.setDevicePixelRatio(devicePixelRatio());
        painter.drawImage(r, scaled_bitmap);
    } else {
        D("Painting emulator window, but no backing bitmap");
    }
}

void EmulatorQtWindow::activateWindow()
{
    mContainer.activateWindow();
}

void EmulatorQtWindow::raise()
{
    mContainer.raise();
    tool_window->raise();
}

void EmulatorQtWindow::show()
{
    mContainer.show();
    QFrame::show();
    tool_window->show();
    tool_window->dockMainWindow();

    QObject::connect(window()->windowHandle(), SIGNAL(screenChanged(QScreen *)), this, SLOT(slot_screenChanged(QScreen *)));
}

void EmulatorQtWindow::setOnTop(bool onTop)
{
#ifndef __linux__
    // On Linux, the Qt::WindowStaysOnTopHint only works if X11 window managment
    // is bypassed (Qt::X11BypassWindowManagerHint). Unfortunately, this prevents
    // a lot of common operations (like moving or resizing the window!), so the
    // "always on top" feature is disabled for Linux.

    const bool oldVisible = mContainer.isVisible();

    setFrameOnTop(&mContainer, onTop);
    setFrameOnTop(tool_window, onTop);

    if (oldVisible) {
        mContainer.show();
        tool_window->show();
    }
#endif
}

void EmulatorQtWindow::setFrameOnTop(QFrame* frame, bool onTop)
{
    Qt::WindowFlags flags = frame->windowFlags();

    if (onTop) {
        flags |=  Qt::WindowStaysOnTopHint;
    } else {
        flags &= ~Qt::WindowStaysOnTopHint;
    }
    frame->setWindowFlags(flags);
}

void EmulatorQtWindow::showMinimized()
{
    mContainer.showMinimized();
}

void EmulatorQtWindow::startThread(StartFunction f, int argc, char **argv)
{
    if (!mMainLoopThread) {
        mMainLoopThread = new MainLoopThread(f, argc, argv);
        QObject::connect(mMainLoopThread, &QThread::finished, &mContainer, &EmulatorWindowContainer::close);
        mMainLoopThread->start();
    } else {
        D("mMainLoopThread already started");
    }
}

void EmulatorQtWindow::slot_blit(QImage *src, QRect *srcRect, QImage *dst, QPoint *dstPos, QPainter::CompositionMode *op, QSemaphore *semaphore)
{
    QPainter painter(dst);
    painter.setCompositionMode(*op);
    painter.drawImage(*dstPos, *src, *srcRect);

    painter.setCompositionMode(QPainter::CompositionMode_Source);
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_clearInstance()
{
#ifndef __APPLE__
    if (tool_window) {
        delete tool_window;
        tool_window = NULL;
    }
#endif

    skin_winsys_save_window_pos();
    sInstance.get().reset();
}

void EmulatorQtWindow::slot_createBitmap(SkinSurface *s, int w, int h, QSemaphore *semaphore) {
    s->bitmap = new QImage(w, h, QImage::Format_ARGB32);
    if (s->bitmap->isNull()) {
        // Failed to create image, warn user.
        showErrorDialog(
                tr("Failed to allocate memory for the skin bitmap."
                   "Try configuring your AVD to not have a skin."),
                tr("Error displaying skin"));
    } else {
        s->bitmap->fill(0);
    }
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_fill(SkinSurface *s, const QRect *rect, const QColor *color, QSemaphore *semaphore)
{
    QPainter painter(s->bitmap);
    painter.fillRect(*rect, *color);
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_getBitmapInfo(SkinSurface *s, SkinSurfacePixels *pix, QSemaphore *semaphore)
{
    pix->pixels = (uint32_t*)s->bitmap->bits();
    pix->w = s->original_w;
    pix->h = s->original_h;
    pix->pitch = s->bitmap->bytesPerLine();
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_getDevicePixelRatio(double *out_dpr, QSemaphore *semaphore)
{
    *out_dpr = devicePixelRatio();
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_getMonitorDpi(int *out_dpi, QSemaphore *semaphore)
{
    *out_dpi = QApplication::screens().at(0)->logicalDotsPerInch();
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_getScreenDimensions(QRect *out_rect, QSemaphore *semaphore)
{
    QRect rect = ((QApplication*)QApplication::instance())->desktop()->screenGeometry();
    out_rect->setX(rect.x());
    out_rect->setY(rect.y());

    // Always report slightly smaller-than-actual dimensions to prevent odd resizing behavior,
    // which can happen if things like the OSX dock are not taken into account. The difference
    // below is specifically to take into account the OSX dock.
    out_rect->setWidth(rect.width() * .95);
#ifdef __APPLE__
    out_rect->setHeight(rect.height() * .85);
#else // _WIN32 || __linux__
    out_rect->setHeight(rect.height() * .95);
#endif

    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_getWindowId(WId *out_id, QSemaphore *semaphore)
{
    WId wid = effectiveWinId();
    D("Effective win ID is %lx", wid);
#if defined(__APPLE__)
    wid = (WId)getNSWindow((void*)wid);
    D("After finding parent, win ID is %lx", wid);
#endif
    *out_id = wid;
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_getWindowPos(int *xx, int *yy, QSemaphore *semaphore)
{
    // Note that mContainer.x() == mContainer.frameGeometry().x(), which
    // is NOT what we want.

    QRect geom = mContainer.geometry();

    *xx = geom.x();
    *yy = geom.y();
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_isWindowFullyVisible(bool *out_value, QSemaphore *semaphore)
{
    QDesktopWidget *desktop = ((QApplication*)QApplication::instance())->desktop();
    int   screenNum = desktop->screenNumber(&mContainer); // Screen holding the app
    QRect screenGeo = desktop->screenGeometry(screenNum);

    *out_value = screenGeo.contains( mContainer.geometry() );

    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_pollEvent(SkinEvent *event, bool *hasEvent, QSemaphore *semaphore)
{
    if (event_queue.isEmpty()) {
        *hasEvent = false;
    } else {
        *hasEvent = true;
        SkinEvent *newEvent = event_queue.dequeue();
        memcpy(event, newEvent, sizeof(SkinEvent));
        delete newEvent;
    }
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_queueEvent(SkinEvent *event, QSemaphore *semaphore)
{
    const bool firstEvent = event_queue.isEmpty();

    // For the following two events, only the "last" example of said event matters, so ensure
    // that there is only one of them in the queue at a time.
    bool replaced = false;
    if (event->type == kEventScrollBarChanged || event->type == kEventZoomedWindowResized) {
        for (int i = 0; i < event_queue.size(); i++) {
            if (event_queue.at(i)->type == event->type) {
                SkinEvent *toDelete = event_queue.at(i);
                event_queue.replace(i, event);
                delete toDelete;
                replaced = true;
                break;
            }
        }
    }

    if (!replaced) event_queue.enqueue(event);

    const auto uiAgent = tool_window->getUiEmuAgent();
    if (firstEvent && uiAgent && uiAgent->userEvents
            && uiAgent->userEvents->onNewUserEvent) {
        // we know that as soon as emulator starts processing user events
        // it processes them until there are none. So we can notify it only
        // if this event is the first one
        uiAgent->userEvents->onNewUserEvent();
    }

    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_releaseBitmap(SkinSurface *s, QSemaphore *semaphore)
{
    if (backing_surface == s) {
        backing_surface = NULL;
    }
    delete s->bitmap;
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_requestClose(QSemaphore *semaphore)
{
    mContainer.close();
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_requestUpdate(const QRect *rect, QSemaphore *semaphore)
{
    QRect r(rect->x()  *backing_surface->w / backing_surface->original_w,
            rect->y()  *backing_surface->h / backing_surface->original_h,
            rect->width()  *backing_surface->w / backing_surface->original_w,
            rect->height()  *backing_surface->h / backing_surface->original_h);
    update(r);
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_setWindowPos(int x, int y, QSemaphore *semaphore)
{
    mContainer.move(x, y);
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_setWindowIcon(const unsigned char *data, int size, QSemaphore *semaphore)
{
    QPixmap image;
    image.loadFromData(data, size);
    QIcon icon(image);
    QApplication::setWindowIcon(icon);
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_setWindowTitle(const QString *title, QSemaphore *semaphore)
{
    mContainer.setWindowTitle(*title);
    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_showWindow(SkinSurface* surface, const QRect* rect, int is_fullscreen, QSemaphore *semaphore)
{
    // Zooming forces the scroll bar to be visible for sizing purpose, so reset them
    // back to the default policy.
    mContainer.setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mContainer.setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    backing_surface = surface;
    if (is_fullscreen) {
        showFullScreen();
    } else {
        showNormal();
        setFixedSize(rect->size());

        // If this was the result of a zoom, don't change the overall window size, and adjust the
        // scroll bars to reflect the desired focus point.
        if (mNextIsZoom) {
            recenterFocusPoint();
        } else {
            mContainer.resize(rect->size());
        }
        mNextIsZoom = false;
    }
    show();

    // If the user isn't using an x86 AVD, make sure its because their machine doesn't support
    // CPU acceleration. If it does, recommend switching to an x86 AVD.
    // This cannot be done on the construction of the window since the Qt UI thread has not been
    // properly initialized yet.
    if (mFirstShowEvent) {
        showAvdArchWarning();
    }
    mFirstShowEvent = false;

    if (semaphore != NULL) semaphore->release();
}

void EmulatorQtWindow::slot_screenChanged(QScreen*)
{
    queueEvent(createSkinEvent(kEventScreenChanged));
}

void EmulatorQtWindow::slot_horizontalScrollChanged(int value)
{
    simulateScrollBarChanged(value, mContainer.verticalScrollBar()->value());
}

void EmulatorQtWindow::slot_verticalScrollChanged(int value)
{
    simulateScrollBarChanged(mContainer.horizontalScrollBar()->value(), value);
}

void EmulatorQtWindow::slot_scrollRangeChanged(int min, int max)
{
    simulateScrollBarChanged(mContainer.horizontalScrollBar()->value(),
                             mContainer.verticalScrollBar()->value());
}

void EmulatorQtWindow::slot_animationFinished()
{
    mOverlay.hideForFlash();
}

void EmulatorQtWindow::slot_animationValueChanged(const QVariant &value)
{
    mOverlay.setFlashValue(value.toDouble() / mFlashAnimation.startValue().toDouble());
    mOverlay.repaint();
}

void EmulatorQtWindow::screenshot()
{
    if (mScreencapProcess.state() != QProcess::NotRunning) {
        // Modal dialogs should prevent this
        return;
    }

    QStringList args;
    QString command = tool_window->getAdbFullPath(&args);
    if (command.isNull()) {
        return;
    }

    // Add the arguments
    args << "shell";                // Running a shell command
    args << "screencap";            // Take a screen capture
    args << "-p";                   // Print it to a file
    args << REMOTE_SCREENSHOT_FILE; // The temporary screenshot file

    // Display the flash animation immediately as feedback - if it fails, an error dialog will
    // indicate as such.
    mOverlay.showAsFlash();
    mFlashAnimation.start();

    mScreencapProcess.start(command, args);
    // TODO(pprabhu): It is a bad idea to call |waitForStarted| from the GUI
    // thread, because it can freeze the UI till timeout.
    if (!mScreencapProcess.waitForStarted(5000)) {
        slot_showProcessErrorDialog(mScreencapProcess.error());
    }
}


void EmulatorQtWindow::slot_screencapFinished(int exitStatus)
{
    if (exitStatus) {
        QByteArray er = mScreencapProcess.readAllStandardError();
        er = er.replace('\n', "<br/>");
        QString msg = tr("The screenshot could not be captured. Output:<br/><br/>") + QString(er);
        showErrorDialog(msg, tr("Screenshot"));
    } else {
        // Pull the image from its remote location to the desired location
        QStringList args;
        QString command = tool_window->getAdbFullPath(&args);
        if (command.isNull()) {
            return;
        }

        // Add the arguments
        args << "pull";                 // Pulling a file
        args << REMOTE_SCREENSHOT_FILE; // Which file to pull

        QString fileName = tool_window->getScreenshotSaveFile();
        if (fileName.isEmpty()) {
            showErrorDialog(tr("The screenshot save location is invalid.<br/>"
                               "Check the settings page and ensure the directory "
                               "exists and is writeable."),
                            tr("Screenshot"));
            return;
        }

        args << fileName;

        // Use a different process to avoid infinite looping when pulling the
        // file.
        mScreencapPullProcess.start(command, args);
        // TODO(pprabhu): It is a bad idea to call |waitForStarted| from the GUI
        // thread, because it can freeze the UI till timeout.
        if (!mScreencapPullProcess.waitForStarted(5000)) {
            slot_showProcessErrorDialog(mScreencapPullProcess.error());
        }
    }
}

void EmulatorQtWindow::slot_screencapPullFinished(int exitStatus)
{
    if (exitStatus) {
        QByteArray er = mScreencapPullProcess.readAllStandardError();
        er = er.replace('\n', "<br/>");
        QString msg = tr("The screenshot could not be loaded from the device. Output:<br/><br/>")
                        + QString(er);
        showErrorDialog(msg, tr("Screenshot"));
    }
}

void EmulatorQtWindow::slot_resizeDone()
{
    // This function should never actually be called on Linux, since the timer is never
    // started on those systems.
#if defined(__APPLE__) || defined(_WIN32)

    // A hacky way of determining if the user is still holding down for a resize. This queries the
    // global event state to see if any mouse buttons are held down. If there are, then the user must
    // not be done resizing yet.
    if (numHeldMouseButtons() == 0) {
        doResize(mContainer.size());
    } else {
        mResizeTimer.start(500);
    }
#endif
}

// Convert a Qt::Key_XXX code into the corresponding Linux keycode value.
// On failure, return -1.
static int convertKeyCode(int sym)
{
#define KK(x,y)  { Qt::Key_ ## x, KEY_ ## y }
#define K1(x)    KK(x,x)
    static const struct {
        int qt_sym;
        int keycode;
    } kConvert[] = {
        KK(Left, LEFT),
        KK(Right, RIGHT),
        KK(Up, UP),
        KK(Down, DOWN),
        K1(0),
        K1(1),
        K1(2),
        K1(3),
        K1(4),
        K1(5),
        K1(6),
        K1(7),
        K1(8),
        K1(9),
        K1(F1),
        K1(F2),
        K1(F3),
        K1(F4),
        K1(F5),
        K1(F6),
        K1(F7),
        K1(F8),
        K1(F9),
        K1(F10),
        K1(F11),
        K1(F12),
        K1(A),
        K1(B),
        K1(C),
        K1(D),
        K1(E),
        K1(F),
        K1(G),
        K1(H),
        K1(I),
        K1(J),
        K1(K),
        K1(L),
        K1(M),
        K1(N),
        K1(O),
        K1(P),
        K1(Q),
        K1(R),
        K1(S),
        K1(T),
        K1(U),
        K1(V),
        K1(W),
        K1(X),
        K1(Y),
        K1(Z),
        KK(Minus, MINUS),
        KK(Equal, EQUAL),
        KK(Backspace, BACKSPACE),
        KK(Home, HOME),
        KK(Escape, ESC),
        KK(Comma, COMMA),
        KK(Period,DOT),
        KK(Space, SPACE),
        KK(Slash, SLASH),
        KK(Return,ENTER),
        KK(Tab, TAB),
        KK(BracketLeft, LEFTBRACE),
        KK(BracketRight, RIGHTBRACE),
        KK(Backslash, BACKSLASH),
        KK(Semicolon, SEMICOLON),
        KK(Apostrophe, APOSTROPHE),
    };
    const size_t kConvertSize = sizeof(kConvert) / sizeof(kConvert[0]);
    size_t nn;
    for (nn = 0; nn < kConvertSize; ++nn) {
        if (sym == kConvert[nn].qt_sym) {
            return kConvert[nn].keycode;
        }
    }
    return -1;
}

SkinEvent *EmulatorQtWindow::createSkinEvent(SkinEventType type)
{
    SkinEvent *skin_event = new SkinEvent();
    skin_event->type = type;
    return skin_event;
}

void EmulatorQtWindow::doResize(const QSize &size)
{
    if (backing_surface) {
        QSize newSize(backing_surface->original_w, backing_surface->original_h);
        newSize.scale(size, Qt::KeepAspectRatio);

        QRect screenDimensions;
        slot_getScreenDimensions(&screenDimensions);

        // Make sure the new size is always a little bit smaller than the screen to prevent
        // keyboard shortcut scaling from making a window too large for the screen, which can
        // result in the showing of the scroll bars.
        if (newSize.width() > screenDimensions.width() ||
            newSize.height() > screenDimensions.height()) {
            newSize.scale(screenDimensions.size(), Qt::KeepAspectRatio);
        }

        double widthScale = (double) newSize.width() / (double) backing_surface->original_w;
        double heightScale = (double) newSize.height() / (double) backing_surface->original_h;

        simulateSetScale(MIN(widthScale, heightScale));
    }
}

SkinMouseButtonType EmulatorQtWindow::getSkinMouseButton(QMouseEvent *event) const
{
    return (event->button() == Qt::RightButton) ? kMouseButtonRight : kMouseButtonLeft;
}

void EmulatorQtWindow::handleMouseEvent(SkinEventType type, SkinMouseButtonType button, const QPoint &pos)
{
    SkinEvent *skin_event = createSkinEvent(type);
    skin_event->u.mouse.button = button;
    skin_event->u.mouse.x = pos.x();
    skin_event->u.mouse.y = pos.y();

    skin_event->u.mouse.xrel = pos.x() - mPrevMousePosition.x();
    skin_event->u.mouse.yrel = pos.y() - mPrevMousePosition.y();
    mPrevMousePosition = pos;

    queueEvent(skin_event);
}

void EmulatorQtWindow::forwardKeyEventToEmulator(SkinEventType type, QKeyEvent* event) {
    SkinEvent* skin_event = createSkinEvent(type);
    SkinEventKeyData& keyData = skin_event->u.key;
    keyData.keycode = convertKeyCode(event->key());

    Qt::KeyboardModifiers modifiers = event->modifiers();
    if (modifiers & Qt::ShiftModifier) keyData.mod |= kKeyModLShift;
    if (modifiers & Qt::ControlModifier) keyData.mod |= kKeyModLCtrl;
    if (modifiers & Qt::AltModifier) keyData.mod |= kKeyModLAlt;

    queueEvent(skin_event);
}

void EmulatorQtWindow::handleKeyEvent(SkinEventType type, QKeyEvent *event)
{
    bool must_ungrab = event->key() == Qt::Key_Alt &&
                       event->modifiers() == (Qt::ControlModifier + Qt::AltModifier);
    if (mGrabKeyboardInput && must_ungrab) {
        mGrabKeyboardInput = false;
    }

    QSettings settings;
    bool grab =
        mGrabKeyboardInput &&
        settings.value(Ui::Settings::ALLOW_KEYBOARD_GRAB, false).toBool() &&
        mouseInside();

    if (!grab && mInZoomMode) {
        if (event->key() == Qt::Key_Control) {
            if (type == kEventKeyDown) {
                mOverlay.hide();
            } else if (type == kEventKeyUp) {
                mOverlay.showForZoom();
            }
        }
    }

    if (!grab &&
         event->key() == Qt::Key_Alt &&
         event->modifiers() == Qt::AltModifier) {
        if (type == kEventKeyDown) {
            mOverlay.showForMultitouch();
        } else if (type == kEventKeyUp) {
            mOverlay.hide();
        }
    }

    if (grab ||
         !tool_window->handleQtKeyEvent(event)) {
        forwardKeyEventToEmulator(type, event);
    }
}

void EmulatorQtWindow::simulateKeyPress(int keyCode, int modifiers)
{
    SkinEvent *event = createSkinEvent(kEventKeyDown);
    event->u.key.keycode = keyCode;
    event->u.key.mod = modifiers;
    slot_queueEvent(event);

    event = createSkinEvent(kEventKeyUp);
    event->u.key.keycode = keyCode;
    event->u.key.mod = modifiers;
    slot_queueEvent(event);
}

void EmulatorQtWindow::simulateScrollBarChanged(int x, int y)
{
    SkinEvent *event = createSkinEvent(kEventScrollBarChanged);
    event->u.scroll.x = x;
    event->u.scroll.xmax = mContainer.horizontalScrollBar()->maximum();
    event->u.scroll.y = y;
    event->u.scroll.ymax = mContainer.verticalScrollBar()->maximum();
    slot_queueEvent(event);
}

void EmulatorQtWindow::simulateSetScale(double scale)
{
    // Avoid zoom and scale events clobbering each other if the user rapidly changes zoom levels
    if (mNextIsZoom) {
        return;
    }

    // Reset our local copy of zoom factor
    mZoomFactor = 1.0;

    SkinEvent *event = createSkinEvent(kEventSetScale);
    event->u.window.scale = scale;
    slot_queueEvent(event);
}

void EmulatorQtWindow::simulateSetZoom(double zoom)
{
    // Avoid zoom and scale events clobbering each other if the user rapidly changes zoom levels
    if (mNextIsZoom || mZoomFactor == zoom) {
        return;
    }

    // Qt Widgets do not get properly sized unless they appear at least once. The scroll bars
    // *must* be properly sized in order for zoom to create the correct GLES subwindow, so this
    // ensures they will be. This is reset as soon as the window is shown.
    mContainer.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    mContainer.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    mNextIsZoom = true;
    mZoomFactor = zoom;

    QSize viewport = mContainer.viewportSize();

    SkinEvent *event = createSkinEvent(kEventSetZoom);
    event->u.window.x = viewport.width();
    event->u.window.y = viewport.height();

    QScrollBar *horizontal = mContainer.horizontalScrollBar();
    event->u.window.scroll_h = horizontal->isVisible() ? horizontal->height() : 0;
    event->u.window.scale = zoom;
    slot_queueEvent(event);
}

void EmulatorQtWindow::simulateWindowMoved(const QPoint &pos)
{
    SkinEvent *event = createSkinEvent(kEventWindowMoved);
    event->u.window.x = pos.x();
    event->u.window.y = pos.y();
    slot_queueEvent(event);

    mOverlay.move(mContainer.mapToGlobal(QPoint()));
}

void EmulatorQtWindow::simulateZoomedWindowResized(const QSize &size)
{
    SkinEvent *event = createSkinEvent(kEventZoomedWindowResized);
    QScrollBar *horizontal = mContainer.horizontalScrollBar();
    event->u.scroll.x = horizontal->value();
    event->u.scroll.y = mContainer.verticalScrollBar()->value();
    event->u.scroll.xmax = size.width();
    event->u.scroll.ymax = size.height();
    event->u.scroll.scroll_h = horizontal->isVisible() ? horizontal->height() : 0;
    slot_queueEvent(event);

    mOverlay.resize(size);
}


void EmulatorQtWindow::slot_runOnUiThread(SkinGenericFunction* f, void* data, QSemaphore* semaphore) {
    (*f)(data);
    if (semaphore) semaphore->release();
}

bool EmulatorQtWindow::isInZoomMode()
{
    return mInZoomMode;
}

void EmulatorQtWindow::toggleZoomMode()
{
    mInZoomMode = !mInZoomMode;

    // Exiting zoom mode snaps back to aspect ratio
    if (!mInZoomMode) {
        mContainer.setCursor(QCursor(Qt::ArrowCursor));
        doResize(mContainer.size());
        mOverlay.hide();
    } else {
        mOverlay.showForZoom();
    }
}

void EmulatorQtWindow::recenterFocusPoint()
{
    mContainer.horizontalScrollBar()->setValue(mFocus.x() * width() - mViewportFocus.x());
    mContainer.verticalScrollBar()->setValue(mFocus.y() * height() - mViewportFocus.y());

    mFocus = QPointF();
    mViewportFocus = QPoint();
}

void EmulatorQtWindow::saveZoomPoints(const QPoint &focus, const QPoint &viewportFocus)
{
    // The underlying frame will change sizes, so get what "percentage" of the frame was
    // clicked, where (0,0) is the top-left corner and (1,1) is the bottom right corner.
    mFocus = QPointF((float) focus.x() / this->width(),
                     (float) focus.y() / this->height());

    // Save to re-align the container with the underlying frame.
    mViewportFocus = viewportFocus;
}

void EmulatorQtWindow::scaleDown()
{
    doResize(mContainer.size() / 1.1);
}

void EmulatorQtWindow::scaleUp()
{
    doResize(mContainer.size() * 1.1);
}

void EmulatorQtWindow::zoomIn()
{
    zoomIn(QPoint(width() / 2, height() / 2), QPoint(mContainer.width() / 2, mContainer.height() / 2));
}

void EmulatorQtWindow::zoomIn(const QPoint &focus, const QPoint &viewportFocus)
{
    saveZoomPoints(focus, viewportFocus);

    // The below scale = x creates a skin equivalent to calling "window scale x" through the
    // emulator console. At scale = 1, the device should be at a 1:1 pixel mapping with the
    // monitor. We allow going to twice this size.
    double scale = ((double) size().width() / (double) backing_surface->original_w);
    double maxZoom = mZoomFactor * 2.0 / scale;

    if (scale < 2) {
        simulateSetZoom(MIN(mZoomFactor + .25, maxZoom));
    }
}

void EmulatorQtWindow::zoomOut()
{
    zoomOut(QPoint(width() / 2, height() / 2), QPoint(mContainer.width() / 2, mContainer.height() / 2));
}

void EmulatorQtWindow::zoomOut(const QPoint &focus, const QPoint &viewportFocus)
{
    saveZoomPoints(focus, viewportFocus);
    if (mZoomFactor > 1) {
        simulateSetZoom(MAX(mZoomFactor - .25, 1));
    }
}

void EmulatorQtWindow::zoomReset()
{
    simulateSetZoom(1);
}

void EmulatorQtWindow::zoomTo(const QPoint &focus, const QSize &rectSize)
{
    saveZoomPoints(focus, QPoint(mContainer.width() / 2, mContainer.height() / 2));

    // The below scale = x creates a skin equivalent to calling "window scale x" through the
    // emulator console. At scale = 1, the device should be at a 1:1 pixel mapping with the
    // monitor. We allow going to twice this size.
    double scale = ((double) size().width() / (double) backing_surface->original_w);

    // Calculate the "ideal" zoom factor, which would perfectly frame this rectangle, and the
    // "maximum" zoom factor, which makes scale = 1, and pick the smaller one.
    // Adding 20 accounts for the scroll bars potentially cutting off parts of the selection
    double maxZoom = mZoomFactor * 2.0 / scale;
    double idealWidthZoom = mZoomFactor * (double) mContainer.width() / (double) (rectSize.width() + 20);
    double idealHeightZoom = mZoomFactor * (double) mContainer.height() / (double) (rectSize.height() + 20);

    simulateSetZoom(MIN(MIN(idealWidthZoom, idealHeightZoom), maxZoom));
}

void EmulatorQtWindow::panHorizontal(bool left)
{
    QScrollBar *bar = mContainer.horizontalScrollBar();
    if (left) {
        bar->setValue(bar->value() - bar->singleStep());
    } else {
        bar->setValue(bar->value() + bar->singleStep());
    }
}

void EmulatorQtWindow::panVertical(bool up)
{
    QScrollBar *bar = mContainer.verticalScrollBar();
    if (up) {
        bar->setValue(bar->value() - bar->singleStep());
    } else {
        bar->setValue(bar->value() + bar->singleStep());
    }
}

bool EmulatorQtWindow::mouseInside() {
    QPoint widget_cursor_coords = mapFromGlobal(QCursor::pos());
    return widget_cursor_coords.x() >= 0 &&
           widget_cursor_coords.x() < width() &&
           widget_cursor_coords.y() >= 0 &&
           widget_cursor_coords.y() < height();
}

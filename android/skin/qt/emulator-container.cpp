// Copyright (C) 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/skin/qt/emulator-container.h"

#include "android/skin/qt/emulator-qt-window.h"
#include "android/skin/qt/tool-window.h"

#include <QtCore>
#include <QApplication>
#include <QObject>
#include <QScrollBar>
#include <QStyle>
#include <QStyleFactory>

#if defined(__APPLE__)
#include "android/skin/qt/mac-native-window.h"
#endif

#if defined(_WIN32)
#include "android/skin/qt/windows-native-window.h"
#endif

EmulatorContainer::EmulatorContainer(EmulatorQtWindow* window)
    : QScrollArea(), mEmulatorWindow(window) {
    setFrameShape(QFrame::NoFrame);
    setWidget(window);

    // The following hints prevent the minimize/maximize/close buttons from
    // appearing.
    setWindowFlags(Qt::WindowTitleHint | Qt::CustomizeWindowHint | Qt::Window);

#ifdef __APPLE__
    // Digging into the Qt source code reveals that if the above flags are set
    // on OSX, the created window will be given a style mask that removes the
    // resize handles from the window. The hint below is the specific
    // customization flag that ensures the window will have resize handles.
    // So, we add the button for now, then immediately disable it when the
    // window is first shown.
    setWindowFlags(this->windowFlags() | Qt::WindowMaximizeButtonHint);

    // On OS X the native scrollbars disappear when not in use which
    // makes the zoomed-in emulator window look unscrollable. Also, due
    // to the semi-transparent nature of the scrollbar, it will
    // interfere with the main GL window, causing all kinds of ugly
    // effects.
    QStyle* style = QStyleFactory::create("Fusion");
    if (style) {
        this->verticalScrollBar()->setStyle(style);
        this->horizontalScrollBar()->setStyle(style);
        QObject::connect(this, &QObject::destroyed, [style] { delete style; });
    }
#endif  // __APPLE__

    mResizeTimer.setSingleShot(true);
    QObject::connect(&mResizeTimer, SIGNAL(timeout()), this,
                     SLOT(slot_resizeDone()));
}

EmulatorContainer::~EmulatorContainer() {
    // This object is owned directly by |window|.  Avoid circular
    // destructor calls by explicitly unsetting the widget.
    takeWidget();
}

bool EmulatorContainer::event(QEvent* e) {
    // Ignore MetaCall and UpdateRequest events, and don't snap in zoom mode.
    if (mEmulatorWindow->isInZoomMode() || e->type() == QEvent::MetaCall ||
        e->type() == QEvent::UpdateRequest) {
        return QScrollArea::event(e);
    }

    // Add to the event buffer, but keep it a reasonable size - a few events,
    // such as repaint,
    // can occur in between resizes but before the release happens
    mEventBuffer.push_back(e->type());
    if (mEventBuffer.size() > 8) {
        mEventBuffer.removeFirst();
    }

    // Scan to see if a resize event happened recently
    bool foundResize = false;
    int i = 0;
    for (; i < mEventBuffer.size(); i++) {
        if (mEventBuffer[i] == QEvent::Resize) {
            foundResize = true;
            i++;
            break;
        }
    }

    // Determining resize-over is OS specific
    // Do so by scanning the remainder of the event buffer for specific
    // combinations
    if (foundResize) {
#ifdef _WIN32

        for (; i < mEventBuffer.size() - 1; i++) {
            if (mEventBuffer[i] == QEvent::NonClientAreaMouseButtonRelease) {
                mEventBuffer.clear();
                mEmulatorWindow->doResize(this->size());

                // Kill the resize timer to avoid double resizes.
                stopResizeTimer();
                break;
            }
        }

#elif __linux__

        for (; i < mEventBuffer.size() - 3; i++) {
            if (mEventBuffer[i] == QEvent::WindowActivate &&
                mEventBuffer[i + 1] == QEvent::ActivationChange &&
                mEventBuffer[i + 2] == QEvent::FocusIn &&
                mEventBuffer[i + 3] == QEvent::InputMethodQuery) {
                mEventBuffer.clear();
                mEmulatorWindow->doResize(this->size());
                break;
            }
        }

#elif __APPLE__

        if (e->type() == QEvent::NonClientAreaMouseMove ||
            e->type() == QEvent::Enter || e->type() == QEvent::Leave) {
            mEventBuffer.clear();
            mEmulatorWindow->doResize(this->size());

            // Kill the resize timer to avoid double resizes.
            stopResizeTimer();
        }

#endif
    }

    return QScrollArea::event(e);
}

void EmulatorContainer::changeEvent(QEvent* event) {
    // Strictly preventing the maximizing (called "zooming" on OS X) of a
    // window is hard - it changes by host, and even by window manager on
    // Linux. Therefore, we counteract it by seeing if the window ever
    // enters a maximized state, and if it does, immediately undoing that
    // maximization.
    //
    // Note that we *do not* call event->ignore(). Maximizing happens in the
    // OS-level window, not Qt's representation of the window. This event
    // simply notifies the Qt representation (and us) that the OS-level window
    // has changed to a maximized state. We do not want to ignore this state
    // change, we just want to counteract the effects it had.
    if (event->type() == QEvent::WindowStateChange) {
        if (windowState() & Qt::WindowMaximized) {
            showNormal();
        }
    }
}

void EmulatorContainer::closeEvent(QCloseEvent* event) {
    mEmulatorWindow->closeEvent(event);
}

void EmulatorContainer::focusInEvent(QFocusEvent* event) {
    mEmulatorWindow->toolWindow()->raise();
}

void EmulatorContainer::keyPressEvent(QKeyEvent* event) {
    mEmulatorWindow->keyPressEvent(event);
}

void EmulatorContainer::keyReleaseEvent(QKeyEvent* event) {
    mEmulatorWindow->keyReleaseEvent(event);
}

void EmulatorContainer::moveEvent(QMoveEvent* event) {
    QScrollArea::moveEvent(event);
    mEmulatorWindow->simulateWindowMoved(event->pos());
    mEmulatorWindow->toolWindow()->dockMainWindow();
}

void EmulatorContainer::resizeEvent(QResizeEvent* event) {
    QScrollArea::resizeEvent(event);
    mEmulatorWindow->toolWindow()->dockMainWindow();
    mEmulatorWindow->simulateZoomedWindowResized(this->viewportSize());

// To solve some resizing edge cases on OSX/Windows, start a short timer that
// will attempt to trigger a resize in case the user's mouse has not entered
// the window again.
#if defined(__APPLE__) || defined(_WIN32)
    mResizeTimer.start(500);
#endif
}

void EmulatorContainer::showEvent(QShowEvent* event) {
// Disable to maximize button on OSX. See the comment in the constructor for an
// explanation of why this is necessary.
#ifdef __APPLE__
    WId wid = effectiveWinId();
    wid = (WId)getNSWindow((void*)wid);
    nsWindowHideWindowButtons((void*)wid);
#endif // __APPLE__

// As seen below in showMinimized(), we need to remove the minimize button on
// Linux when the window is re-shown. We know this show event is from being
// un-minimized because the minimized button flag is present.
#ifdef __linux__
    if (!(windowState() & Qt::WindowMinimized)) {
        Qt::WindowFlags flags = windowFlags();
        if (flags & Qt::WindowMinimizeButtonHint) {
            setWindowFlags(flags & ~Qt::WindowMinimizeButtonHint);

            // Changing window flags requires re-showing this window to ensure
            // the flags are appropriately changed.
            showNormal();

            // The subwindow won't redraw until the guest screen changes, which
            // may not happen for a minute (when the clock changes), so force a
            // redraw after re-showing the window.
            SkinEvent* event = new SkinEvent();
            event->type = kEventForceRedraw;
            mEmulatorWindow->queueSkinEvent(event);
        }
    }
#endif // __linux__

    mEmulatorWindow->toolWindow()->show();
}

void EmulatorContainer::showMinimized() {
// Some Linux window managers (specifically, Compiz, which is the default
// Ubuntu window manager) will not allow minimizing unless the minimize
// button is actually there! So, we re-add the button, minimize the window,
// and then remove the button when it gets reshown.
#ifdef __linux__
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint);
#endif // __linux__
    QScrollArea::showMinimized();
}

void EmulatorContainer::stopResizeTimer() {
    mResizeTimer.stop();
}

QSize EmulatorContainer::viewportSize() const {
    QSize output = this->size();

    QScrollBar* vertical = this->verticalScrollBar();
    output.setWidth(output.width() -
                    (vertical->isVisible() ? vertical->width() : 0));

    QScrollBar* horizontal = this->horizontalScrollBar();
    output.setHeight(output.height() -
                     (horizontal->isVisible() ? horizontal->height() : 0));

    return output;
}

void EmulatorContainer::slot_resizeDone() {
// This function should never actually be called on Linux, since the timer is
// never started on those systems.
#if defined(__APPLE__) || defined(_WIN32)

    if (mEmulatorWindow->isInZoomMode()) {
        return;
    }

    // A hacky way of determining if the user is still holding down for a
    // resize.
    // This queries the global event state to see if any mouse buttons are held
    // down.
    // If there are, then the user must not be done resizing yet.
    if (numHeldMouseButtons() == 0) {
        mEmulatorWindow->doResize(this->size());
    } else {
        mResizeTimer.start(500);
    }
#endif
}

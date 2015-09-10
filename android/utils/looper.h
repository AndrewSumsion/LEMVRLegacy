/* Copyright (C) 2010 The Android Open Source Project
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
#ifndef ANDROID_UTILS_LOOPER_H
#define ANDROID_UTILS_LOOPER_H

#include "android/utils/compiler.h"
#include "android/utils/system.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

ANDROID_BEGIN_HEADER

/* stdint.h will not define necessary macros in some c++ compiler
 * implementations without this macro definition.
 */
#if defined(__cplusplus) && !defined(__STDC_LIMIT_MACROS)
#error "This header requires you to define __STDC_LIMIT_MACROS."
#endif

/**********************************************************************
 **********************************************************************
 *****
 *****  T I M E   R E P R E S E N T A T I O N
 *****
 **********************************************************************
 **********************************************************************/

/* A Duration represents a duration in milliseconds */
typedef int64_t   Duration;

/* A special Duration value used to mean "infinite" */
#define  DURATION_INFINITE       ((Duration)INT64_MAX)

/**********************************************************************
 **********************************************************************
 *****
 *****  E V E N T   L O O P   O B J E C T S
 *****
 **********************************************************************
 **********************************************************************/


/* A Looper is an abstraction for an event loop, which can
 * be implemented in different ways. For example, the UI program may
 * want to implement a custom event loop on top of the SDL event queue,
 * while the QEMU core would implement it on top of QEMU's internal
 * main loop which works differently.
 *
 * Once you have a Looper pointer, you can register "watchers" that
 * will trigger callbacks whenever certain events occur. Supported event
 * types are:
 *
 *   - timer expiration
 *   - i/o file descriptor input/output
 *
 * See the relevant documentation for these below.
 *
 * Once you have registered one or more watchers, you can call looper_run()
 * which will run the event loop until looper_forceQuit() is called from a
 * callback, or no more watchers are registered.
 *
 * You can register/unregister watchers from a callback, or call various
 * Looper methods from them (e.g. looper_now(), looper_forceQuit(), etc..)
 *
 * You can create a new Looper by calling looper_newGeneric(). This provides
 * a default implementation that can be used in all threads.
 *
 * For the QEMU core, you can grab a Looper pointer by calling
 * looper_newCore() instead. Its implementation relies on top of
 * the QEMU event loop instead.
 */
typedef struct Looper    Looper;

/* Create a new generic looper that can be used in any context / thread. */
Looper*  looper_newGeneric(void);

typedef struct LoopTimer LoopTimer;
typedef void (*LoopTimerFunc)(void* opaque);

typedef struct LoopIo    LoopIo;
typedef void (*LoopIoFunc)(void* opaque, int fd, unsigned events);

/**********************************************************************
 **********************************************************************
 *****
 *****  T I M E R S
 *****
 **********************************************************************
 **********************************************************************/

/* Initialize a LoopTimer with a callback and an 'opaque' value.
 * Each timer belongs to only one looper object.
 */
LoopTimer* loopTimer_new(Looper*        looper,
                         LoopTimerFunc  callback,
                         void*          opaque);

/* Finalize a LoopTimer */
void loopTimer_free(LoopTimer* timer);

/* Start a timer, i.e. arm it to expire in 'timeout_ms' milliseconds,
 * unless loopTimer_stop() is called before that, or the timer is
 * reprogrammed with another loopTimer_startXXX() call.
 */
void loopTimer_startRelative(LoopTimer* timer, Duration timeout_ms);

/* A variant of loopTimer_startRelative that fires on a given deadline
 * in milliseconds instead. If the deadline already passed, the timer is
 * automatically appended to the list of pending event watchers and will
 * fire as soon as possible. Note that this can cause infinite loops
 * in your code if you're not careful.
 */
void loopTimer_startAbsolute(LoopTimer* timer, Duration deadline_ms);

/* Stop a given timer */
void loopTimer_stop(LoopTimer* timer);

/* Returns true iff the timer is active / started */
int loopTimer_isActive(LoopTimer* timer);

/**********************************************************************
 **********************************************************************
 *****
 *****  F I L E   D E S C R I P T O R S
 *****
 **********************************************************************
 **********************************************************************/

/* Bitmasks about i/o events. Note that errors (e.g. network disconnections)
 * are mapped to both read and write events. The idea is that a read() or
 * write() will return 0 or even -1 on non-blocking file descriptors in this
 * case.
 *
 * You can receive several events at the same time on a single LoopIo
 *
 * Socket connect()s are mapped to LOOP_IO_WRITE events.
 * Socket accept()s are mapped to LOOP_IO_READ events.
 */
enum {
    LOOP_IO_READ  = (1 << 0),
    LOOP_IO_WRITE = (1 << 1),
};

LoopIo* loopIo_new(Looper* looper,
                   int fd,
                   LoopIoFunc callback,
                   void* opaque);

/* Note: This does not close the file descriptor! */
void loopIo_free(LoopIo* io);

int loopIo_fd(LoopIo* io);

/* The following functions are used to indicate whether you want the callback
 * to be fired when there is data to be read or when the file is ready to
 * be written to. */
void loopIo_wantRead(LoopIo* io);
void loopIo_wantWrite(LoopIo* io);
void loopIo_dontWantRead(LoopIo* io);
void loopIo_dontWantWrite(LoopIo* io);

unsigned loopIo_poll(LoopIo* io);

/**********************************************************************
 **********************************************************************
 *****
 *****  L O O P E R
 *****
 **********************************************************************
 **********************************************************************/

/* Return the current looper time in milliseconds. This can be used to
 * compute deadlines for looper_runWithDeadline(). */
Duration looper_now(Looper* looper);

/* Run the event loop, until looper_forceQuit() is called, or there is no
 * more registered watchers for events/timers in the looper, or a certain
 * deadline expires.
 *
 * |deadline_ms| is a deadline in milliseconds.
 *
 * The value returned indicates the reason:
 *    0           -> normal exit through looper_forceQuit()
 *    EWOULDBLOCK -> there are not more watchers registered (the looper
 *                   would loop infinitely)
 *    ETIMEDOUT   -> deadline expired.
 */
int looper_runWithDeadline(Looper* looper, Duration deadline_ms);

/* Run the event loop, until looper_forceQuit() is called, or there is no
 * more registered watchers for events/timers in the looper.
 */
AINLINED void looper_run(Looper* looper) {
    (void) looper_runWithDeadline(looper, DURATION_INFINITE);
}

/* A variant of looper_run() that allows to run the event loop only
 * until a certain timeout in milliseconds has passed.
 *
 * Returns the reason why the looper stopped:
 *    0           -> normal exit through looper_forceQuit()
 *    EWOULDBLOCK -> there are not more watchers registered (the looper
 *                   would loop infinitely)
 *    ETIMEDOUT   -> timeout reached
 *
 */
AINLINED int looper_runWithTimeout(Looper* looper, Duration timeout_ms) {
    if (timeout_ms != DURATION_INFINITE)
        timeout_ms += looper_now(looper);

    return looper_runWithDeadline(looper, timeout_ms);
}

/* Call this function from within the event loop to force it to quit
 * as soon as possible. looper_run() / _runWithTimeout() / _runWithDeadline()
 * will then return 0.
 */
void looper_forceQuit(Looper* looper);

/* Destroy a given looper object. Only works for those created
 * with looper_new(). Cannot be called within looper_run()!!
 *
 * NOTE: This assumes that the user has destroyed all its
 *        timers and ios properly
 */
void looper_free(Looper* looper);

/* */

ANDROID_END_HEADER

#endif /* ANDROID_UTILS_LOOPER_H */

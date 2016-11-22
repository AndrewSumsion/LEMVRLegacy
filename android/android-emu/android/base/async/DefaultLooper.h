// Copyright 2014 The Android Open Source Project
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

#include "android/base/async/Looper.h"
#include "android/base/containers/ScopedPointerSet.h"
#include "android/base/containers/TailQueueList.h"
#include "android/base/sockets/SocketWaiter.h"

#include <memory>

namespace android {
namespace base {

// Default looper implementation based on select(). To make sure all timers and
// FD watches execute, run its runWithDeadlineMs() explicitly.
class DefaultLooper : public Looper {
public:
    DefaultLooper();

    ~DefaultLooper() override;

    Duration nowMs(ClockType clockType = ClockType::kHost) override;

    DurationNs nowNs(ClockType clockType = ClockType::kHost) override;

    void forceQuit() override;

    //
    //  F D   W A T C H E S
    //
    class FdWatch : public Looper::FdWatch {
    public:
        FdWatch(DefaultLooper* looper, int fd, Callback callback, void* opaque);

        DefaultLooper* defaultLooper() const;

        ~FdWatch() override;

        void addEvents(unsigned events) override;

        void removeEvents(unsigned events) override;

        unsigned poll() const override;

        // Return true iff this FdWatch is pending execution.
        bool isPending() const;

        // Add this FdWatch to a pending queue.
        void setPending(unsigned events);

        // Remove this FdWatch from a pending queue.
        void clearPending();

        // Fire the watch, i.e. invoke the callback with the right
        // parameters.
        void fire();

        TAIL_QUEUE_LIST_TRAITS(Traits, FdWatch, mPendingLink);

    private:
        unsigned mWantedEvents;
        unsigned mLastEvents;
        bool mPending;
        TailQueueLink<FdWatch> mPendingLink;
    };

    void addFdWatch(FdWatch* watch);

    void delFdWatch(FdWatch* watch);

    void addPendingFdWatch(FdWatch* watch);

    void delPendingFdWatch(FdWatch* watch);

    void updateFdWatch(int fd, unsigned wantedEvents);

    Looper::FdWatch* createFdWatch(int fd,
                                   Looper::FdWatch::Callback callback,
                                   void* opaque) override;

    //
    //  T I M E R S
    //

    class Timer : public Looper::Timer {
    public:
        Timer(DefaultLooper* looper,
              Callback callback,
              void* opaque,
              ClockType clock);

        DefaultLooper* defaultLooper() const;

        ~Timer() override;

        Timer* next() const;

        Duration deadline() const;

        void startRelative(Duration deadlineMs) override;

        void startAbsolute(Duration deadlineMs) override;

        void stop() override;

        bool isActive() const override;

        void setPending();

        void clearPending();

        void fire();

        void save(Stream* stream) const;

        void load(Stream* stream);

        TAIL_QUEUE_LIST_TRAITS(Traits, Timer, mPendingLink);

    private:
        Duration mDeadline;
        bool mPending;
        TailQueueLink<Timer> mPendingLink;
    };

    void addTimer(Timer* timer);

    void delTimer(Timer* timer);

    void enableTimer(Timer* timer);

    void disableTimer(Timer* timer);

    void addPendingTimer(Timer* timer);

    void delPendingTimer(Timer* timer);

    Looper::Timer* createTimer(Looper::Timer::Callback callback,
                               void* opaque,
                               ClockType clock) override;

    //
    //  M A I N   L O O P
    //
    int runWithDeadlineMs(Duration deadlineMs) override;

    typedef TailQueueList<Timer> TimerList;
    typedef ScopedPointerSet<Timer> TimerSet;

    typedef TailQueueList<FdWatch> FdWatchList;
    typedef ScopedPointerSet<FdWatch> FdWatchSet;

protected:
    bool runOneIterationWithDeadlineMs(Duration deadlineMs);

    std::unique_ptr<SocketWaiter> mWaiter;
    FdWatchSet mFdWatches;          // Set of all fd watches.
    FdWatchList mPendingFdWatches;  // Queue of pending fd watches.

    TimerSet mTimers;          // Set of all timers.
    TimerList mActiveTimers;   // Sorted list of active timers.
    TimerList mPendingTimers;  // Sorted list of pending timers.

    bool mForcedExit = false;
};

}  // namespace base
}  // namespace android

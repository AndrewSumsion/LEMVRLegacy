// Copyright 2017 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/snapshot/MemoryWatch.h"

#include "android/base/ArraySize.h"
#include "android/base/Debug.h"
#include "android/base/EintrWrapper.h"
#include "android/base/files/ScopedFd.h"
#include "android/base/threads/FunctorThread.h"
#include "android/utils/debug.h"

#include <fcntl.h>
#include <linux/userfaultfd.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <cassert>
#include <utility>

// Looks like the current emulator toolchain doesn't have this define.
#ifndef __NR_userfaultfd
#ifdef __i368__
#define __NR_userfaultfd 374
#elif defined(__x86_64__)
#define __NR_userfaultfd 323
#else
#error This target architecture is not supported
#endif
#endif

namespace android {
namespace snapshot {

static bool checkUserfaultFdCaps(int ufd) {
    if (ufd < 0) {
        return false;
    }

    uffdio_api apiStruct = {UFFD_API};
    if (ioctl(ufd, UFFDIO_API, &apiStruct)) {
        dwarning("UFFDIO_API failed: %s", strerror(errno));
        return false;
    }

    uint64_t ioctlMask =
            (__u64)1 << _UFFDIO_REGISTER | (__u64)1 << _UFFDIO_UNREGISTER;
    if ((apiStruct.ioctls & ioctlMask) != ioctlMask) {
        dwarning("Missing userfault features: %llu",
                 (unsigned long long)(~apiStruct.ioctls & ioctlMask));
        return false;
    }

    return true;
}

class MemoryAccessWatch::Impl {
public:
    Impl(MemoryAccessWatch::AccessCallback&& accessCallback,
         MemoryAccessWatch::IdleCallback&& idleCallback)
        : mAccessCallback(std::move(accessCallback)),
          mIdleCallback(std::move(idleCallback)),
          mPagefaultThread([this]() { pagefaultWorker(); }) {
        mUserfaultFd = base::ScopedFd(
                syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK));
        if (!checkUserfaultFdCaps(mUserfaultFd.get())) {
            mUserfaultFd.close();
        }
        mExitFd = base::ScopedFd(eventfd(0, EFD_CLOEXEC));
        assert(mExitFd.get() >= 0);
    }

    ~Impl() { mPagefaultThread.wait(); }

    void* readNextPagefaultAddr() const {
        uffd_msg msg;
        const int ret =
                HANDLE_EINTR(read(mUserfaultFd.get(), &msg, sizeof(msg)));
        if (ret != sizeof(msg)) {
            if (errno == EAGAIN) {
                /* if a wake up happens on the other thread just after
                 * the poll, there is nothing to read. */
                return nullptr;
            }
            if (ret < 0) {
                derror("%s: Failed to read full userfault message: %s",
                       __func__, strerror(errno));
                return nullptr;
            } else {
                derror("%s: Read %d bytes from userfaultfd expected %zd",
                       __func__, ret, sizeof(msg));
                return nullptr; /* Lost alignment, don't know what we'd read
                                   next */
            }
        }
        if (msg.event != UFFD_EVENT_PAGEFAULT) {
            derror("%s: Read unexpected event %ud from userfaultfd", __func__,
                   msg.event);
            return nullptr; /* It's not a page fault, shouldn't happen */
        }
        return (void*)(uintptr_t)msg.arg.pagefault.address;
    }

    void pagefaultWorker() {
        assert(mUserfaultFd.valid());
        int timeoutNs = 0;
        for (;;) {
            pollfd pfd[] = {{mExitFd.get(), POLLIN},
                            {mUserfaultFd.get(), POLLIN}};
            timespec timeout = {0, timeoutNs};
            if (ppoll(pfd, ARRAY_SIZE(pfd), &timeout, nullptr) == -1) {
                derror("%s: userfault ppoll: %s", __func__, strerror(errno));
                break;
            }
            if (pfd[0].revents) {
                break;
            }
            if (pfd[1].revents) {
                while (auto ptr = readNextPagefaultAddr()) {
                    mAccessCallback(ptr);
                }
                timeoutNs = 0;
            } else {
                switch (mIdleCallback()) {
                case IdleCallbackResult::RunAgain:
                    timeoutNs = 0;
                    break;
                case IdleCallbackResult::Wait:
                    timeoutNs = 10 * 1000 * 1000;
                    break;
                case IdleCallbackResult::AllDone:
                    return;
                }
            }
        }
    }

    void stop() { HANDLE_EINTR(eventfd_write(mExitFd.get(), 1)); }

    MemoryAccessWatch::AccessCallback mAccessCallback;
    MemoryAccessWatch::IdleCallback mIdleCallback;

    base::ScopedFd mUserfaultFd;
    base::ScopedFd mExitFd;

    base::FunctorThread mPagefaultThread;
};

bool MemoryAccessWatch::isSupported() {
    base::ScopedFd ufd(syscall(__NR_userfaultfd, O_CLOEXEC));
    return checkUserfaultFdCaps(ufd.get());
}

MemoryAccessWatch::MemoryAccessWatch(AccessCallback&& accessCallback,
                                     IdleCallback&& idleCallback)
    : mImpl(new Impl(std::move(accessCallback), std::move(idleCallback))) {}

MemoryAccessWatch::~MemoryAccessWatch() {
    mImpl->stop();
}

bool MemoryAccessWatch::valid() const {
    return mImpl->mUserfaultFd.valid();
}

bool MemoryAccessWatch::registerMemoryRange(void* start, size_t length) {
    madvise(start, length, MADV_DONTNEED);
    uffdio_register regStruct = {{(uintptr_t)start, length},
                                 UFFDIO_REGISTER_MODE_MISSING};
    if (ioctl(mImpl->mUserfaultFd.get(), UFFDIO_REGISTER, &regStruct)) {
        derror("%s userfault register: %s", __func__, strerror(errno));
        return false;
    }
    return true;
}

void MemoryAccessWatch::doneRegistering() {
    if (valid()) {
        mImpl->mPagefaultThread.start();
    }
}

bool MemoryAccessWatch::fillPage(void* ptr, size_t length, const void* data) {
    if (data) {
        uffdio_copy copyStruct = {(uintptr_t)ptr, (uintptr_t)data, length};
        if (ioctl(mImpl->mUserfaultFd.get(), UFFDIO_COPY, &copyStruct)) {
            derror("%s: %s copy host: %p from: %p\n", __func__, strerror(errno),
                   (void*)copyStruct.dst, (void*)copyStruct.src);
            return false;
        }
    } else {
        uffdio_zeropage zeroStruct = {(uintptr_t)ptr, length};
        if (ioctl(mImpl->mUserfaultFd.get(), UFFDIO_ZEROPAGE, &zeroStruct)) {
            derror("%s: %s zero host: %p\n", __func__, strerror(errno),
                   (void*)zeroStruct.range.start);
            return false;
        }
    }
    uffdio_range rangeStruct{(uintptr_t)ptr, length};
    if (ioctl(mImpl->mUserfaultFd.get(), UFFDIO_UNREGISTER, &rangeStruct)) {
        derror("%s: userfault unregister %s", __func__, strerror(errno));
        return false;
    }
    return true;
}

}  // namespace snapshot
}  // namespace android

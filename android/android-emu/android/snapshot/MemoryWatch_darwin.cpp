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

#include "android/base/synchronization/Lock.h"
#include "android/base/system/System.h"
#include "android/base/threads/FunctorThread.h"
#include "android/crashreport/crash-handler.h"
#include "android/emulation/CpuAccelerator.h"
#include "android/snapshot/MacSegvHandler.h"

#include <signal.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/mman.h>

#include <Hypervisor/hv.h>

using android::base::System;

namespace android {
namespace snapshot {

static MemoryAccessWatch* sWatch = nullptr;

class MemoryAccessWatch::Impl {
public:
    Impl(MemoryAccessWatch::AccessCallback accessCallback,
         MemoryAccessWatch::IdleCallback idleCallback) :
        mAccel(GetCurrentCpuAccelerator()),
        mAccessCallback(accessCallback),
        mIdleCallback(idleCallback),
        mSegvHandler(MacDoAccessCallback),
        mBackgroundLoadingThread([this]() { bgLoaderWorker(); }) {
    }

    ~Impl() {
        mBackgroundLoadingThread.wait();
    }

    static void MacDoAccessCallback(void* ptr) {
        sWatch->mImpl->mAccessCallback(ptr);
    }

    static IdleCallbackResult MacIdlePageCallback() {
        return sWatch->mImpl->mIdleCallback();
    }

    bool registerMemoryRange(void* start, size_t length) {
        if (mAccel == CPU_ACCELERATOR_HVF) {
            bool found = false;
            uint64_t gpa = hva2gpa_call(start, &found);
            if (found) {
                guest_mem_protect_call(gpa, length, 0);
            }
        }
        mprotect(start, length, PROT_NONE);
        mSegvHandler.registerMemoryRange(start, length);
        return true;
    }

    void doneRegistering() {
        mBackgroundLoadingThread.start();
    }

    bool fillPage(void* start, size_t length, const void* data,
                  bool isQuickboot) {
        android::base::AutoLock lock(mLock);
        mprotect(start, length, PROT_READ | PROT_WRITE | PROT_EXEC);
        bool remapNeeded = false;
        if (!data) {
            // Remapping:
            // Is zero data, so try to use an existing zero page in the OS
            // instead of memset which might cause more memory to be resident.
            if (!isQuickboot &&
                (MAP_FAILED == mmap(start, length,
                                   PROT_READ | PROT_WRITE | PROT_EXEC,
                                   MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0))) {
                memset(start, 0x0, length);
                remapNeeded = false;
            } else {
                remapNeeded = true;
            }
        } else {
            memcpy(start, data, length);
        }
        if (mAccel == CPU_ACCELERATOR_HVF) {
            bool found = false;
            uint64_t gpa = hva2gpa_call(start, &found);
            if (found) {
                // Restore the mapping because we might have re-mapped above.
                if (remapNeeded) {
                    guest_mem_remap_call(start, gpa, length, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
                } else {
                    guest_mem_protect_call(gpa, length, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
                }
            }
        }
        return true;
    }

    void bgLoaderWorker() {
        System::Duration timeoutUs = 0;
        for (;;) {
            switch (mIdleCallback()) {
                case IdleCallbackResult::RunAgain:
                    timeoutUs = 0;
                    break;
                case IdleCallbackResult::Wait:
                    timeoutUs = 500;
                    break;
                case IdleCallbackResult::AllDone:
                    return;
            }
            if (timeoutUs) {
                System::get()->sleepUs(timeoutUs);
            }
        }
    }

    android::base::Lock mLock;
    CpuAccelerator mAccel;
    MemoryAccessWatch::AccessCallback mAccessCallback;
    MemoryAccessWatch::IdleCallback mIdleCallback;
    MacSegvHandler mSegvHandler;
    base::FunctorThread mBackgroundLoadingThread;
};

// static
bool MemoryAccessWatch::isSupported() {
    // TODO: HAXM
    return GetCurrentCpuAccelerator() == CPU_ACCELERATOR_HVF;
}

MemoryAccessWatch::MemoryAccessWatch(AccessCallback&& accessCallback,
                                     IdleCallback&& idleCallback) :
    mImpl(isSupported() ? new Impl(std::move(accessCallback),
                                   std::move(idleCallback)) : nullptr) {
    if (isSupported()) {
        sWatch = this;
    }
}

MemoryAccessWatch::~MemoryAccessWatch() {}

bool MemoryAccessWatch::valid() const {
    if (mImpl) return true;
    return false;
}

bool MemoryAccessWatch::registerMemoryRange(void* start, size_t length) {
    if (mImpl) return mImpl->registerMemoryRange(start, length);
    return false;
}

void MemoryAccessWatch::doneRegistering() {
    if (mImpl) mImpl->doneRegistering();
}

bool MemoryAccessWatch::fillPage(void* ptr, size_t length, const void* data,
                                 bool isQuickboot) {
    if (!mImpl) return false;
    return mImpl->fillPage(ptr, length, data, isQuickboot);
}

}  // namespace snapshot
}  // namespace android

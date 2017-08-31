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

#pragma once

#include "android/base/containers/SmallVector.h"
#include "android/base/files/StdioStream.h"
#include "android/base/synchronization/Lock.h"
#include "android/base/system/System.h"
#include "android/base/threads/Thread.h"
#include "android/snapshot/common.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace android {
namespace snapshot {

class ITextureLoader {
    DISALLOW_COPY_AND_ASSIGN(ITextureLoader);

protected:
    ~ITextureLoader() = default;

public:
    ITextureLoader() = default;

    using LoaderThreadPtr = std::shared_ptr<android::base::Thread>;
    using loader_t = std::function<void(android::base::Stream*)>;

    virtual bool start() = 0;
    // Move file position to texId and trigger loader
    virtual void loadTexture(uint32_t texId, const loader_t& loader) = 0;
    virtual void acquireLoaderThread(LoaderThreadPtr thread) = 0;
    virtual bool hasError() const = 0;
    virtual uint64_t diskSize() const = 0;
};

class TextureLoader final : public ITextureLoader {
public:
    TextureLoader(android::base::StdioStream&& stream);

    bool start() override;
    void loadTexture(uint32_t texId, const loader_t& loader) override;
    bool hasError() const override { return mHasError; }
    uint64_t diskSize() const override { return mDiskSize; }

    void acquireLoaderThread(LoaderThreadPtr thread) override {
        mLoaderThread = std::move(thread);
    }

    void join() {
        if (mLoaderThread) {
            mLoaderThread->wait();
            mLoaderThread.reset();
        }
    }

private:
    bool readIndex();

    android::base::StdioStream mStream;
    std::unordered_map<uint32_t, int64_t> mIndex;
    android::base::Lock mLock;
    bool mStarted = false;
    bool mHasError = false;
    uint64_t mDiskSize = 0;
#if SNAPSHOT_PROFILE > 1
    android::base::System::WallDuration mStartTime;
#endif
    LoaderThreadPtr mLoaderThread;
};

}  // namespace snapshot
}  // namespace android

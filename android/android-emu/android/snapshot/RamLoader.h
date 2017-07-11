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

#include "android/base/Compiler.h"
#include "android/base/Optional.h"
#include "android/base/files/StdioStream.h"
#include "android/base/synchronization/MessageChannel.h"
#include "android/base/system/System.h"
#include "android/base/threads/FunctorThread.h"
#include "android/base/threads/ThreadPool.h"
#include "android/snapshot/MemoryWatch.h"
#include "android/snapshot/common.h"

#include <cstdint>
#include <vector>

namespace android {
namespace snapshot {

class RamLoader {
    DISALLOW_COPY_AND_ASSIGN(RamLoader);

public:
    RamLoader(base::StdioStream&& stream, ZeroChecker zeroChecker);
    ~RamLoader();

    void registerBlock(const RamBlock& block);
    bool startLoading();
    bool wasStarted() const { return mWasStarted; }

    
private:
    enum class State : uint8_t { Empty, Reading, Read, Filling, Filled, Error };

    struct Page;
    using Pages = std::vector<Page>;

    struct FileIndex {
        struct Block {
            RamBlock ramBlock;
            Pages::iterator pagesBegin;
            Pages::iterator pagesEnd;
        };

        IndexFlags flags;
        std::vector<Block> blocks;
        Pages pages;
    };

    bool readIndex();
    bool registerPageWatches();

    void zeroOutPage(const Page& page);
    uint8_t* pagePtr(const Page& page) const;
    uint32_t pageSize(const Page& page) const;
    Page& page(void* ptr);

    bool readDataFromDisk(Page* pagePtr, uint8_t* preallocatedBuffer = nullptr);
    void fillPageData(Page* pagePtr);

    void readerWorker();
    MemoryAccessWatch::IdleCallbackResult backgroundPageLoad();
    MemoryAccessWatch::IdleCallbackResult fillPageInBackground(Page* page);
    void interruptReading();

    bool readAllPages();
    void startDecompressor();

    base::StdioStream mStream;
    int mStreamFd;  // An FD for the |mStream|'s underlying open file.
    ZeroChecker mZeroChecker;
    bool mWasStarted = false;

    base::Optional<MemoryAccessWatch> mAccessWatch;
    base::FunctorThread mReaderThread;
    Pages::iterator mBackgroundPageIt;
    bool mSentEndOfPagesMarker = false;
    base::MessageChannel<Page*, 32> mReadingQueue;
    base::MessageChannel<Page*, 32> mReadDataQueue;

    base::Optional<base::ThreadPool<Page*>> mDecompressor;

    FileIndex mIndex;

#if SNAPSHOT_PROFILE > 1
    base::System::WallDuration mStartTime;
#endif
};

}  // namespace snapshot
}  // namespace android

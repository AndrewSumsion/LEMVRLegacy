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

#include "android/snapshot/RamLoader.h"

#include "android/base/ArraySize.h"
#include "android/base/EintrWrapper.h"
#include "android/base/files/preadwrite.h"
#include "android/snapshot/Decompressor.h"
#include "android/utils/debug.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>

namespace android {
namespace snapshot {

struct RamLoader::Page {
    std::atomic<uint8_t> state{uint8_t(State::Empty)};
    uint16_t blockIndex;
    uint32_t sizeOnDisk;
    uint64_t filePos;
    uint8_t* data;

    Page() = default;
    Page(RamLoader::State state) : state(uint8_t(state)) {}
    Page(Page&& other)
        : state(other.state.load(std::memory_order_relaxed)),
          blockIndex(other.blockIndex),
          sizeOnDisk(other.sizeOnDisk),
          filePos(other.filePos),
          data(other.data) {}

    Page& operator=(Page&& other) {
        state.store(other.state.load(std::memory_order_relaxed),
                    std::memory_order_relaxed);
        blockIndex = other.blockIndex;
        sizeOnDisk = other.sizeOnDisk;
        filePos = other.filePos;
        data = other.data;
        return *this;
    }
};

RamLoader::RamLoader(base::StdioStream&& stream)
    : mStream(std::move(stream)), mReaderThread([this]() { readerWorker(); }) {
    if (MemoryAccessWatch::isSupported()) {
        mAccessWatch.emplace(
                [this](void* ptr) {
                    Page& page = this->page(ptr);
                    uint8_t buf[4096];
                    readDataFromDisk(&page, ARRAY_SIZE(buf) >= pageSize(page)
                                                    ? buf
                                                    : nullptr);
                    fillPageData(&page);
                    if (page.data != buf) {
                        delete[] page.data;
                    }
                    page.data = nullptr;
                },
                [this]() { return backgroundPageLoad(); });
        if (!mAccessWatch->valid()) {
            derror("Failed to initialize memory access watcher, falling back "
                   "to synchronous RAM loading");
            mAccessWatch.clear();
        }
    }
}

RamLoader::~RamLoader() {
    if (mWasStarted) {
        interruptReading();
        mReaderThread.wait();
        assert(hasError() || !mAccessWatch);
    }
}

void RamLoader::registerBlock(const RamBlock& block) {
    mIndex.blocks.push_back({block, {}, {}});
}

bool RamLoader::start() {
    if (mWasStarted) {
        return !mHasError;
    }

#if SNAPSHOT_PROFILE > 1
    mStartTime = base::System::get()->getHighResTimeUs();
#endif
    mWasStarted = true;
    if (!readIndex()) {
        mHasError = true;
        return false;
    }
    if (!mAccessWatch) {
        return readAllPages();
    }

    if (!registerPageWatches()) {
        mHasError = true;
        return false;
    }
    mBackgroundPageIt = mIndex.pages.begin();
    mAccessWatch->doneRegistering();
    mReaderThread.start();
    return true;
}

void RamLoader::join() {
    mJoining = true;
    mReaderThread.wait();
}

void RamLoader::interruptReading() {
    mReadDataQueue.stop();
    mReadingQueue.stop();
}

void RamLoader::zeroOutPage(const Page& page) {
    auto ptr = pagePtr(page);
    const RamBlock& block = mIndex.blocks[page.blockIndex].ramBlock;
    if (!isBufferZeroed(ptr, block.pageSize)) {
        memset(ptr, 0, size_t(block.pageSize));
    }
}

// Read a (usually) small delta using the same algorithm as in RamSaver's
// putDelta() function.
static int64_t getDelta(base::Stream* stream) {
    auto num = stream->getPackedNum();
    auto sign = num & 1;
    return sign ? -int32_t(num >> 1) : (num >> 1);
}

bool RamLoader::readIndex() {
#if SNAPSHOT_PROFILE > 1
    auto start = base::System::get()->getHighResTimeUs();
#endif
    mStreamFd = fileno(mStream.get());
    uint64_t indexPos = mStream.getBe64();
    fseek(mStream.get(), intptr_t(indexPos), SEEK_SET);

    auto version = mStream.getBe32();
    if (version != 1) {
        return false;
    }
    mIndex.flags = IndexFlags(mStream.getBe32());
    const bool compressed = nonzero(mIndex.flags & IndexFlags::CompressedPages);
    auto pageCount = mStream.getBe32();
    mIndex.pages.reserve(pageCount);
    int64_t runningFilePos = 8;
    int32_t prevPageSizeOnDisk = 0;
    for (size_t loadedBlockCount = 0; loadedBlockCount < mIndex.blocks.size();
         ++loadedBlockCount) {
        const auto nameLength = mStream.getByte();
        char name[256];
        mStream.read(name, nameLength);
        name[nameLength] = 0;
        auto blockIt = std::find_if(mIndex.blocks.begin(), mIndex.blocks.end(),
                                    [&name](const FileIndex::Block& b) {
                                        return strcmp(b.ramBlock.id, name) == 0;
                                    });
        if (blockIt == mIndex.blocks.end()) {
            return false;
        }
        const auto blockIndex = std::distance(mIndex.blocks.begin(), blockIt);
        FileIndex::Block& block = *blockIt;
        block.pagesBegin = mIndex.pages.end();

        uint32_t blockPagesCount = mStream.getBe32();
        for (int i = 0; i < int(blockPagesCount); ++i) {
            auto sizeOnDisk = mStream.getPackedNum();
            if (sizeOnDisk == 0) {
                // Empty page
                mIndex.pages.emplace_back(State::Read);
                Page& page = mIndex.pages.back();
                page.blockIndex = uint16_t(blockIndex);
                page.sizeOnDisk = 0;
                page.filePos = 0;
            } else {
                mIndex.pages.emplace_back();
                Page& page = mIndex.pages.back();
                page.blockIndex = uint16_t(blockIndex);
                page.sizeOnDisk = uint32_t(sizeOnDisk);
                auto posDelta = getDelta(&mStream);
                if (compressed) {
                    posDelta += prevPageSizeOnDisk;
                } else {
                    page.sizeOnDisk *= uint32_t(block.ramBlock.pageSize);
                    posDelta *= block.ramBlock.pageSize;
                }
                runningFilePos += posDelta;
                page.filePos = uint64_t(runningFilePos);
                prevPageSizeOnDisk = int32_t(page.sizeOnDisk);
            }
        }
        block.pagesEnd = mIndex.pages.end();
        assert(block.pagesEnd - block.pagesBegin == int(blockPagesCount));
    }

#if SNAPSHOT_PROFILE > 1
    printf("readIndex() time: %.03f\n",
           (base::System::get()->getHighResTimeUs() - start) / 1000.0);
#endif
    return true;
}

bool RamLoader::registerPageWatches() {
    uint8_t* startPtr = nullptr;
    uint64_t curSize = 0;
    for (const Page& page : mIndex.pages) {
        auto ptr = pagePtr(page);
        auto size = pageSize(page);
        if (ptr == startPtr + curSize) {
            curSize += size;
        } else {
            if (startPtr) {
                if (!mAccessWatch->registerMemoryRange(startPtr, curSize)) {
                    return false;
                }
            }
            startPtr = ptr;
            curSize = size;
        }
    }
    if (startPtr) {
        if (!mAccessWatch->registerMemoryRange(startPtr, curSize)) {
            return false;
        }
    }
    return true;
}

uint8_t* RamLoader::pagePtr(const RamLoader::Page& page) const {
    const FileIndex::Block& block = mIndex.blocks[page.blockIndex];
    return block.ramBlock.hostPtr + uint64_t(&page - &*block.pagesBegin) *
                                            uint64_t(block.ramBlock.pageSize);
}

uint32_t RamLoader::pageSize(const RamLoader::Page& page) const {
    return uint32_t(mIndex.blocks[page.blockIndex].ramBlock.pageSize);
}

template <class Num>
static bool isPowerOf2(Num num) {
    return !(num & (num - 1));
}

RamLoader::Page& RamLoader::page(void* ptr) {
    const auto blockIt = std::find_if(
            mIndex.blocks.begin(), mIndex.blocks.end(),
            [ptr](const FileIndex::Block& b) {
                return ptr >= b.ramBlock.hostPtr &&
                       ptr < b.ramBlock.hostPtr + b.ramBlock.totalSize;
            });
    assert(blockIt != mIndex.blocks.end());
    assert(ptr >= blockIt->ramBlock.hostPtr);
    assert(ptr < blockIt->ramBlock.hostPtr + blockIt->ramBlock.totalSize);
    assert(blockIt->pagesBegin != blockIt->pagesEnd);

    assert(isPowerOf2(blockIt->ramBlock.pageSize));
    auto pageStart = reinterpret_cast<uint8_t*>(
            (uintptr_t(ptr)) & uintptr_t(~(blockIt->ramBlock.pageSize - 1)));
    auto pageIndex = (pageStart - blockIt->ramBlock.hostPtr) /
                     blockIt->ramBlock.pageSize;
    auto pageIt = blockIt->pagesBegin + pageIndex;
    assert(pageIt != blockIt->pagesEnd);
    assert(ptr >= pagePtr(*pageIt));
    assert(ptr < pagePtr(*pageIt) + pageSize(*pageIt));
    return *pageIt;
}

void RamLoader::readerWorker() {
    while (auto pagePtr = mReadingQueue.receive()) {
        Page* page = *pagePtr;
        if (!page) {
            mReadDataQueue.send(nullptr);
            mReadingQueue.stop();
            break;
        }

        if (readDataFromDisk(page)) {
            mReadDataQueue.send(page);
        }
    }

    mAccessWatch.clear();

#if SNAPSHOT_PROFILE > 1
    printf("Background loading complete in %.03f ms\n",
           (base::System::get()->getHighResTimeUs() - mStartTime) / 1000.0);
#endif
}

MemoryAccessWatch::IdleCallbackResult RamLoader::backgroundPageLoad() {
    if (mReadingQueue.isStopped() && mReadDataQueue.isStopped()) {
        return MemoryAccessWatch::IdleCallbackResult::AllDone;
    }

    {
        Page* page = nullptr;
        if (mReadDataQueue.tryReceive(&page)) {
            return fillPageInBackground(page);
        }
    }

    for (int i = 0; i < int(mReadingQueue.capacity()); ++i) {
        // Find next page to queue.
        mBackgroundPageIt = std::find_if(
                mBackgroundPageIt, mIndex.pages.end(), [](const Page& page) {
                    auto state = page.state.load(std::memory_order_acquire);
                    return state == uint8_t(State::Empty) ||
                           (state == uint8_t(State::Read) && !page.data);
                });
#if SNAPSHOT_PROFILE > 2
        const auto count = int(mBackgroundPageIt - mIndex.pages.begin());
        if ((count % 10000) == 0 || count == int(mIndex.pages.size())) {
            printf("Background loading: at page #%d of %d\n", count,
                   int(mIndex.pages.size()));
        }
#endif

        if (mBackgroundPageIt == mIndex.pages.end()) {
            if (!mSentEndOfPagesMarker) {
                mSentEndOfPagesMarker = mReadingQueue.trySend(nullptr);
            }
            return mJoining ? MemoryAccessWatch::IdleCallbackResult::RunAgain
                            : MemoryAccessWatch::IdleCallbackResult::Wait;
        }

        if (mBackgroundPageIt->state.load(std::memory_order_relaxed) ==
            uint8_t(State::Read)) {
            Page* const page = &*mBackgroundPageIt++;
            return fillPageInBackground(page);
        }

        if (mReadingQueue.trySend(&*mBackgroundPageIt)) {
            ++mBackgroundPageIt;
        } else {
            // The queue is full - let's wait for a while to give the reader
            // time to empty it.
            return mJoining ? MemoryAccessWatch::IdleCallbackResult::RunAgain
                            : MemoryAccessWatch::IdleCallbackResult::Wait;
        }
    }

    return MemoryAccessWatch::IdleCallbackResult::RunAgain;
}

MemoryAccessWatch::IdleCallbackResult RamLoader::fillPageInBackground(
        RamLoader::Page* page) {
    if (page) {
        fillPageData(page);
        delete[] page->data;
        page->data = nullptr;
        // If we've loaded a page then this function took quite a while
        // and it's better to check for a pagefault before proceeding to
        // queuing pages into the reader thread.
        return mJoining ? MemoryAccessWatch::IdleCallbackResult::RunAgain
                        : MemoryAccessWatch::IdleCallbackResult::Wait;
    } else {
        // null page == all pages were loaded, stop.
        mReadDataQueue.stop();
        mReadingQueue.stop();
        return MemoryAccessWatch::IdleCallbackResult::AllDone;
    }
}

bool RamLoader::readDataFromDisk(Page* pagePtr, uint8_t* preallocatedBuffer) {
    Page& page = *pagePtr;
    if (page.sizeOnDisk == 0) {
        assert(page.state.load(std::memory_order_relaxed) ==
               uint8_t(State::Read));
        page.data = nullptr;
        return true;
    }

    auto state = uint8_t(State::Empty);
    if (!page.state.compare_exchange_strong(state, uint8_t(State::Reading),
                                            std::memory_order_acquire)) {
        // Spin until the reading thread finishes.
        while (state < uint8_t(State::Read)) {
            state = uint8_t(page.state.load(std::memory_order_acquire));
        }
        return false;
    }

    uint8_t compressedBuf[4208];
    const bool compressed = nonzero(mIndex.flags & IndexFlags::CompressedPages);
    auto size = page.sizeOnDisk;

    // We need to allocate a dynamic buffer if:
    // - page is compressed and there's a decompressing thread pool
    // - page is compressed and local buffer is too small
    // - there's no preallocated buffer passed from the caller
    bool allocateBuffer = (compressed && (mDecompressor ||
                                          ARRAY_SIZE(compressedBuf) < size)) ||
                          !preallocatedBuffer;
    auto buf = allocateBuffer ? new uint8_t[size]
                              : compressed ? compressedBuf : preallocatedBuffer;
    auto read = HANDLE_EINTR(
            base::pread(mStreamFd, buf, size, int64_t(page.filePos)));
    if (read != int64_t(size)) {
        derror("(%d) Reading page %p from disk returned less data: %d of %d at "
               "%lld",
               errno, this->pagePtr(page), int(read), int(size),
               static_cast<long long>(page.filePos));
        if (allocateBuffer) {
            delete[] buf;
        }
        page.state.store(uint8_t(State::Error));
        mHasError = true;
        return false;
    }

    if (compressed) {
        if (mDecompressor) {
            page.data = buf;
            mDecompressor->enqueue(&page);
        } else {
            auto decompressed = preallocatedBuffer
                                        ? preallocatedBuffer
                                        : new uint8_t[pageSize(page)];
            if (!Decompressor::decompress(buf, int32_t(size), decompressed,
                                          int32_t(pageSize(page)))) {
                derror("Decompressing page %p failed", this->pagePtr(page));
                if (!preallocatedBuffer) {
                    delete[] decompressed;
                }
                page.state.store(uint8_t(State::Error));
                mHasError = true;
                return false;
            }
            buf = decompressed;
        }
    }

    page.data = buf;
    page.state.store(uint8_t(State::Read), std::memory_order_release);
    return true;
}

void RamLoader::fillPageData(Page* pagePtr) {
    Page& page = *pagePtr;
    auto state = uint8_t(State::Read);
    if (!page.state.compare_exchange_strong(state, uint8_t(State::Filling),
                                            std::memory_order_acquire)) {
        if (state == uint8_t(State::Read) && !page.data) {
            page.state.store(uint8_t(State::Filled), std::memory_order_relaxed);
        } else {
            assert(state == uint8_t(State::Filled));
        }
        return;
    }

#if SNAPSHOT_PROFILE > 2
    printf("%s: loading page %p\n", __func__, this->pagePtr(page));
#endif
    if (mAccessWatch) {
        bool res = mAccessWatch->fillPage(this->pagePtr(page), pageSize(page),
                                          page.data);
        if (!res) {
            mHasError = true;
        }
        page.state.store(uint8_t(res ? State::Filled : State::Error),
                         std::memory_order_release);
    }
}

bool RamLoader::readAllPages() {
#if SNAPSHOT_PROFILE > 1
    auto startTime = base::System::get()->getHighResTimeUs();
#endif
    if (nonzero(mIndex.flags & IndexFlags::CompressedPages) && !mAccessWatch) {
        startDecompressor();
    }

    // Rearrange the nonzero pages in sequential disk order for faster reading.
    // Zero out all zero pages right here.
    std::vector<Page*> sortedPages;
    sortedPages.reserve(mIndex.pages.size());

#if SNAPSHOT_PROFILE > 1
    auto startTime1 = base::System::get()->getHighResTimeUs();
#endif

    for (Page& page : mIndex.pages) {
        if (page.sizeOnDisk) {
            sortedPages.emplace_back(&page);
        } else {
            zeroOutPage(page);
        }
    }

#if SNAPSHOT_PROFILE > 1
    printf("zeroing took %.03f ms\n",
           (base::System::get()->getHighResTimeUs() - startTime1) / 1000.0);
#endif

    std::sort(sortedPages.begin(), sortedPages.end(),
              [](const Page* l, const Page* r) {
                  return l->filePos < r->filePos;
              });

#if SNAPSHOT_PROFILE > 1
    printf("Starting unpacker + sorting took %.03f ms\n",
           (base::System::get()->getHighResTimeUs() - startTime) / 1000.0);
#endif

    for (Page* page : sortedPages) {
        if (!readDataFromDisk(page, pagePtr(*page))) {
            mHasError = true;
            return false;
        }
    }

    mDecompressor.clear();
    return true;
}

void RamLoader::startDecompressor() {
    mDecompressor.emplace([this](Page* page) {
        const bool res = Decompressor::decompress(
                page->data, int32_t(page->sizeOnDisk), pagePtr(*page),
                int32_t(pageSize(*page)));
        delete[] page->data;
        page->data = nullptr;
        if (!res) {
            derror("Decompressing page %p failed", pagePtr(*page));
            mHasError = true;
            page->state.store(uint8_t(State::Error));
        }
    });
    mDecompressor->start();
}

}  // namespace snapshot
}  // namespace android

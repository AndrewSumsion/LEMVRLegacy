// Copyright 2018 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/base/AlignedBuf.h"

#include <gtest/gtest.h>

#include <vector>
#include <array>

using android::AlignedBuf;

TEST(AlignedBuf, Basic) {
    const int numItems = 10;

    // Check that the buffers are aligned
    {
        AlignedBuf<uint32_t, 64> buf(numItems);
        EXPECT_EQ(0, (uintptr_t)buf.data() & (64 - 1));
        EXPECT_EQ(numItems, buf.size());
    }

    {
        AlignedBuf<uint32_t, 256> buf(numItems);
        EXPECT_EQ(0, (uintptr_t)buf.data() & (256 - 1));
        EXPECT_EQ(numItems, buf.size());
    }

    {
        AlignedBuf<uint32_t, 4096> buf(numItems);
        EXPECT_EQ(0, (uintptr_t)buf.data() & (4096 - 1));
        EXPECT_EQ(numItems, buf.size());
    }

    const int numManyItems = 100;
    const int numFewItems = 4;

    // Test read/write
    AlignedBuf<uint32_t, 64> buf(numManyItems);
    uint32_t* bufData = buf.data();
    for (int i = 0; i < numManyItems; i++) {
        bufData[i] = 0;
        EXPECT_EQ(0, bufData[i]);
    }

    AlignedBuf<uint32_t, 64> buf2(numFewItems);
    bufData = buf2.data();
    for (int i = 0; i < numFewItems; i++) {
        bufData[i] = 0;
        EXPECT_EQ(0, bufData[i]);
    }
}

// Tests that copy constructor copies underlying buffer.
TEST(AlignedBuf, Copy) {
    constexpr int align = 64;
    constexpr int size = 128;

    AlignedBuf<uint32_t, align> buf(size);
    AlignedBuf<uint32_t, align> buf2 = buf;

    EXPECT_EQ(buf2.size(), buf.size());
    EXPECT_NE(buf2.data(), buf.data());

    for (int i = 0; i < buf.size(); i++) {
        buf[i] = 0;
    }

    for (int i = 0; i < buf2.size(); i++) {
        buf2[i] = 1;
    }

    for (int i = 0; i < buf.size(); i++) {
        EXPECT_EQ(0, buf[i]);
    }
}

// Tests that move constructor preserves underlying buffer.
TEST(AlignedBuf, Move) {
    constexpr int align = 64;
    constexpr int size = 128;

    AlignedBuf<uint32_t, align> buf(size);

    for (int i = 0; i < buf.size(); i++) {
        buf[i] = 0;
        EXPECT_EQ(0, buf[i]);
    }

    AlignedBuf<uint32_t, align> buf2 = std::move(buf);

    EXPECT_EQ(0, buf.size());
    EXPECT_EQ(size, buf2.size());

    for (int i = 0; i < buf2.size(); i++) {
        EXPECT_EQ(0, buf2[i]);
        // Check that it is stil writable.
        buf2[i] = 0;
    }
}

// Tests that operator== is comparing raw bytes.
TEST(AlignedBuf, Compare) {
    constexpr int align = 64;
    constexpr int size = 128;

    AlignedBuf<uint32_t, align> buf(size);
    AlignedBuf<uint32_t, align> buf2 = buf;

    EXPECT_EQ(buf, buf2);
}

// Tests that resize preserves contents.
TEST(AlignedBuf, Resize) {
    std::array<char, 4> contents = { 0xa, 0xb, 0xc, 0xd };
    size_t initialSize = contents.size();
    AlignedBuf<char, 4096> buf(initialSize);

    auto check = [&]() {
        for (size_t i = 0; i < initialSize; i++) {
            EXPECT_EQ(contents[i], buf[i]);
        }
    };

    memcpy(buf.data(), contents.data(), initialSize);
    check();

    for (size_t i = 0; i < 10; i++) {
        buf.resize(initialSize + i * 4096);
        check();
    }
}

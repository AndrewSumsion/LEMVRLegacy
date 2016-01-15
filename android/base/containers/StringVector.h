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

#include "android/base/containers/PodVector.h"
#include "android/base/String.h"
#include "android/base/StringView.h"

namespace android {
namespace base {

// A StringVector is a vector of strings. This implementation is optimized
// to use less memory and be more efficient than std::vector<std::string>
// for most operations.
class StringVector : public PodVector<String> {
public:
    StringVector() = default;

    // Copy-constructor.
    StringVector(const StringVector& other);

    // Initializer-list constructor.
    StringVector(std::initializer_list<StringView> list);

    // Assignment operator.
    StringVector& operator=(const StringVector& other);

    // Destructor.
    ~StringVector();

    // Any operations that may change the underlying storage must be
    // overriden. However, the behaviour / documentation should be
    // identical to the one from PodVector<String> here.
    void resize(size_t newSize);
    void reserve(size_t newSize);

    void remove(size_t index);
    String* emplace(size_t index);
    void swap(StringVector* other);

    // std::vector<> compatibility.
    void push_back(const StringView& str) { append(str);  }
    void pop() { remove(0U);  }

    void insert(size_t index, const StringView& view);
    void prepend(const StringView& view);
    void append(const StringView& view);
};

}  // namespace base
}  // namespace android

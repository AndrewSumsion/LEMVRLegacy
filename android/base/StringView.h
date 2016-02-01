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

#include <string>
#include <string.h>

namespace android {
namespace base {

class String;

// A StringView is a simple (address, size) pair that points to an
// existing read-only string. It's a convenience class used to hidden
// creation of String() objects un-necessarily.
//
// Consider the two following functions:
//
//     size_t  count1(const String& str) {
//         size_t result = 0;
//         for (size_t n = 0; n < str.size(); ++n) {
//              if (str[n] == '1') {
//                  count++;
//              }
//         }
//     }
//
//     size_t  count2(const StringView& str) {
//         size_t result = 0;
//         for (size_t n = 0; n < str.size(); ++n) {
//              if (str[n] == '2') {
//                  count++;
//              }
//         }
//     }
//
// Then consider the following calls:
//
//       size_t n1 = count1("There is 1 one in this string!");
//       size_t n2 = count2("I can count 2 too");
//
// In the first case, the compiler will silently create a temporary
// String object, copy the input string into it (allocating memory in
// the heap), call count1() and destroy the String upon its return.
//
// In the second case, the compiler will create a temporary StringView,
// initialize it trivially before calling count2(), this results in
// much less generated code, as well as better performance.
//
// Generally speaking, always use a reference or pointer to StringView
// instead of a String if your function or method doesn't need to modify
// its input.
//
class StringView {
public:
    StringView() : mString(""), mSize(0U) {}

    StringView(const StringView& other) :
        mString(other.data()), mSize(other.size()) {}

    // IMPORTANT: This is intentionally not 'explicit'.
    StringView(const char* string) :
            mString(string ? string : ""), mSize(string ? strlen(string) : 0) {}

    // IMPORTANT: This is intentionally not 'explicit'.
    StringView(const String& str);

    // IMPORTANT: This is intentionally not 'explicit'.
    StringView(const std::string& str) :
        mString(str.c_str()), mSize(str.size()) {}

    StringView(const char* str, size_t len)
        : mString(str ? str : ""), mSize(len) {}

    StringView(const char* begin, const char* end)
        : mString(begin ? begin : ""), mSize(begin ? end - begin : 0) {}

    const char* c_str() const { return mString; }
    const char* str() const { return mString; }
    const char* data() const { return mString; }
    size_t size() const { return mSize; }

    typedef const char* iterator;
    typedef const char* const_iterator;

    const_iterator begin() const { return mString; }
    const_iterator end() const { return mString + mSize; }

    bool empty() const { return !size(); }

    void clear() {
        mSize = 0;
        mString = "";
    }

    char operator[](size_t index) {
        return mString[index];
    }

    void set(const char* data, size_t len) {
        mString = data ? data : "";
        mSize = len;
    }

    void set(const char* str) {
        mString = str ? str : "";
        mSize = ::strlen(mString);
    }

    void set(const StringView& other) {
        mString = other.mString;
        mSize = other.mSize;
    }

    // Compare with another StringView.
    int compare(const StringView& other) const;

    StringView& operator=(const StringView& other) {
        set(other);
        return *this;
    }

private:
    const char* mString;
    size_t mSize;
};

// Comparison operators. Defined as functions to allow automatic type
// conversions with C strings and String objects.

bool operator==(const StringView& x, const StringView& y);

inline bool operator!=(const StringView& x, const StringView& y) {
    return !(x == y);
}

inline bool operator<(const StringView& x, const StringView& y) {
    return x.compare(y) < 0;
}

inline bool operator>=(const StringView& x, const StringView& y) {
    return !(x < y);
}

inline bool operator >(const StringView& x, const StringView& y) {
    return x.compare(y) > 0;
}

inline bool operator<=(const StringView& x, const StringView& y) {
    return !(x > y);
}

}  // namespace base
}  // namespace android

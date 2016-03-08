// Copyright 2015 The Android Open Source Project
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

namespace android {
namespace base {

// Class |Version| is a class for software version manipulations
// it is able to parse, store, compare and convert version back to string
// Expected string format is "major.minor.micro", where all three components
// are unsigned numbers (and, hopefully, reasonably small)
class Version {
public:
    explicit Version(const char* ver);
    Version(unsigned int major, unsigned int minor, unsigned int micro);

    bool isValid() const;

    bool operator<(const Version& other) const;
    bool operator==(const Version& other) const;
    bool operator!=(const Version& other) const;

    std::string toString() const;

    static Version Invalid();

private:
    unsigned int mMajor;
    unsigned int mMinor;
    unsigned int mMicro;
};

}  // namespace android
}  // namespace base

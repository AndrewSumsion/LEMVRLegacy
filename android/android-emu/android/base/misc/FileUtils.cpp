// Copyright (C) 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/base/misc/FileUtils.h"
#include "android/utils/eintr_wrapper.h"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#endif

#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace android {

bool readFileIntoString(int fd, std::string* file_contents) {
    off_t size = lseek(fd, 0, SEEK_END);
    if (size == (off_t)-1) {
        return false;
    }
    off_t err = lseek(fd, 0, SEEK_SET);
    if (err == (off_t)-1) {
        return false;
    }

    std::string buf((size_t)size, '\0');
    ssize_t result = HANDLE_EINTR(read(fd, &buf[0], size));
    if (result != size) {
        return false;
    }
    *file_contents = std::move(buf);
    return true;
}

bool writeStringToFile(int fd, const std::string& file_contents) {
    ssize_t result = HANDLE_EINTR(
            write(fd, file_contents.c_str(), file_contents.size()));
    if (result != (ssize_t)file_contents.size()) {
        return false;
    }
    return true;
}

base::Optional<std::string> readFileIntoString(base::StringView name) {
    std::ifstream is(name.isNullTerminated()
                             ? name.c_str()
                             : std::string(name.data(), name.size()).c_str());
    if (!is) {
        return {};
    }

    std::ostringstream ss;
    ss << is.rdbuf();
    return ss.str();
}

bool setFileSize(int fd, int64_t size) {
#ifdef _WIN32
    return _chsize_s(fd, size) == 0;
#else
    return ftruncate(fd, size) == 0;
#endif
}

}  // namespace android

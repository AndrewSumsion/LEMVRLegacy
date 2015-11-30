// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "android/base/Compiler.h"

#include "android/base/String.h"
#include "android/base/containers/StringVector.h"

#include <limits.h>
#include <stdint.h>
#include <time.h>


namespace android {
namespace base {

// Interface class to the underlying operating system.
class System {
public:
    typedef int64_t Duration;

    // Information about user and system times for some process,
    // in milliseconds
    struct Times {
        Duration userMs;
        Duration systemMs;
    };

public:
    // Call this function to get the instance
    static System* get();

    // Default constructor doesn't do anything.
    System() {}

    // Default destructor is empty but virtual.
    virtual ~System() {}

    // Return the path of the current program's directory.
    virtual const String& getProgramDirectory() const = 0;

    // Return the path of the emulator launcher's directory.
    virtual const String& getLauncherDirectory() const = 0;

    // Return the path to user's home directory (as defined in the
    // underlying platform) or an empty string if it can't be found
    virtual const String& getHomeDirectory() const = 0;

    // Return the path to user's App Data directory (only applies
    // in Microsoft Windows) or an empty string if it can't be found
    virtual const String& getAppDataDirectory() const = 0;

    // Return the current directory path. Because this can change at
    // runtime, this returns a new String instance, not a const-reference
    // to a constant one held by the object. Return an empty string if there is
    // a problem with the system when getting the current directory.
    virtual String getCurrentDirectory() const = 0;

    // Return the host bitness as an integer, either 32 or 64.
    // Note that this is different from the program's bitness. I.e. if
    // a 32-bit program runs under a 64-bit host, getProgramBitness()
    // shall return 32, but getHostBitness() shall return 64.
    virtual int getHostBitness() const = 0;

    // Return the program bitness as an integer, either 32 or 64.
#ifdef __x86_64__
    static const int kProgramBitness = 64;
#else
    static const int kProgramBitness = 32;
#endif

#ifdef _WIN32
    static const char kDirSeparator = '\\';
#else
    static const char kDirSeparator = '/';
#endif

    // The character used to separator directories in path-related
    // environment variables.
#ifdef _WIN32
    static const char kPathSeparator = ';';
#else
    static const char kPathSeparator = ':';
#endif

    // Environment variable name corresponding to the library search
    // list for shared libraries.
    static const char* kLibrarySearchListEnvVarName;

    // Return the name of the sub-directory containing libraries
    // for the current platform, i.e. "lib" or "lib64" depending
    // on the value of kProgramBitness.
    static const char* kLibSubDir;

    // Return the name of the sub-directory containing executables
    // for the current platform, i.e. "bin" or "bin64" depending
    // on the value of kProgramBitness.
    static const char* kBinSubDir;

    // Name of the 32-bit binaries subdirectory
    static const char* kBin32SubDir;

    // Return program's bitness, either 32 or 64.
    static int getProgramBitness() { return kProgramBitness; }

    // Prepend a new directory to the system's library search path. This
    // only alters an environment variable like PATH or LD_LIBRARY_PATH,
    // and thus typically takes effect only after spawning/executing a new
    // process.
    static void addLibrarySearchDir(const char* dirPath);

    // Find a bundled executable named |programName|, it must appear in the
    // kBinSubDir of getLauncherDirectory(). The name should not include the
    // executable extension (.exe) on Windows.
    // Return an empty string if the file doesn't exist.
    static String findBundledExecutable(const char* programName);

    // Retrieve the value of a given environment variable.
    // Equivalent to getenv() but returns a String instance.
    // If the variable is not defined, return an empty string.
    // NOTE: On Windows, this uses _wgetenv() and returns the corresponding
    // UTF-8 text string.
    virtual String envGet(const char* varname) const = 0;

    // Set the value of a given environment variable.
    // If |varvalue| is NULL or empty, this unsets the variable.
    // Equivalent to setenv().
    virtual void envSet(const char* varname, const char* varvalue) = 0;

    // Returns true if environment variable |varname| is set and non-empty.
    virtual bool envTest(const char* varname) const = 0;

    // Return true iff |path| exists on the file system.
    virtual bool pathExists(const char* path) const = 0;

    // Return true iff |path| exists and is a regular file on the file system.
    virtual bool pathIsFile(const char* path) const = 0;

    // Return true iff |path| exists and is a directory on the file system.
    virtual bool pathIsDir(const char* path) const = 0;

    // Return true iff |path| exists and can be read by the current user.
    virtual bool pathCanRead(const char* path) const = 0;

    // Return true iff |path| exists and can be written to by the current
    // user.
    virtual bool pathCanWrite(const char* path) const = 0;

    // Return true iff |path| exists and can be executed to by the current
    // user.
    virtual bool pathCanExec(const char* path) const = 0;

    // Scan directory |dirPath| for entries, and return them as a sorted
    // vector or entries. If |fullPath| is true, then each item of the
    // result vector contains a full path.
    virtual StringVector scanDirEntries(const char* dirPath,
                                        bool fullPath = false) const = 0;

    // Checks the system to see if it is running under a remoting session
    // like Nomachine's NX, Chrome Remote Desktop or Windows Terminal Services.
    // On success, return true and sets |*sessionType| to the detected
    // session type. Otherwise, just return false.
    virtual bool isRemoteSession(String* sessionType) const = 0;

    // Returns Times structure for the current process
    virtual Times getProcessTimes() const = 0;

    // Returns the current Unix timestamp
    virtual time_t getUnixTime() const = 0;

    // Run a shell command silently. This doesn't try to wait for it to
    // complete and will return as soon as possible. |commandLine| is a list
    // of parameters, where |commandLine[0]| is the full path to the
    // executable. Return true on success, false on failure (i.e. if the
    // executable could not be found, not whether the command itself
    // succeeded).
    virtual bool runSilentCommand(const StringVector& commandLine) = 0;

    // Return the path of a temporary directory appropriate for the system.
    virtual String getTempDir() const = 0;

protected:
    static System* setForTesting(System* system);

    // Internal implementation of scanDirEntries() that can be used by
    // mock implementation using a fake file system rooted into a temporary
    // directory or something like that. Always returns short paths.
    static StringVector scanDirInternal(const char* dirPath);

    static bool pathExistsInternal(const char* path);
    static bool pathIsFileInternal(const char* path);
    static bool pathIsDirInternal(const char* path);
    static bool pathCanReadInternal(const char* path);
    static bool pathCanWriteInternal(const char* path);
    static bool pathCanExecInternal(const char* path);

private:
    DISALLOW_COPY_AND_ASSIGN(System);
};

}  // namespace base
}  // namespace android

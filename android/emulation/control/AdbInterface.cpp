// Copyright (C) 2016 The Android Open Source Project
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

#include "android/emulation/control/AdbInterface.h"

#include "android/base/StringView.h"
#include "android/base/Uuid.h"
#include "android/base/files/PathUtils.h"
#include "android/base/system/System.h"
#include "android/emulation/ConfigDirs.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace android::base;

namespace android {
namespace emulation {

// Helper function, checks if the version of adb in the given SDK is
// fresh enough.
static bool checkAdbVersion(const std::string& sdk_root_directory) {
    static const int kMinAdbVersionMajor = 23;
    static const int kMinAdbVersionMinor = 1;

    if (sdk_root_directory.empty()) {
        return false;
    }

    // The file at $(ANDROID_SDK_ROOT)/platform-tools/source.properties tells
    // what version the ADB executable is. Find that file.
    std::string properties_path = PathUtils::join(
            sdk_root_directory, "platform-tools", "source.properties");

    std::ifstream properties_file(properties_path.c_str());
    if (properties_file) {
        // Find the line containing "Pkg.Revision".
        std::string line;
        while (std::getline(properties_file, line)) {
            int version_major, version_minor = 0;
            if (sscanf(line.c_str(), " Pkg.Revision = %d.%d", &version_major,
                       &version_minor) >= 1) {
                return version_major > kMinAdbVersionMajor ||
                       (version_major == kMinAdbVersionMajor &&
                        version_minor >= kMinAdbVersionMinor);
            }
        }
    }

    // If the file is missing, assume the tools directory is broken in some
    // way, and updating should fix the problem.
    return false;
}

AdbInterface::AdbInterface(android::base::Looper* looper)
    : mLooper(looper), mAdbVersionCurrent(false) {
    // First try finding ADB by the environment variable.
    auto sdk_root_by_env = android::ConfigDirs::getSdkRootDirectoryByEnv();
    if (!sdk_root_by_env.empty()) {
        // If ANDROID_SDK_ROOT is defined, the user most likely wanted to use
        // that ADB. Store it for later - if the second potential ADB path
        // also fails, we'll warn the user about this one.
        mAdbPath = PathUtils::join(sdk_root_by_env, "platform-tools", "adb");
        if (checkAdbVersion(sdk_root_by_env)) {
            mAdbVersionCurrent = true;
            return;
        }
    }

    // If the first path was non-existent or a bad version, try to infer the
    // path based on the emulator executable location.
    auto sdk_root_by_path = android::ConfigDirs::getSdkRootDirectoryByPath();
    if (sdk_root_by_path != sdk_root_by_env && !sdk_root_by_path.empty()) {
        if (checkAdbVersion(sdk_root_by_path)) {
            mAdbPath =
                    PathUtils::join(sdk_root_by_path, "platform-tools", "adb");
            mAdbVersionCurrent = true;
            return;

            // Only save this path if the ANDROID_SDK_ROOT path was not set.
        } else if (mAdbPath.empty()) {
            mAdbPath =
                    PathUtils::join(sdk_root_by_path, "platform-tools", "adb");
        }
    }
}

AdbCommandPtr AdbInterface::runAdbCommand(
        const std::vector<std::string>& args,
        std::function<void(const OptionalAdbCommandResult&)> result_callback,
        base::System::Duration timeout_ms,
        bool want_output) {
    auto command = std::shared_ptr<AdbCommand>(new AdbCommand(
            mLooper, mAdbPath, args, want_output, timeout_ms, result_callback));
    command->start();
    return command;
}

AdbCommand::AdbCommand(android::base::Looper* looper,
                       const std::string& adb_path,
                       const std::vector<std::string>& command,
                       bool want_output,
                       base::System::Duration timeout,
                       AdbCommand::ResultCallback callback)
    : mLooper(looper),
      mResultCallback(callback),
      mOutputFilePath(PathUtils::join(
              System::get()->getTempDir(),
              std::string("adbcommand").append(Uuid::generate().toString()))),
      mWantOutput(want_output),
      mTimeout(timeout),
      mFinished(false) {
    mCommand.push_back(adb_path);
    mCommand.insert(mCommand.end(), command.begin(), command.end());
}

void AdbCommand::start() {
    if (!mTask && !mFinished) {
        auto shared = shared_from_this();
        mTask.reset(new ParallelTask<OptionalAdbCommandResult>(
                mLooper,
                [shared](OptionalAdbCommandResult* result) {
                    shared->taskFunction(result);
                },
                [shared](const OptionalAdbCommandResult& result) {
                    shared->taskDoneFunction(result);
                }));
        mTask->start();
    }
}

void AdbCommand::taskDoneFunction(const OptionalAdbCommandResult& result) {
    if (!mCancelled) {
        mResultCallback(result);
    }
    mTask.reset();
    mFinished = true;
}

void AdbCommand::taskFunction(OptionalAdbCommandResult* result) {
    RunOptions output_flag = mWantOutput ? System::RunOptions::DumpOutputToFile
                                         : System::RunOptions::HideAllOutput;
    RunOptions run_flags = System::RunOptions::WaitForCompletion |
                           System::RunOptions::TerminateOnTimeout | output_flag;
    System::Pid pid;
    android::base::System::ProcessExitCode exit_code;

    bool command_ran = System::get()->runCommand(
            mCommand, run_flags, mTimeout, &exit_code, &pid, mOutputFilePath);

    if (command_ran) {
        *result = android::base::makeOptional<AdbCommandResult>(
                {exit_code,
                 mWantOutput ? std::unique_ptr<std::ifstream>(new std::ifstream(
                                       mOutputFilePath.c_str()))
                             : std::unique_ptr<std::ifstream>()});
    }
}
}
}

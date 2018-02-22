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

#include "android/base/ArraySize.h"
#include "android/base/Log.h"
#include "android/base/Optional.h"
#include "android/base/StringView.h"
#include "android/base/Uuid.h"
#include "android/base/files/PathUtils.h"
#include "android/base/files/ScopedFd.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/base/misc/FileUtils.h"
#include "android/base/sockets/ScopedSocket.h"
#include "android/base/sockets/SocketWaiter.h"
#include "android/base/system/System.h"
#include "android/emulation/AdbHostServer.h"
#include "android/emulation/ComponentVersion.h"
#include "android/emulation/ConfigDirs.h"
#include "android/utils/debug.h"
#include "android/utils/path.h"
#include "android/utils/sockets.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <utility>

using android::base::AutoLock;
using android::base::Looper;
using android::base::Optional;
using android::base::ParallelTask;
using android::base::PathUtils;
using android::base::RunOptions;
using android::base::ScopedCPtr;
using android::base::ScopedPtr;
using android::base::ScopedSocket;
using android::base::SocketWaiter;
using android::base::System;
using android::base::Uuid;
using android::base::Version;

namespace android {
namespace emulation {

// We consider adb shipped with 23.1.0 or later to be modern enough
// to not notify people to upgrade adb. 23.1.0 ships with adb protocol 32
static const int kMinAdbProtocol = 32;

class AdbDaemonImpl : public AdbDaemon {
public:
    virtual Optional<int> getProtocolVersion() override {
        return AdbHostServer::getProtocolVersion();
    }
};

class AdbInterfaceImpl final : public AdbInterface {
public:
    friend AdbInterface::Builder;

    bool isAdbVersionCurrent() const final { return mAdbVersionCurrent; }

    void setCustomAdbPath(const std::string& path) final {
        mCustomAdbPath = path;
    }

    const std::string& detectedAdbPath() const final { return mAutoAdbPath; }

    const std::string& adbPath() final;

    virtual void setSerialNumberPort(int port) final {
        mSerialString = std::string("emulator-") + std::to_string(port);
    }

    const std::string& serialString() const final { return mSerialString; }

    AdbCommandPtr runAdbCommand(const std::vector<std::string>& args,
                                ResultCallback&& result_callback,
                                System::Duration timeout_ms,
                                bool want_output = true) final;

private:
    explicit AdbInterfaceImpl(Looper* looper,
                              AdbLocator* locator,
                              AdbDaemon* daemon);
    void discoverAdbInstalls();
    void selectAdbPath();

    Looper* mLooper;
    std::unique_ptr<AdbLocator> mLocator;
    std::unique_ptr<AdbDaemon> mDaemon;
    std::string mAutoAdbPath;
    std::string mCustomAdbPath;
    std::string mSerialString;
    std::vector<std::pair<std::string, Optional<int>>> mAvailableAdbInstalls;
    bool mAdbVersionCurrent = false;
};

std::unique_ptr<AdbInterface> AdbInterface::create(Looper* looper) {
    return AdbInterface::Builder().setLooper(looper).build();
}

// A locator that will scan the filesystem for available ADB installs
class AdbLocatorImpl : public AdbLocator {
    std::vector<std::string> availableAdb() override;

    Optional<int> getAdbProtocolVersion(StringView adbPath) override;
};

// Construct the platform path with adb executable, or Nothing if it doesn't
// exist.
static Optional<std::string> platformPath(base::StringView root) {
    if (root.empty()) {
        LOG(VERBOSE) << " no root specified: ";
        return Optional<std::string>();
    }

    auto path = PathUtils::join(root, "platform-tools",
                                PathUtils::toExecutableName("adb"));
    return System::get()->pathCanExec(path) ? base::makeOptional(path)
                                            : base::kNullopt;
}

std::vector<std::string> AdbLocatorImpl::availableAdb() {
    std::vector<std::string> available = {};
    // First try finding ADB by the environment variable.
    auto adb = platformPath(android::ConfigDirs::getSdkRootDirectoryByEnv());
    if (adb) {
        available.push_back(std::move(*adb));
    }

    // Try finding it based on the emulator executable
    adb = platformPath(android::ConfigDirs::getSdkRootDirectoryByPath());
    if (adb) {
        available.push_back(std::move(*adb));
    }

    // See if it is on the path.
    adb = System::get()->which(PathUtils::toExecutableName("adb"));
    if (adb) {
        available.push_back(std::move(*adb));
    }

    LOG(VERBOSE) << "Found: " << available.size() << " adb executables";
    for (const auto& install : available) {
        LOG(VERBOSE) << "Adb: " << install;
    }

    return available;
}

// Gets the reported adb protocol version from the given executable
// This is the last digit in the adb version string.
Optional<int> AdbLocatorImpl::getAdbProtocolVersion(StringView adbPath) {
    const std::vector<std::string> adbVersion = {adbPath, "version"};
    int protocol = 0;
    // Retrieve the adb version.
    const int maxAdbRetrievalTimeMs = 500;
    auto read = System::get()->runCommandWithResult(adbVersion, maxAdbRetrievalTimeMs, nullptr);
    if (!read || sscanf(read->c_str(), "Android Debug Bridge version %*d.%*d.%d",
               &protocol) != 1) {
        LOG(VERBOSE) << "Failed obtain protocol version from " << adbPath;
        return {};
    }
    LOG(VERBOSE) << "Path:" << adbPath << " protocol version: " << protocol;
    return Optional<int>(protocol);
}


std::unique_ptr<AdbInterface> AdbInterface::Builder::build() {
    if (!mLocator) {
        setAdbLocator(new AdbLocatorImpl());
    }
    if (!mDaemon) {
        setAdbDaemon(new AdbDaemonImpl());
    }

    AdbInterface* adb = new AdbInterfaceImpl(mLooper, mLocator.release(),
                                    mDaemon.release());
    return std::unique_ptr<AdbInterface>(adb);
}

// Discovers which adb executables are available.
void AdbInterfaceImpl::discoverAdbInstalls() {
    auto adbs = mLocator->availableAdb();
    for (const auto& install : adbs) {
        mAvailableAdbInstalls.push_back(std::make_pair(
                install, mLocator->getAdbProtocolVersion(install)));
    }
}

const std::string& AdbInterfaceImpl::adbPath() {
    if (!mCustomAdbPath.empty())
        return mCustomAdbPath;

    selectAdbPath();
    return mAutoAdbPath;
}

void AdbInterfaceImpl::selectAdbPath() {
    // Running without ADB! I too like to live on the edge.
    if (mAvailableAdbInstalls.empty()) {
        return;
    }

    Optional<int> adbVersion;
    std::string adbPath;

    // Maybe we can find one matching the current active version.
    // Even if this might be an ancient adb version.
    Optional<int> daemon = mDaemon->getProtocolVersion();
    if (daemon) {
        for (const auto& choice : mAvailableAdbInstalls) {
            std::tie(adbPath, adbVersion) = choice;
            if (daemon == adbVersion) {
                assert(adbVersion); // Clearly true..
                mAdbVersionCurrent = *adbVersion >= kMinAdbProtocol;
                mAutoAdbPath = adbPath;
                return;
            }
        }
    }

    // A locator that will scan the filesystem for available ADB installs
    // Just take the first (likely the one from the SDK, our dir)
    std::tie(mAutoAdbPath, adbVersion) = mAvailableAdbInstalls.front();
    mAdbVersionCurrent = adbVersion ? *adbVersion >= kMinAdbProtocol : false;
}

AdbInterfaceImpl::AdbInterfaceImpl(Looper* looper,
                                   AdbLocator* locator,
                                   AdbDaemon* daemon)
    : mLooper(looper), mLocator(locator), mDaemon(daemon) {
    discoverAdbInstalls();
    selectAdbPath();
}

AdbCommandPtr AdbInterfaceImpl::runAdbCommand(
        const std::vector<std::string>& args,
        ResultCallback&& result_callback,
        System::Duration timeout_ms,
        bool want_output) {
    auto command = std::shared_ptr<AdbCommand>(
            new AdbCommand(mLooper, adbPath(), mSerialString, args, want_output,
                           timeout_ms, std::move(result_callback)));
    command->start();

    return command;
}

AdbCommand::AdbCommand(Looper* looper,
                       const std::string& adb_path,
                       const std::string& serial_string,
                       const std::vector<std::string>& command,
                       bool want_output,
                       System::Duration timeoutMs,
                       ResultCallback&& callback)
    : mLooper(looper),
      mResultCallback(std::move(callback)),
      mOutputFilePath(PathUtils::join(
              System::get()->getTempDir(),
              std::string("adbcommand").append(Uuid::generate().toString()))),
      mWantOutput(want_output),
      mTimeoutMs(timeoutMs) {
    mCommand.push_back(adb_path);

    // TODO: when run headless, the serial string won't be properly
    // initialized, so make a best attempt by using -e. This should be updated
    // when the headless emulator is given an AdbInterface reference.
    if (serial_string.empty()) {
        mCommand.push_back("-e");
    } else {
        mCommand.push_back("-s");
        mCommand.push_back(serial_string);
    }

    mCommand.insert(mCommand.end(), command.begin(), command.end());
}

void AdbCommand::start(int checkTimeoutMs) {
    if (!mTask && !mFinished) {
        auto shared = shared_from_this();
        mTask.reset(new ParallelTask<OptionalAdbCommandResult>(
                mLooper,
                [shared](OptionalAdbCommandResult* result) {
                    shared->taskFunction(result);
                },
                [shared](const OptionalAdbCommandResult& result) {
                    shared->taskDoneFunction(result);
                },
                checkTimeoutMs));
        mTask->start();
    }
}

void AdbCommand::taskDoneFunction(const OptionalAdbCommandResult& result) {
    if (!mCancelled) {
        mResultCallback(result);
    }
    AutoLock lock(mLock);
    mFinished = true;
    mCv.broadcastAndUnlock(&lock);
    // This may invalidate this object and clean it up.
    // DO NOT reference any internal state from this class after this
    // point.
    mTask.reset();
}

bool AdbCommand::wait(System::Duration timeoutMs) {
    if (!mTask) {
        return true;
    }

    AutoLock lock(mLock);
    if (timeoutMs < 0) {
        mCv.wait(&lock, [this]() { return mFinished; });
        return true;
    }

    const auto deadlineUs = System::get()->getUnixTimeUs() + timeoutMs * 1000;
    while (!mFinished && System::get()->getUnixTimeUs() < deadlineUs) {
        mCv.timedWait(&mLock, deadlineUs);
    }

    return mFinished;
}

void AdbCommand::taskFunction(OptionalAdbCommandResult* result) {
    if (mCommand.empty() || mCommand.front().empty()) {
        result->clear();
        return;
    }

    RunOptions output_flag = mWantOutput ? RunOptions::DumpOutputToFile
                                         : RunOptions::HideAllOutput;
    RunOptions run_flags = RunOptions::WaitForCompletion |
                           RunOptions::TerminateOnTimeout | output_flag;
    System::Pid pid;
    System::ProcessExitCode exit_code;

    bool command_ran = System::get()->runCommand(
            mCommand, run_flags, mTimeoutMs, &exit_code, &pid, mOutputFilePath);

    if (command_ran) {
        result->emplace(exit_code,
                        mWantOutput ? mOutputFilePath : std::string());
    }
}

AdbCommandResult::AdbCommandResult(System::ProcessExitCode exitCode,
                                   const std::string& outputName)
    : exit_code(exitCode),
      output(outputName.empty() ? nullptr
                                : new std::ifstream(outputName.c_str())),
      output_name(outputName) {}

AdbCommandResult::~AdbCommandResult() {
    output.reset();
    if (!output_name.empty()) {
        path_delete_file(output_name.c_str());
    }
}

AdbCommandResult::AdbCommandResult(AdbCommandResult&& other)
    : exit_code(other.exit_code),
      output(std::move(other.output)),
      output_name(std::move(other.output_name)) {
    other.output_name.clear();  // make sure |other| won't delete it
}

AdbCommandResult& AdbCommandResult::operator=(AdbCommandResult&& other) {
    exit_code = other.exit_code;
    output = std::move(other.output);
    output_name = std::move(other.output_name);
    other.output_name.clear();
    return *this;
}

}  // namespace emulation
}  // namespace android

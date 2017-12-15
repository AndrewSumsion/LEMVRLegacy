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

#include "android/snapshot/Snapshotter.h"

#include "android/base/memory/LazyInstance.h"
#include "android/base/Stopwatch.h"
#include "android/base/StringFormat.h"
#include "android/crashreport/CrashReporter.h"
#include "android/featurecontrol/FeatureControl.h"
#include "android/metrics/AdbLivenessChecker.h"
#include "android/metrics/MetricsReporter.h"
#include "android/metrics/proto/studio_stats.pb.h"
#include "android/metrics/StudioConfig.h"
#include "android/opengl/emugl_config.h"
#include "android/snapshot/Hierarchy.h"
#include "android/snapshot/Loader.h"
#include "android/snapshot/Quickboot.h"
#include "android/snapshot/Saver.h"
#include "android/snapshot/TextureLoader.h"
#include "android/snapshot/TextureSaver.h"
#include "android/snapshot/interface.h"
#include "android/utils/debug.h"
#include "android/utils/path.h"
#include "android/utils/system.h"

#include <cassert>
#include <utility>

extern "C" {
#include <emmintrin.h>
}

using android::base::LazyInstance;
using android::base::Stopwatch;
using android::base::StringFormat;
using android::base::System;
using android::crashreport::CrashReporter;
using android::metrics::MetricsReporter;
namespace pb = android_studio;

// Inspired by QEMU's bufferzero.c implementation, but simplified for the case
// when checking the whole aligned memory page.
static bool buffer_zero_sse2(const void* buf, int len) {
    buf = __builtin_assume_aligned(buf, 1024);
    __m128i t = _mm_load_si128(static_cast<const __m128i*>(buf));
    auto p = reinterpret_cast<__m128i*>(
            (reinterpret_cast<intptr_t>(buf) + 5 * 16));
    auto e =
            reinterpret_cast<__m128i*>((reinterpret_cast<intptr_t>(buf) + len));
    const __m128i zero = _mm_setzero_si128();

    /* Loop over 16-byte aligned blocks of 64.  */
    do {
        __builtin_prefetch(p);
        t = _mm_cmpeq_epi32(t, zero);
        if (_mm_movemask_epi8(t) != 0xFFFF) {
            return false;
        }
        t = p[-4] | p[-3] | p[-2] | p[-1];
        p += 4;
    } while (p <= e);

    /* Finish the aligned tail.  */
    t |= e[-3];
    t |= e[-2];
    t |= e[-1];
    return _mm_movemask_epi8(_mm_cmpeq_epi32(t, zero)) == 0xFFFF;
}

namespace android {
namespace snapshot {

static const System::Duration kSnapshotCrashThresholdMs = 120000; // 2 minutes

// TODO: implement an optimized SSE4 version and dynamically select it if host
// supports SSE4.
bool isBufferZeroed(const void* ptr, int32_t size) {
    assert((uintptr_t(ptr) & (1024 - 1)) == 0);  // page-aligned
    assert(size >= 1024);  // at least one small page in |size|
    return buffer_zero_sse2(ptr, size);
}

Snapshotter::Snapshotter() = default;

Snapshotter::~Snapshotter() {
    if (mVmOperations.setSnapshotCallbacks) {
        mVmOperations.setSnapshotCallbacks(nullptr, nullptr);
    }
}

static LazyInstance<Snapshotter> sInstance = {};

Snapshotter& Snapshotter::get() {
    return sInstance.get();
}

void Snapshotter::initialize(const QAndroidVmOperations& vmOperations,
                             const QAndroidEmulatorWindowAgent& windowAgent) {
    static const SnapshotCallbacks kCallbacks = {
            // ops
            {
                    // save
                    {// start
                     [](void* opaque, const char* name) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         return snapshot->onStartSaving(name) ? 0 : -1;
                     },
                     // end
                     [](void* opaque, const char* name, int res) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         snapshot->onSavingComplete(name, res);
                     },
                     // quick fail
                     [](void* opaque, const char* name, int res) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         snapshot->onSavingFailed(name, res);
                     }},
                    // load
                    {// start
                     [](void* opaque, const char* name) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         return snapshot->onStartLoading(name) ? 0 : -1;
                     },
                     // end
                     [](void* opaque, const char* name, int res) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         snapshot->onLoadingComplete(name, res);
                     },  // quick fail
                     [](void* opaque, const char* name, int res) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         snapshot->onLoadingFailed(name, res);
                     }},
                    // del
                    {// start
                     [](void* opaque, const char* name) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         return snapshot->onStartDelete(name) ? 0 : -1;
                     },
                     // end
                     [](void* opaque, const char* name, int res) {
                         auto snapshot = static_cast<Snapshotter*>(opaque);
                         snapshot->onDeletingComplete(name, res);
                     },
                     // quick fail
                     [](void*, const char*, int) {}},
            },
            // ramOps
            {// registerBlock
             [](void* opaque, SnapshotOperation operation,
                const RamBlock* block) {
                 auto snapshot = static_cast<Snapshotter*>(opaque);
                 if (operation == SNAPSHOT_LOAD) {
                     snapshot->mLoader->ramLoader().registerBlock(*block);
                 } else if (operation == SNAPSHOT_SAVE) {
                     snapshot->mSaver->ramSaver().registerBlock(*block);
                 }
             },
             // startLoading
             [](void* opaque) {
                 auto snapshot = static_cast<Snapshotter*>(opaque);
                 snapshot->mLoader->ramLoader().start(snapshot->isQuickboot());
                 return snapshot->mLoader->ramLoader().hasError() ? -1 : 0;
             },
             // savePage
             [](void* opaque, int64_t blockOffset, int64_t pageOffset,
                int32_t size) {
                 auto snapshot = static_cast<Snapshotter*>(opaque);
                 snapshot->mSaver->ramSaver().savePage(blockOffset, pageOffset,
                                                       size);
             },
             // savingComplete
             [](void* opaque) {
                 auto snapshot = static_cast<Snapshotter*>(opaque);
                 snapshot->mSaver->ramSaver().join();
                 return snapshot->mSaver->ramSaver().hasError() ? -1 : 0;
             },
             // loadRam
             [](void* opaque, void* hostRamPtr, uint64_t size) {
                 auto snapshot = static_cast<Snapshotter*>(opaque);

                 auto& loader = snapshot->mLoader;
                 if (!loader || loader->status() != OperationStatus::Ok) return;

                 auto& ramLoader = loader->ramLoader();
                 if (ramLoader.onDemandEnabled() &&
                     !ramLoader.onDemandLoadingComplete()) {
                     ramLoader.loadRam(hostRamPtr, size);
                 }
             }}};

    assert(vmOperations.setSnapshotCallbacks);
    mVmOperations = vmOperations;
    mWindowAgent = windowAgent;
    mVmOperations.setSnapshotCallbacks(this, &kCallbacks);
}  // namespace snapshot

static constexpr int kDefaultMessageTimeoutMs = 10000;

static void appendFailedSave(pb::EmulatorSnapshotSaveState state,
                             FailureReason failureReason) {
    MetricsReporter::get().report([state, failureReason](pb::AndroidStudioEvent* event) {
        auto snap = event->mutable_emulator_details()->add_snapshot_saves();
        snap->set_save_state(state);
        snap->set_save_failure_reason((pb::EmulatorSnapshotFailureReason)failureReason);
    });
}

static void appendFailedLoad(pb::EmulatorSnapshotLoadState state,
                             FailureReason failureReason) {
    MetricsReporter::get().report([state, failureReason](pb::AndroidStudioEvent* event) {
        auto snap = event->mutable_emulator_details()->add_snapshot_loads();
        snap->set_load_state(state);
        snap->set_load_failure_reason((pb::EmulatorSnapshotFailureReason)failureReason);
    });
}

OperationStatus Snapshotter::prepareForLoading(const char* name) {
    if (mSaver && mSaver->snapshot().name() == name) {
        mSaver.clear();
    }
    mLoader.emplace(name);
    mLoader->prepare();
    return mLoader->status();
}

OperationStatus Snapshotter::load(bool isQuickboot, const char* name) {
    mLastLoadDuration = android::base::kNullopt;
    mIsQuickboot = isQuickboot;
    Stopwatch sw;
    mVmOperations.snapshotLoad(name, this, nullptr);
    mIsQuickboot = false;
    mLastLoadDuration.emplace(sw.elapsedUs() / 1000);
    mLoadedSnapshotFile = (mLoader->status() == OperationStatus::Ok) ? name : "";
    return mLoader->status();
}

void Snapshotter::callCallbacks(Operation op, Stage stage) {
    for (auto&& cb : mCallbacks) {
        cb(op, stage);
    }
}

void Snapshotter::fillSnapshotMetrics(pb::EmulatorSnapshot* snapshot,
                                      const SnapshotOperationStats& stats) {
    snapshot->set_name(MetricsReporter::get().anonymize(stats.name));
    if (stats.compressedRam) {
        snapshot->set_flags(pb::SNAPSHOT_FLAGS_RAM_COMPRESSED_BIT);
    }
    if (stats.compressedTextures) {
        snapshot->set_flags(pb::SNAPSHOT_FLAGS_TEXTURES_COMPRESSED_BIT);
    }

    snapshot->set_lazy_loaded(stats.onDemandRamEnabled);
    snapshot->set_incrementally_saved(stats.incrementallySaved);

    snapshot->set_size_bytes(int64_t(stats.diskSize +
                                     stats.ramSize +
                                     stats.texturesSize));
    snapshot->set_ram_size_bytes(int64_t(stats.ramSize));
    snapshot->set_textures_size_bytes(int64_t(stats.texturesSize));

    if (stats.forSave) {
        snapshot->set_save_state(
                pb::EmulatorSnapshotSaveState::EMULATOR_SNAPSHOT_SAVE_SUCCEEDED_NORMAL);
        snapshot->set_save_duration_ms(uint64_t(stats.durationMs));
        snapshot->set_ram_save_duration_ms(int64_t(stats.ramDurationMs));
        snapshot->set_textures_save_duration_ms(int64_t(stats.texturesDurationMs));
    } else {
        snapshot->set_load_state(
                pb::EmulatorSnapshotLoadState::EMULATOR_SNAPSHOT_LOAD_SUCCEEDED_NORMAL);
        snapshot->set_load_duration_ms(uint64_t(stats.durationMs));
        snapshot->set_ram_load_duration_ms(int64_t(stats.ramDurationMs));
        snapshot->set_textures_load_duration_ms(int64_t(stats.texturesDurationMs));
    }
}

Snapshotter::SnapshotOperationStats Snapshotter::getSaveStats(const char* name,
                                                              System::Duration durationMs) {
    auto& save = saver();
    const auto compressedRam = save.ramSaver().compressed();
    const auto compressedTextures = save.textureSaver()->compressed();
    const auto diskSize = save.snapshot().diskSize();
    const auto ramSize = save.ramSaver().diskSize();
    const auto texturesSize = save.textureSaver()->diskSize();

    System::Duration ramDurationMs = 0;
    System::Duration texturesDurationMs = 0;
    save.ramSaver().getDuration(&ramDurationMs); ramDurationMs /= 1000;
    save.textureSaver()->getDuration(&texturesDurationMs); texturesDurationMs /= 1000;

    return {
        true /* for save */,
        std::string(name),
        durationMs,
        false /* on-demand ram loading N/A for save */,
        false /* not incrementally saved */,
        compressedRam,
        compressedTextures,
        (int64_t)diskSize,
        (int64_t)ramSize,
        (int64_t)texturesSize,
        ramDurationMs,
        texturesDurationMs,
    };
}

Snapshotter::SnapshotOperationStats Snapshotter::getLoadStats(const char* name,
                                                              System::Duration durationMs) {
    auto& load = loader();
    const auto onDemandRamEnabled = load.ramLoader().onDemandEnabled();
    const auto compressedRam = load.ramLoader().compressed();
    const auto compressedTextures = load.textureLoader()->compressed();
    const auto diskSize = load.snapshot().diskSize();
    const auto ramSize = load.ramLoader().diskSize();
    const auto texturesSize = load.textureLoader()->diskSize();
    System::Duration ramDurationMs = 0;
    load.ramLoader().getDuration(&ramDurationMs);
    ramDurationMs /= 1000;
    return {
        false /* not for save */,
        name,
        durationMs,
        onDemandRamEnabled,
        false /* not incrementally saved */,
        compressedRam,
        compressedTextures,
        (int64_t)diskSize,
        (int64_t)ramSize,
        (int64_t)texturesSize,
        ramDurationMs,
        0 /* TODO: texture lazy/bg load duration */,
    };
}


void Snapshotter::appendSuccessfulSave(const char* name,
                                       System::Duration durationMs) {
    auto stats = getSaveStats(name, durationMs);
    MetricsReporter::get().report([stats](pb::AndroidStudioEvent* event) {
        auto snapshot = event->mutable_emulator_details()->add_snapshot_saves();
        fillSnapshotMetrics(snapshot, stats);
    });
}

void Snapshotter::appendSuccessfulLoad(const char* name,
                                       System::Duration durationMs) {
    loader().reportSuccessful();
    auto stats = getLoadStats(name, durationMs);
    MetricsReporter::get().report([stats](pb::AndroidStudioEvent* event) {
        auto snapshot = event->mutable_emulator_details()->add_snapshot_loads();
        fillSnapshotMetrics(snapshot, stats);
    });
}

void Snapshotter::showError(const std::string& message) {
    mWindowAgent.showMessage(message.c_str(), WINDOW_MESSAGE_ERROR,
                             kDefaultMessageTimeoutMs);
    dwarning(message.c_str());
}

bool Snapshotter::checkSafeToSave(const char* name, bool reportMetrics) {
    const bool shouldTrySaving =
        metrics::AdbLivenessChecker::isEmulatorBooted();

    if (!shouldTrySaving) {
        showError("Skipping snapshot save: "
                  "Emulator not booted (or ADB not online)");
        if (reportMetrics) {
            appendFailedSave(
                pb::EmulatorSnapshotSaveState::
                    EMULATOR_SNAPSHOT_SAVE_SKIPPED_NOT_BOOTED,
                FailureReason::AdbOffline);
        }
        return false;
    }

    if (!name) {
        showError("Skipping snapshot save: "
                  "Null snapshot name");
        if (reportMetrics) {
            appendFailedSave(
                pb::EmulatorSnapshotSaveState::
                    EMULATOR_SNAPSHOT_SAVE_SKIPPED_NO_SNAPSHOT,
                FailureReason::NoSnapshotPb);
        }
        return false;
    }

    if (!emuglConfig_current_renderer_supports_snapshot()) {
        showError(
            StringFormat("Skipping snapshot save: "
                         "Renderer type '%s' (%d) "
                         "doesn't support snapshotting",
                         emuglConfig_renderer_to_string(
                                 emuglConfig_get_current_renderer()),
                         int(emuglConfig_get_current_renderer())));
        if (reportMetrics) {
            appendFailedSave(pb::EmulatorSnapshotSaveState::
                                 EMULATOR_SNAPSHOT_SAVE_SKIPPED_UNSUPPORTED,
                             FailureReason::SnapshotsNotSupported);
        }
        return false;
    }

    return true;
}

bool Snapshotter::checkSafeToLoad(const char* name, bool reportMetrics) {
    if (!name) {
        showError("Skipping snapshot load: "
                  "Null snapshot name");
        if (reportMetrics) {
            appendFailedLoad(pb::EmulatorSnapshotLoadState::
                                 EMULATOR_SNAPSHOT_LOAD_NO_SNAPSHOT,
                             FailureReason::NoSnapshotPb);
        }
        return false;
    }

    if (!emuglConfig_current_renderer_supports_snapshot()) {
        showError(
            StringFormat("Skipping snapshot load of '%s': "
                         "Renderer type '%s' (%d) "
                         "doesn't support snapshotting",
                         name,
                         emuglConfig_renderer_to_string(
                                 emuglConfig_get_current_renderer()),
                         int(emuglConfig_get_current_renderer())));
        if (reportMetrics) {
            appendFailedLoad(pb::EmulatorSnapshotLoadState::
                                 EMULATOR_SNAPSHOT_LOAD_SKIPPED_UNSUPPORTED,
                             FailureReason::SnapshotsNotSupported);
        }
        return false;
    }
    return true;
}

void Snapshotter::handleGenericSave(const char* name,
                                    OperationStatus saveStatus,
                                    bool reportMetrics) {
    if (saveStatus != OperationStatus::Ok) {
        showError(
            StringFormat(
                "Snapshot save for snapshot '%s' failed. "
                "Cleaning it out", name));
        deleteSnapshot(name);
        if (reportMetrics) {
            if (auto failureReason = saver().snapshot().failureReason()) {
                appendFailedSave(pb::EmulatorSnapshotSaveState::
                                     EMULATOR_SNAPSHOT_SAVE_FAILED,
                                 *failureReason);
            } else {
                appendFailedSave(pb::EmulatorSnapshotSaveState::
                                     EMULATOR_SNAPSHOT_SAVE_FAILED,
                                 FailureReason::InternalError);
            }
        }
    } else {
        if (reportMetrics) {
            appendSuccessfulSave(name,
                                 mLastSaveDuration ? *mLastSaveDuration : 1234);
        }
    }
}

void Snapshotter::handleGenericLoad(const char* name,
                                    OperationStatus loadStatus,
                                    bool reportMetrics) {
    if (loadStatus != OperationStatus::Ok) {
        // Check if the error is about something done as just a check or
        // we've started actually loading the VM data
        if (auto failureReason = loader().snapshot().failureReason()) {
            if (reportMetrics) {
                appendFailedLoad(pb::EmulatorSnapshotLoadState::
                                     EMULATOR_SNAPSHOT_LOAD_FAILED,
                                 *failureReason);
            }
            if (*failureReason != FailureReason::Empty &&
                *failureReason < FailureReason::ValidationErrorLimit) {
                showError(
                    StringFormat(
                        "Snapshot '%s' can not be loaded (%d). "
                        "Continuing current session",
                        name, int(*failureReason)));
            } else {
                showError(
                    StringFormat(
                        "Snapshot '%s' can not be loaded (%d). "
                        "Fatal error, resetting current session",
                        name, int(*failureReason)));
                mVmOperations.vmReset();
            }
        } else {
            showError(
                StringFormat(
                    "Snapshot '%s' can not be loaded (reason not set). "
                    "Fatal error, resetting current session", name));
            mVmOperations.vmReset();
            if (reportMetrics) {
                appendFailedLoad(pb::EmulatorSnapshotLoadState::
                                     EMULATOR_SNAPSHOT_LOAD_FAILED,
                                 FailureReason::InternalError);
            }
        }
    }

    if (reportMetrics) {
        appendSuccessfulLoad(name,
                             mLastLoadDuration ? *mLastLoadDuration : 0);
    }
}

OperationStatus Snapshotter::prepareForSaving(const char* name) {
    if (mLoader && mLoader->snapshot().name() == name) {
        mLoader.clear();
    }
    mSaver.emplace(name);
    mSaver->prepare();
    return mSaver->status();
}

OperationStatus Snapshotter::save(const char* name) {
    mLastSaveDuration = android::base::kNullopt;
    mLastSaveUptimeMs =
        System::Duration(System::get()->getProcessTimes().wallClockMs);
    Stopwatch sw;
    mVmOperations.snapshotSave(name, this, nullptr);
    mLastSaveDuration.emplace(sw.elapsedUs() / 1000);
    return mSaver->status();
}

OperationStatus Snapshotter::saveGeneric(const char* name) {
    OperationStatus res = OperationStatus::Error;
    if (checkSafeToSave(name)) {
        res = save(name);
        handleGenericSave(name, res);
    }
    return res;
}

OperationStatus Snapshotter::loadGeneric(const char* name) {
    CrashReporter::get()->addCrashCallback([this, &name]() {
        Snapshotter::get().onCrashedSnapshot(name);
    });
    OperationStatus res = OperationStatus::Error;
    if (checkSafeToLoad(name)) {
        res = load(false /* not quickboot */, name);
        handleGenericLoad(name, res);
    }
    return res;
}

void Snapshotter::deleteSnapshot(const char* name) {
    if (!strcmp(name, mLoadedSnapshotFile.c_str())) {
        // We're deleting the "loaded" snapshot
        mLoadedSnapshotFile.clear();
    }
    mVmOperations.snapshotDelete(name, this, nullptr);
}

void Snapshotter::onCrashedSnapshot(const char* name) {
    // if it's been less than 2 minutes since the load,
    // consider it a snapshot fail.
    if (System::Duration(System::get()->getProcessTimes().wallClockMs) -
        mLastLoadUptimeMs < kSnapshotCrashThresholdMs) {
        onLoadingFailed(name, -EINVAL);
    }
}

bool Snapshotter::onStartSaving(const char* name) {
    CrashReporter::get()->hangDetector().pause(true);
    callCallbacks(Operation::Save, Stage::Start);
    mLoader.clear();
    if (!mSaver || isComplete(*mSaver)) {
        mSaver.emplace(name);
    }
    if (mSaver->status() == OperationStatus::Error) {
        onSavingComplete(name, -1);
        return false;
    }
    return true;
}

bool Snapshotter::onSavingComplete(const char* name, int res) {
    assert(mSaver && name == mSaver->snapshot().name());
    mSaver->complete(res == 0);
    CrashReporter::get()->hangDetector().pause(false);
    callCallbacks(Operation::Save, Stage::End);
    bool good = mSaver->status() != OperationStatus::Error;
    if (good) {
        Hierarchy::get()->currentInfo();
    }
    return good;
}

void Snapshotter::onSavingFailed(const char* name, int res) {
    // Well, we haven't started anything and it failed already - nothing to do.
}

bool Snapshotter::onStartLoading(const char* name) {
    mLoadedSnapshotFile.clear();
    CrashReporter::get()->hangDetector().pause(true);
    callCallbacks(Operation::Load, Stage::Start);
    mSaver.clear();
    if (!mLoader || isComplete(*mLoader)) {
        if (mLoader) {
            mLoader->interrupt();
        }
        mLoader.emplace(name);
    }
    mLoader->start();
    if (mLoader->status() == OperationStatus::Error) {
        onLoadingComplete(name, -1);
        return false;
    }
    return true;
}

bool Snapshotter::onLoadingComplete(const char* name, int res) {
    assert(mLoader && name == mLoader->snapshot().name());
    mLoader->complete(res == 0);
    CrashReporter::get()->hangDetector().pause(false);
    mLastLoadUptimeMs =
            System::Duration(System::get()->getProcessTimes().wallClockMs);
    callCallbacks(Operation::Load, Stage::End);
    if (mLoader->status() == OperationStatus::Error) {
        return false;
    }
    mLoadedSnapshotFile = name;
    Hierarchy::get()->currentInfo();
    return true;
}

void Snapshotter::onLoadingFailed(const char* name, int err) {
    assert(err < 0);
    mSaver.clear();
    if (err == -EINVAL) { // corrupted snapshot. abort immediately,
                          // try not to do anything since this could be
                          // in the crash handler
        mLoader->onInvalidSnapshotLoad();
        return;
    }
    mLoader.emplace(name, -err);
    mLoader->complete(false);
    mLoadedSnapshotFile.clear();
}

bool Snapshotter::onStartDelete(const char*) {
    CrashReporter::get()->hangDetector().pause(true);
    return true;
}

bool Snapshotter::onDeletingComplete(const char* name, int res) {
    if (res == 0) {
        if (mSaver && mSaver->snapshot().name() == name) {
            mSaver.clear();
        }
        if (mLoader && mLoader->snapshot().name() == name) {
            mLoader.clear();
        }
        path_delete_dir(Snapshot::dataDir(name).c_str());
    }
    CrashReporter::get()->hangDetector().pause(false);
    return true;
}

void Snapshotter::addOperationCallback(Callback&& cb) {
    if (cb) {
        mCallbacks.emplace_back(std::move(cb));
    }
}

}  // namespace snapshot
}  // namespace android

void androidSnapshot_initialize(
        const QAndroidVmOperations* vmOperations,
        const QAndroidEmulatorWindowAgent* windowAgent) {
    using android::base::Version;

    static constexpr auto kMinStudioVersion = Version(3, 0, 0);
    // Make sure the installed AndroidStudio is able to handle the snapshots
    // feature.
    if (isEnabled(android::featurecontrol::FastSnapshotV1)) {
        auto version = android::studio::lastestAndroidStudioVersion();
        if (version.isValid() && version < kMinStudioVersion) {
            auto prettyVersion = Version(version.component<Version::kMajor>(),
                                         version.component<Version::kMinor>(),
                                         version.component<Version::kMicro>());
            VERBOSE_PRINT(init,
                          "Disabling snapshot boot - need Android Studio %s "
                          " but found %s",
                          kMinStudioVersion.toString().c_str(),
                          prettyVersion.toString().c_str());
            setEnabledOverride(android::featurecontrol::FastSnapshotV1, false);
        }
    }

    android::snapshot::sInstance->initialize(*vmOperations, *windowAgent);
    android::snapshot::Quickboot::initialize(*vmOperations, *windowAgent);
}

void androidSnapshot_finalize() {
    android::snapshot::Quickboot::finalize();
    android::snapshot::sInstance->~Snapshotter();
}

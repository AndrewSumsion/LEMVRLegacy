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

#pragma once

#include "android/base/Compiler.h"
#include "android/base/Optional.h"
#include "android/snapshot/common.h"
#include "android/snapshot/RamLoader.h"
#include "android/snapshot/Snapshot.h"

namespace android {
namespace snapshot {

class Loader {
    DISALLOW_COPY_AND_ASSIGN(Loader);

public:
    Loader(const Snapshot& snapshot);
    ~Loader();

    RamLoader& ramLoader() { return *mRamLoader; }
    const TextureLoaderPtr& textureLoader() const { return mTextureLoader; }

    OperationStatus status() const { return mStatus; }
    const Snapshot& snapshot() const { return mSnapshot; }

    void prepare();
    void start();
    void complete(bool succeeded);

private:
    OperationStatus mStatus;
    Snapshot mSnapshot;
    base::Optional<RamLoader> mRamLoader;
    TextureLoaderPtr mTextureLoader;
};

}  // namespace snapshot
}  // namespace android

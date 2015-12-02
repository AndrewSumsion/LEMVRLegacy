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

#include "android/base/threads/Thread.h"

#include <functional>

// FunctorThread class is an implementation of base Thread interface that
// allows one to run a function object in separate thread. It's mostly a
// convenience class so one doesn't need to create a separate class if the only
// needed thing is to run a specific existing function in a thread.

namespace android {
namespace base {

class FunctorThread : public android::base::Thread {
public:
    using Functor = std::function<intptr_t()>;

    explicit FunctorThread(const Functor& func);

private:
    virtual intptr_t main() override;

private:
    Functor mThreadFunc;
};

}  // namespace base
}  // namespace android

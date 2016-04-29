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
#pragma once

#include "OpenglRender/RenderChannel.h"
#include "RendererImpl.h"

#include "android/base/Compiler.h"
#include "android/base/synchronization/MessageChannel.h"

#include <memory>

namespace emugl {

class RenderChannelImpl final : public RenderChannel {
public:
    RenderChannelImpl(std::shared_ptr<RendererImpl> renderer);

public:
    // RenderChannel implementation, operations provided for a guest system
    virtual void setEventCallback(EventCallback callback) override final;

    virtual bool write(ChannelBuffer&& buffer) override final;
    virtual bool read(ChannelBuffer* buffer, CallType type) override final;

    virtual State currentState() const override final { return mState; }

    virtual void stop() override final;
    virtual bool isStopped() const override final;

public:
    // These functions are for the RenderThread, they could be called in
    // parallel with the ones from the RenderChannel interface. Make sure the
    // internal state remains consistent all the time.
    void writeToGuest(ChannelBuffer&& buf);
    size_t readFromGuest(ChannelBuffer::value_type* buf, size_t size,
                         bool blocking);
    void forceStop();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(RenderChannelImpl);

private:
    void onEvent(bool byGuest);
    State calcState() const;
    void stop(bool byGuest);

private:
    std::shared_ptr<RendererImpl> mRenderer;

    EventCallback mOnEvent;
    State mState = State::Empty;
    bool mStopped = false;

    static const size_t kChannelCapacity = 256;
    android::base::MessageChannel<ChannelBuffer, kChannelCapacity> mFromGuest;
    android::base::MessageChannel<ChannelBuffer, kChannelCapacity> mToGuest;

    ChannelBuffer mFromGuestBuffer;
    size_t mFromGuestBufferLeft = 0;
};

}  // namespace emugl

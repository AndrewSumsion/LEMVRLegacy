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
#include "BufferQueue.h"
#include "RendererImpl.h"

namespace emugl {

// Implementation of the RenderChannel interface that connects a guest
// client thread (really an AndroidPipe implementation) to a host
// RenderThread instance.
class RenderChannelImpl final : public RenderChannel {
public:
    // Default constructor.
    RenderChannelImpl();

    /////////////////////////////////////////////////////////////////
    // RenderChannel overriden methods. These are called from the guest
    // client thread.

    // Set the event |callback| to be notified when the host changes the
    // state of the channel, according to the event mask provided by
    // setWantedEvents(). Call this function right after creating the
    // instance.
    virtual void setEventCallback(EventCallback&& callback) override final;

    // Set the mask of events the guest wants to be notified of from the
    // host thread.
    virtual void setWantedEvents(State state) override final;

    // Return the current channel state relative to the guest.
    virtual State state() const override final;

    // Try to send a buffer from the guest to the host render thread.
    virtual IoResult tryWrite(Buffer&& buffer) override final;

    // Try to read a buffer from the host render thread into the guest.
    virtual IoResult tryRead(Buffer* buffer) override final;

    // Close the channel from the guest.
    virtual void stop() override final;

    /////////////////////////////////////////////////////////////////
    // These functions are called from the host render thread.

    // Send a buffer to the guest, this call is blocking. On success,
    // move |buffer| into the channel and return true. On failure, return
    // false (meaning that the channel was closed).
    bool writeToGuest(Buffer&& buffer);

    // Read data from the guest. If |blocking| is true, the call will be
    // blocking. On success, move item into |*buffer| and return true. On
    // failure, return IoResult::Error to indicate the channel was closed,
    // or IoResult::TryAgain to indicate it was empty (this can happen only
    // if |blocking| is false).
    IoResult readFromGuest(Buffer* buffer, bool blocking);

    // Close the channel from the host.
    void stopFromHost();

private:
    void updateStateLocked();
    void notifyStateChangeLocked();

    EventCallback mEventCallback;

    // A single lock to protect the state and the two buffer queues at the
    // same time. NOTE: This needs to appear before the BufferQueue instances.
    mutable android::base::Lock mLock;
    State mState = State::Empty;
    State mWantedEvents = State::Empty;
    BufferQueue mFromGuest;
    BufferQueue mToGuest;
};

}  // namespace emugl

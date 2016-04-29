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

#include "android/base/EnumFlags.h"

#include <functional>
#include <memory>
#include <vector>

namespace emugl {

// Turn the RenderChannel::Event enum into flags.
using namespace ::android::base::EnumFlags;

// A type used for data passing.
// TODO(zyy): add a small buffer optimization here to pass 4 and 8-byte chunks
// without heap allocation (those are _very_ common).
using ChannelBuffer = std::vector<char>;

// RenderChannel - an interface for a single guest to host renderer connection.
// It allows the guest to send GPU emulation protocol-serialized messages to an
// asynchronous renderer, read the responses and subscribe for state updates.
class RenderChannel {
public:
    // Flags for the channel state.
    enum class State {
        // Can't use None here, some system header declares it as a macro.
        Empty = 0,
        CanRead = 1 << 0,
        CanWrite = 1 << 1,
        Stopped = 1 << 2,
    };

    // Possible points of origin for an event in EventCallback.
    enum class EventSource {
        RenderChannel,
        Client,
    };

    // Types of read() the channel supports.
    enum class CallType {
        Blocking,    // if the call can't do what it needs, block until it can
        Nonblocking, // immidiately return if the call can't do the job
    };

    // Sets a single (!) callback that is called if some event happends that
    // changes the channel state - e.g. when it's stopped, or it gets some data
    // the client can read after being empty, or it isn't full anymore and the
    // client may write again without blocking.
    // If the state isn't State::Empty, the |callback| is called for the first
    // time during the setEventCallback() to report this initial state.
    using EventCallback = std::function<void(State, EventSource)>;
    virtual void setEventCallback(EventCallback callback) = 0;

    // Writes the data in |buffer| into the channel. |buffer| is moved from.
    // Blocks if there's no room in the channel (shouldn't really happen).
    // Returns false if the channel is stopped.
    virtual bool write(ChannelBuffer&& buffer) = 0;
    // Reads a chunk of data from the channel. Returns false if there was no
    // data for a non-blocking call or if the channel is stopped.
    virtual bool read(ChannelBuffer* buffer, CallType callType) = 0;

    // Get the current state flags.
    virtual State currentState() const = 0;

    // Abort all pending operations. Any following operation is a noop.
    virtual void stop() = 0;
    // Check if the channel is stopped.
    virtual bool isStopped() const = 0;

protected:
    ~RenderChannel() = default;
};

using RenderChannelPtr = std::shared_ptr<RenderChannel>;

}  // namespace emugl

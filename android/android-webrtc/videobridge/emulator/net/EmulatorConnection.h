// Copyright (C) 2019 The Android Open Source Project
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
#include <map>
#include <memory>
#include <string>

#include "emulator/webrtc/Switchboard.h"
#include "rtc_base/asyncsocket.h"
#include "rtc_base/physicalsocketserver.h"

namespace emulator {
namespace webrtc {
class Switchboard;
}
namespace net {

using rtc::AsyncSocket;
using webrtc::Switchboard;

// Binds to a port and listens for incoming emulator connections.
class EmulatorConnection : public sigslot::has_slots<> {
public:
    EmulatorConnection(int port, std::string handle, std::string turnconfig);
    ~EmulatorConnection();

    bool listen(bool fork);
    void disconnect(Switchboard* disconnect);

private:
    void OnRead(AsyncSocket* socket);
    void OnClose(AsyncSocket* socket, int err);
    void OnConnect(AsyncSocket* socket);

    rtc::PhysicalSocketServer mSocketServer;
    std::unique_ptr<AsyncSocket> mServerSocket;
    std::vector<std::unique_ptr<Switchboard>> mConnections;
    std::unique_ptr<rtc::AutoSocketServerThread> mThread;
    std::unique_ptr<rtc::AsyncSocket> mSocket;
    std::string mHandle;
    std::string mTurnConfig;
    int mPort;
};
}  // namespace net
}  // namespace emulator
// Copyright (C) 2020 The Android Open Source Project
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

#include <grpcpp/grpcpp.h>                        // for ClientContext
#include <cstdint>                                // for uint8_t
#include <string>                                 // for string
#include <thread>                                 // for thread
#include <vector>                                 // for vector

#include "emulator/net/EmulatorGrcpClient.h"      // for EmulatorGrpcClient
#include "emulator/webrtc/capture/AudioSource.h"  // for AudioSource

namespace android {
namespace emulation {
namespace control {
class AudioPacket;
}  // namespace control
}  // namespace emulation
}  // namespace android

namespace emulator {
namespace webrtc {

class GrpcAudioSource : public AudioSource {
public:
    explicit GrpcAudioSource(std::string discovery_file);

    ~GrpcAudioSource();

private:
    // Listens for available audio packets from the Android Emulator.
    void StreamAudio();

    // Consumes a single audio packet received from the Android Emulator.
    void ConsumeAudioPacket(
            const ::android::emulation::control::AudioPacket& audio_packet);

    std::vector<uint8_t> mPartialFrame;
    EmulatorGrpcClient mClient;
    grpc::ClientContext mContext;
    std::thread mAudioThread;
    bool mCaptureAudio{true};
};

}  // namespace webrtc
}  // namespace emulator

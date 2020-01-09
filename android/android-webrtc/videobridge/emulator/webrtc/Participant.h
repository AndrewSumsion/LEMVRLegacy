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

#include <api/jsep.h>
#include <api/peerconnectioninterface.h>
#include <emulator/net/SocketTransport.h>

#include "Switchboard.h"
#include "nlohmann/json.hpp"

using cricket::VideoCapturer;
using json = nlohmann::json;
using rtc::scoped_refptr;
using webrtc::PeerConnectionInterface;
using webrtc::VideoTrackInterface;

namespace emulator {
namespace webrtc {

class Switchboard;

// A default peer connection observer that does nothing
class EmptyConnectionObserver : public ::webrtc::PeerConnectionObserver {
public:
    void OnSignalingChange(::webrtc::PeerConnectionInterface::SignalingState
                                   new_state) override {}
    void OnAddTrack(rtc::scoped_refptr<::webrtc::RtpReceiverInterface> receiver,
                    const std::vector<
                            rtc::scoped_refptr<::webrtc::MediaStreamInterface>>&
                            streams) override {}
    void OnRemoveTrack(rtc::scoped_refptr<::webrtc::RtpReceiverInterface>
                               receiver) override {}
    void OnDataChannel(rtc::scoped_refptr<::webrtc::DataChannelInterface>
                               channel) override {}
    void OnRenegotiationNeeded() override {}
    void OnIceConnectionChange(
            ::webrtc::PeerConnectionInterface::IceConnectionState new_state)
            override {}
    void OnIceGatheringChange(
            ::webrtc::PeerConnectionInterface::IceGatheringState new_state)
            override {}
    void OnIceCandidate(
            const ::webrtc::IceCandidateInterface* candidate) override {}
    void OnIceConnectionReceivingChange(bool receiving) override {}
};

// A Participant in an webrtc streaming session. This class is
// repsonsbile for driving the jsep protocol. It basically:
//
// 1. Creates the audio & video streams with shared mem handle & fps.
// 2. Do network discovery (ice etc).
// 3. Exchanging of offers between remote client.
//
// It talks with the switchboard to send/receive messages.
class Participant : public EmptyConnectionObserver,
                    public ::webrtc::CreateSessionDescriptionObserver {
public:
    Participant(Switchboard* board,
                std::string id,
                std::string mem_handle,
                int desiredFps);
    ~Participant() override;

    // PeerConnectionObserver implementation.
    void OnIceCandidate(
            const ::webrtc::IceCandidateInterface* candidate) override;

    // CreateSessionDescriptionObserver implementation.
    void OnSuccess(::webrtc::SessionDescriptionInterface* desc) override;
    // The OnFailure callback takes an RTCError, which consists of an¶
    // error code and a string.¶
    // RTCError is non-copyable, so it must be passed using std::move.¶
    // Earlier versions of the API used a string argument. This version¶
    // is deprecated; in order to let clients remove the old version, it has a¶
    // default implementation. If both versions are unimplemented, the¶
    // result will be a runtime error (stack overflow). This is intentional.¶
    void OnFailure(::webrtc::RTCError error) override;
    void OnFailure(const std::string& error) override;
    void OnIceConnectionChange(
            ::webrtc::PeerConnectionInterface::IceConnectionState new_state)
            override;
    void IncomingMessage(json msg);
    bool Initialize();
    inline const std::string GetPeerId() const { return mPeerId; };

private:
    void SendMessage(json msg);
    void HandleOffer(const json& msg) const;
    void HandleCandidate(const json& msg) const;
    void DeletePeerConnection();
    bool AddStreams();
    bool CreatePeerConnection(bool dtls);
    VideoCapturer* OpenVideoCaptureDevice();

    scoped_refptr<PeerConnectionInterface> peer_connection_;
    scoped_refptr<::webrtc::PeerConnectionFactoryInterface>
            peer_connection_factory_;
    std::map<std::string, scoped_refptr<::webrtc::MediaStreamInterface>>
            active_streams_;

    Switchboard* mSwitchboard;
    std::string mPeerId;
    std::string mMemoryHandle;
    uint32_t mFps = 24;

    const std::string kStunUri = "stun:stun.l.google.com:19302";
    const std::string kAudioLabel = "emulator_audio_stream";
    const std::string kVideoLabel = "emulator_video_stream";
    const std::string kStreamLabel = "emulator_view";
};
}  // namespace webrtc
}  // namespace emulator

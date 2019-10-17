#include "android/emulation/control/GrpcServices.h"

#include "android/base/files/PathUtils.h"
#include "android/emulation/ConfigDirs.h"
#include "android/emulation/control/EmulatorService.h"  // for EmulatorContr...
#include "android/emulation/control/RtcBridge.h"

#ifdef ANDROID_WEBRTC
#include "android/emulation/control/WebRtcBridge.h"
#endif

namespace android {
namespace emulation {
namespace control {
class RtcBridge;
}  // namespace control
}  // namespace emulation
}  // namespace android

using android::emulation::control::EmulatorControllerService;
using android::emulation::control::GrpcServices;
using android::emulation::control::NopRtcBridge;
using android::emulation::control::RtcBridge;

const std::string GrpcServices::kCertFileName{"emulator-grpc.cer"};
const std::string GrpcServices::kPrivateKeyFileName{"emulator-grpc.key"};

std::unique_ptr<EmulatorControllerService> GrpcServices::g_controler_service =
        nullptr;
std::unique_ptr<RtcBridge> GrpcServices::g_rtc_bridge = nullptr;

int GrpcServices::setup(int port,
                        const AndroidConsoleAgents* const consoleAgents,
                        const char* turnCfg) {
    // Return the active port if we are already running.
    if (g_controler_service) {
        return g_controler_service->port();
    }

    std::string turn = turnCfg ? std::string(turnCfg) : "";
#ifdef ANDROID_WEBRTC
    g_rtc_bridge.reset(android::emulation::control::WebRtcBridge::create(
            port + 1, consoleAgents, turn));
#else
    g_rtc_bridge = std::make_unique<NopRtcBridge>();
#endif
    g_rtc_bridge->start();

    g_controler_service =
            EmulatorControllerService::Builder()
                    .withConsoleAgents(consoleAgents)
                    .withCertAndKey(
                            base::PathUtils::join(
                                    android::ConfigDirs::getUserDirectory(),
                                    kCertFileName),
                            base::PathUtils::join(
                                    android::ConfigDirs::getUserDirectory(),
                                    kPrivateKeyFileName))
                    .withPort(port)
                    .withRtcBridge(g_rtc_bridge.get())
                    .build();

    return (g_controler_service) ? port : -1;
}

void GrpcServices::teardown() {
    if (g_controler_service) {
        g_controler_service->stop();
    }
}

#include "LemvrMain.h"

#include "TextureCompat.h"

#include "FrameBuffer.h"
#include "emugl/common/thread.h"
#include "GLcommon/GLutils.h"

#include "openvr.h"

#include <iostream>
#include <stdint.h>

namespace lemvr {

LemvrApplication::LemvrApplication()
    : poses(NULL),
      hmd(NULL),
      error(0) {
    if(!vr::VR_IsHmdPresent()) {
        std::cerr << "Error: No HMD detected\n";
        error = 1;
        return;
    }

    if(!vr::VR_IsRuntimeInstalled()) {
        std::cerr << "Error: OpenVR runtime not detected\n";
        error = 1;
        return;
    }

    vr::EVRInitError err = vr::VRInitError_None;
	hmd = vr::VR_Init(&err, vr::VRApplication_Scene);

    if(err != vr::VRInitError_None) {
        std::cerr << "Error: " << vr::VR_GetVRInitErrorAsEnglishDescription(err) << std::endl;
        error = 1;
        return;
    }

    poses = new vr::TrackedDevicePose_t[vr::k_unMaxTrackedDeviceCount];
    waitGetPoses();
}

LemvrApplication::~LemvrApplication() {
    if(hmd) {
        vr::VR_Shutdown();
        hmd = NULL;
        delete [] poses;
    }
}

void LemvrApplication::submitFrame(GLuint texture) {
    unsigned int globalTexture = getGlobalTextureName(texture);

    std::cout << texture << "\t" << globalTexture << std::endl;

    vr::Texture_t vrTexture = {(void*)(uintptr_t)globalTexture, vr::TextureType_OpenGL, vr::ColorSpace_Gamma};

    vr::VRTextureBounds_t leftBounds;
    leftBounds.uMin = 0;
    leftBounds.uMax = 0.5;
    leftBounds.vMin = 1; // Fix VR displaying upside down
    leftBounds.vMax = 0;

    vr::VRTextureBounds_t rightBounds;
    rightBounds.uMin = 0.5;
    rightBounds.uMax = 1;
    rightBounds.vMin = 1;
    rightBounds.vMax = 0;

    vr::EVRCompositorError errorL = vr::VRCompositor()->Submit(vr::Eye_Left, &vrTexture, &leftBounds, vr::Submit_Default);
    vr::EVRCompositorError errorR = vr::VRCompositor()->Submit(vr::Eye_Right, &vrTexture, &rightBounds, vr::Submit_Default);

    if(errorL != 0) {
        std::cerr << "Left  Eye Error: " << errorL << std::endl;
    }

    if(errorR != 0) {
        std::cerr << "Right Eye Error: " << errorR << std::endl;
    }

    waitGetPoses();
}

void LemvrApplication::waitGetPoses() {
    vr::VRCompositor()->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0);
}

LemvrApplication* vrApp;

void lemvrMain() {
    vrApp = new LemvrApplication();

    if(vrApp->error != 0) {
        return;
    }

    std::cout << "OpenVR Initialized!\n";
}

LemvrApplication* getVrApp() {
    return vrApp;
}

void shutdown() {
    delete vrApp;
}

}
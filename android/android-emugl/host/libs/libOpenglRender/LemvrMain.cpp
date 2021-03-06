/*
* Copyright (C) 2021 Andrew Sumsion
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "LemvrMain.h"

#include "TextureCompat.h"

#include "FrameBuffer.h"
#include "GLcommon/GLutils.h"

#include "openvr.h"

#include <iostream>
#include <stdint.h>

namespace lemvr {

class ServerInitThread : public emugl::Thread {
public:
    LemvrServer* server;

    ServerInitThread(LemvrServer* server) : Thread() {
        this->server = server;
    }

    virtual intptr_t main() {
        int err = server->waitForClient();
        if(err != 0) {
            std::cerr << "An error occurred waiting for a client to connect: " << err << std::endl;
            return 1;
        }

        return 0;
    }
};

LemvrApplication::LemvrApplication()
    : poses(nullptr),
      hmd(nullptr),
      server(nullptr),
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
        hmd = NULL;
        error = 1;
        return;
    }

    server = new LemvrServer();
    if(server->startServer(5892) != 0) {
        std::cerr << "Unable to start server at port 5892" << std::endl;
        error = 2;
        return;
    }

    std::cout << "Waiting for client on another thread..." << std::endl;
    serverInitThread = new ServerInitThread(server);
    serverInitThread->start();

    poses = new vr::TrackedDevicePose_t[vr::k_unMaxTrackedDeviceCount];
    waitGetPoses();
}

LemvrApplication::~LemvrApplication() {
    if(hmd) {
        hmd = NULL;
        delete [] poses;
    }
}

void LemvrApplication::shutdown() {
    server->stopServer();
    vr::VR_Shutdown();
}

void LemvrApplication::submitFrame(GLuint texture) {
    if(!hmd) {
        return;
    }
    if(!server->hasClientConnected()) {
        return;
    }

    unsigned int globalTexture = getGlobalTextureName(texture);

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
    if(!server->hasClientConnected()) {
        return;
    }
    int err = server->mainLoop();
    if(err != 0) {
        std::cerr << "An error occured in the server main loop: " << err << std::endl;
    }
}

LemvrApplication* vrApp;

void lemvrMain() {
    socketInit();
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
    vrApp->shutdown();
    delete vrApp;
    socketQuit();
}

}
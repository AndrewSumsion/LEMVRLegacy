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

#pragma once

#include "openvr.h"

#include "LemvrServer.h"
#include "emugl/common/thread.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

namespace lemvr {

class LemvrApplication {
private:
    vr::IVRSystem* hmd;
    vr::TrackedDevicePose_t* poses;
    LemvrServer* server;
    emugl::Thread* serverInitThread;

    void waitGetPoses();

public:
    int error;

    LemvrApplication();
    ~LemvrApplication();

    void shutdown();

    void submitFrame(GLuint texture);

    vr::IVRSystem* getHMD() const { return hmd; }
    vr::TrackedDevicePose_t* getPoses() const { return poses; }
};

LemvrApplication* getVrApp();
void lemvrMain();
void shutdown();

}
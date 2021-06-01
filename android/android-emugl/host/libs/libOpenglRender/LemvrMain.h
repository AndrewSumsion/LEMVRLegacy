#pragma once

#include "openvr.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

namespace lemvr {

class LemvrApplication {
private:
    vr::IVRSystem* hmd;
    vr::TrackedDevicePose_t* poses;

    void waitGetPoses();

public:
    int error;

    LemvrApplication();
    ~LemvrApplication();

    void submitFrame(GLuint texture);
};

LemvrApplication* getVrApp();
void lemvrMain();
void shutdown();

}
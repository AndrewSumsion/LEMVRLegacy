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
#include "RenderLibImpl.h"

#include "RendererImpl.h"

#include "emugl/common/crash_reporter.h"
#include "emugl/common/feature_control.h"
#include "emugl/common/logging.h"
#include "emugl/common/misc.h"
#include "emugl/common/sync_device.h"

namespace emugl {

void RenderLibImpl::setApiLevel(int api) {
    emugl::setApiLevel(api);
}

void RenderLibImpl::setLogger(emugl_logger_struct logger) {
    set_emugl_logger(logger.coarse);
    set_emugl_cxt_logger(logger.fine);
}

void RenderLibImpl::setCrashReporter(emugl_crash_reporter_t reporter) {
    set_emugl_crash_reporter(reporter);
}

void RenderLibImpl::setFeatureController(emugl_feature_is_enabled_t featureController) {
    set_emugl_feature_is_enabled(featureController);
}

void RenderLibImpl::setSyncDevice
    (emugl_sync_create_timeline_t create_timeline,
     emugl_sync_create_fence_t create_fence,
     emugl_sync_timeline_inc_t timeline_inc,
     emugl_sync_destroy_timeline_t destroy_timeline,
     emugl_sync_register_trigger_wait_t register_trigger_wait,
     emugl_sync_device_exists_t device_exists) {
    set_emugl_sync_create_timeline(create_timeline);
    set_emugl_sync_create_fence(create_fence);
    set_emugl_sync_timeline_inc(timeline_inc);
    set_emugl_sync_destroy_timeline(destroy_timeline);
    set_emugl_sync_register_trigger_wait(register_trigger_wait);
    set_emugl_sync_device_exists(device_exists);
}

RendererPtr RenderLibImpl::initRenderer(int width, int height,
                                        bool useSubWindow) {
    if (!mRenderer.expired()) {
        return nullptr;
    }

    const auto res = std::make_shared<RendererImpl>();
    if (!res->initialize(width, height, useSubWindow)) {
        return nullptr;
    }
    mRenderer = res;
    return res;
}

}  // namespace emugl

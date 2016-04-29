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
#include "emugl/common/logging.h"

namespace emugl {

void RenderLibImpl::setLogger(emugl_logger_struct logger) {
    set_emugl_logger(logger.coarse);
    set_emugl_cxt_logger(logger.fine);
}

void RenderLibImpl::setCrashReporter(emugl_crash_reporter_t reporter) {
    set_emugl_crash_reporter(reporter);
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

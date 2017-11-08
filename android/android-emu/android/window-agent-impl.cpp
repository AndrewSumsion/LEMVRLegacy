// Copyright 2015-2017 The Android Open Source Project
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

#include "android/emulation/control/window_agent.h"

#include "android/emulator-window.h"
#include "android/skin/qt/emulator-container.h"
#include "android/skin/qt/emulator-qt-window.h"
#include "android/utils/debug.h"

static_assert(WINDOW_MESSAGE_GENERIC == int(Ui::OverlayMessageIcon::None),
              "Bad message type enum value (None)");
static_assert(WINDOW_MESSAGE_INFO == int(Ui::OverlayMessageIcon::Info),
              "Bad message type enum value (Info)");
static_assert(WINDOW_MESSAGE_WARNING == int(Ui::OverlayMessageIcon::Warning),
              "Bad message type enum value (Warning)");
static_assert(WINDOW_MESSAGE_ERROR == int(Ui::OverlayMessageIcon::Error),
              "Bad message type enum value (Error)");

static const QAndroidEmulatorWindowAgent sQAndroidEmulatorWindowAgent = {
        .getEmulatorWindow = emulator_window_get,
        .rotate90Clockwise =
                [] {
                    return emulator_window_rotate_90(true);
                },
        .rotate = emulator_window_rotate,
        .getRotation =
                [] {
                    EmulatorWindow* window = emulator_window_get();
                    if (!window) return SKIN_ROTATION_0;
                    SkinLayout* layout = emulator_window_get_layout(window);
                    if (!layout) return SKIN_ROTATION_0;
                    return layout->orientation;
                },
        .showMessage =
                [](const char* message, WindowMessageType type, int timeoutMs) {
                    if (const auto win = EmulatorQtWindow::getInstance()) {
                        win->showMessage(
                                QString::fromUtf8(message),
                                static_cast<Ui::OverlayMessageIcon>(type),
                                timeoutMs);
                    } else {
                        const auto printer =
                                (type == WINDOW_MESSAGE_ERROR)
                                        ? &derror
                                        : (type == WINDOW_MESSAGE_WARNING)
                                                  ? &dwarning
                                                  : &dprint;
                        printer("%s", message);
                    }
                }};

const QAndroidEmulatorWindowAgent* const gQAndroidEmulatorWindowAgent =
        &sQAndroidEmulatorWindowAgent;

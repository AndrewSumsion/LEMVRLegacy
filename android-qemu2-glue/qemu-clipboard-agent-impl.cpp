// Copyright 2016 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/emulation/control/clipboard_agent.h"

#include "android/emulation/ClipboardPipe.h"

using android::emulation::ClipboardPipe;

static void set_guest_clipboard_callback(void (*cb)(void*,
                                                    const uint8_t*,
                                                    size_t),
                                         void* context) {
    ClipboardPipe::setGuestClipboardCallback(
            [cb, context](const uint8_t* data, size_t size) {
                cb(context, data, size);
            });
}

static void set_guest_clipboard_contents(const uint8_t* buf, size_t len) {
    const auto pipe = ClipboardPipe::Service::getPipe();
    if (pipe) {
        pipe->setGuestClipboardContents(buf, len);
    }
}

static const QAndroidClipboardAgent clipboardAgent = {
        .setEnabled = &ClipboardPipe::setEnabled,
        .setGuestClipboardCallback = set_guest_clipboard_callback,
        .setGuestClipboardContents = set_guest_clipboard_contents};

extern "C" const QAndroidClipboardAgent* const gQAndroidClipboardAgent =
        &clipboardAgent;

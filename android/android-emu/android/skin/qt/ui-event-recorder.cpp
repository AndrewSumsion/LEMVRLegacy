// Copyright 2016 The Android Open Source Project
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/skin/qt/ui-event-recorder.h"

android::base::LazyInstance<EventSetHolder> sEventRecorderEventsHolder = {};

EventSetHolder::EventSetHolder()
    : events({QEvent::Close, QEvent::Enter, QEvent::Leave, QEvent::FocusIn,
              QEvent::FocusOut, QEvent::Hide, QEvent::MouseButtonPress,
              QEvent::MouseButtonRelease, QEvent::Resize}) {}

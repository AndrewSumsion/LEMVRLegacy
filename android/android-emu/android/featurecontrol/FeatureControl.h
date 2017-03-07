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

#pragma once

#include "android/featurecontrol/Features.h"
#include "android/featurecontrol/HWMatching.h"

#include <string>

namespace android {
namespace featurecontrol {

// featurecontrol is used to switch on/off advanced features
// It loads sdk/tools/advancedFeatures.ini for default values and
// .android/advancedFeatures.ini for user overriden values.
// It is expected to be initialized at the beginning of the emulator.
// For easier testing, one may also want to pass the override value through
// command line and call setEnabledOverride. (Command line override not
// implemented yet)
//
// featurecontrol::isEnabled is thread safe, all other methods are not.
//
// To add new features, please (1) add it to android/data/advancedFeatures.ini
// (2) add a new line to FeatureControlDef.h, in the following format:
// FEATURE_CONTROL_ITEM(YOUR_FEATURE_NAME)
bool isEnabled(Feature feature);
void setEnabledOverride(Feature feature, bool isEnabled);
void resetEnabledToDefault(Feature feature);

// returns true if the user has specified it in
// home directory's user-based advancedFeatures.ini.
bool isOverridden(Feature feature);

// like setEnabledOverride, except it is a no-op
// if isOverridden(feature) == true.
void setIfNotOverriden(Feature feature, bool isEnabled);

Feature stringToFeature(const std::string& str);

// For hardware configurations special enough to warrant
// disabling or enabling features, we use the concept of
// "feature pattern" which consists of properties of hardware
// in question and a set of features to force-enable or disable.

// applyCachedServerFeaturePatterns() queries host hardware
// confiruation, takes current cached patterns, and enables
// or disables features based on which patterns match the host.
// If there is no cached patterns, no action is taken.
void applyCachedServerFeaturePatterns();
// asyncUpdateServerFeaturePatterns():
// If the current cached feature patterns don't exist or are over 24 hours old,
// asyncUpdateServerFeaturePatterns() starts a download of
// a protobuf containing the latest feature patterns, replacing
// the current cached ones.
void asyncUpdateServerFeaturePatterns();

} // namespace android
} // namespace featurecontrol

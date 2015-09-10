// Copyright 2015 The Android Open Source Project
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

#ifndef ANDROID_METRICS_ANDROID_METRICS_REPORTER_GA_H
#define ANDROID_METRICS_ANDROID_METRICS_REPORTER_GA_H

#include "android/metrics/metrics_reporter.h"
#include "android/utils/compiler.h"

#include <stdbool.h>

ANDROID_BEGIN_HEADER

// Upload the given metrics to Google Analytics.
bool androidMetrics_uploadMetricsGA(const AndroidMetrics* metrics);

ANDROID_END_HEADER

#endif  // ANDROID_METRICS_ANDROID_METRICS_REPORTER_GA_H

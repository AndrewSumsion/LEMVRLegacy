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

#include "android/metrics/metrics_reporter_toolbar.h"
#include "android/metrics/internal/metrics_reporter_toolbar_internal.h"

#include "android/curl-support.h"
#include "android/metrics/studio-helper.h"
#include "android/utils/compiler.h"
#include "android/utils/debug.h"
#include "android/utils/uri.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define mwarning(fmt, ...) \
    dwarning("%s:%d: " fmt, __FILE__, __LINE__, ##__VA_ARGS__)

int formatToolbarGetUrl(char** ptr,
                        const char* url,
                        const AndroidMetrics* metrics) {
    // This is of the form: androidsdk_<product_name>_<event_name>
    static const char product_name[] = "androidsdk_emu_crash";
    static const char guest_arch_key[] = "guest_arch";
    static const char guest_gl_vendor_key[] = "ggl_vendor";
    static const char guest_gl_renderer_key[] = "ggl_renderer";
    static const char guest_gl_version_key[] = "ggl_version";
    static const char system_time_key[] = "system_time";
    static const char user_time_key[] = "user_time";
    // These keys are the same as AndroidStudio already uses.
    static const char client_id_key[] = "id";
    static const char version_key[] = "version";
    static const char num_crashes_key[] = "exf";
    static const char opengl_alive_key[] = "opengl_alive";
    char* out_buf;

    assert(ptr != NULL);
    assert(*ptr == NULL);

    char* client_id = android_studio_get_installation_id();
    int result = asprintf(
            &out_buf,
            "as=%s&%s=%s&%s=%s&%s=%s"
            "&%s=%d&%s=%d&%s=%" PRId64 "&%s=%" PRId64,
            product_name,
            version_key, metrics->emulator_version,
            client_id_key, client_id,
            guest_arch_key, metrics->guest_arch,

            num_crashes_key, metrics->is_dirty ? 1 : 0,
            opengl_alive_key, metrics->opengl_alive,
            system_time_key, metrics->system_time,
            user_time_key, metrics->user_time);
    free(client_id);

    if (result >= 0 && metrics->guest_gpu_enabled > 0) {
        char* new_out_buf;
        result = asprintf(
                &new_out_buf,
                "%s&%s=%s&%s=%s&%s=%s",
                out_buf,
                guest_gl_vendor_key, metrics->guest_gl_vendor,
                guest_gl_renderer_key, metrics->guest_gl_renderer,
                guest_gl_version_key, metrics->guest_gl_version);
        free(out_buf);
        out_buf = new_out_buf;
    }

    if (result >= 0) {
        char* new_out_buf = uri_encode(out_buf);
        // There is no real reason to ping the empty string "" either.
        result = (new_out_buf == NULL || new_out_buf[0] == 0) ? -1 : result;
        free(out_buf);
        out_buf = new_out_buf;
    }

    if (result >= 0) {
        char* new_out_buf;
        result = asprintf(&new_out_buf, "%s?%s", url, out_buf);
        free(out_buf);
        out_buf = new_out_buf;
    }

    if (result >= 0) {
        *ptr = out_buf;
    } else {
        // |asprintf| returns garbage if result < 0. Let's be safer than that.
        *ptr = NULL;
    }

    return result;
}

/* typedef'ed to: androidMetricsUploaderFunction */
bool androidMetrics_uploadMetricsToolbar(const AndroidMetrics* metrics) {
    static const char toolbar_url[] = "https://tools.google.com/service/update";
    bool success = true;

    char* formatted_url = NULL;
    if (formatToolbarGetUrl(&formatted_url, toolbar_url, metrics) < 0) {
        mwarning("Failed to allocate memory for a request");
        return false;
    }

    char* error = nullptr;
    if (!curl_download_null(formatted_url, nullptr, true, &error)) {
        mwarning("Can't upload usage metrics: %s", error);
        ::free(error);
        success = false;
    }
    free(formatted_url);
    return success;
}

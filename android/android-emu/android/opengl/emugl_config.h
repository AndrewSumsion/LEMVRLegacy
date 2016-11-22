// Copyright 2015 The Android Open Source Project
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

#include "android/utils/compiler.h"

#include <stdbool.h>

ANDROID_BEGIN_HEADER

// A small structure used to model the EmuGL configuration
// to use.
// |enabled| is true if GPU emulation is enabled, false otherwise.
// |backend| contains the name of the backend to use, if |enabled|
// is true.
// |status| is a string used to report error or the current status
// of EmuGL emulation.
typedef struct {
    bool enabled;
    bool use_backend;
    int bitness;
    char backend[64];
    char status[256];
} EmuglConfig;

// Check whether or not the host GPU is blacklisted. If so, fall back
// to software rendering.
bool isHostGpuBlacklisted();
// If we actually switched to software, call this.
void setGpuBlacklistStatus(bool switchedSoftware);

typedef struct {
    char* make;
    char* model;
    char* device_id;
    char* revision_id;
    char* version;
    char* renderer;
} emugl_host_gpu_props;

typedef struct {
    int num_gpus;
    emugl_host_gpu_props* props;
} emugl_host_gpu_prop_list;

// Get a description of host GPU properties.
// Need to free after use.
emugl_host_gpu_prop_list emuglConfig_get_host_gpu_props();

// Enum tracking all current available renderer backends
// for the emulator.
typedef enum SelectedRenderer {
    SELECTED_RENDERER_UNKNOWN = 0,
    SELECTED_RENDERER_HOST = 1,
    SELECTED_RENDERER_OFF = 2,
    SELECTED_RENDERER_GUEST = 3,
    SELECTED_RENDERER_MESA = 4,
    SELECTED_RENDERER_SWIFTSHADER = 5,
    SELECTED_RENDERER_ANGLE = 6,
    SELECTED_RENDERER_ERROR = 255,
} SelectedRenderer;

// Returns SelectedRenderer value the selected gpu mode.
// Assumes that the -gpu command line option
// has been taken into account already.
SelectedRenderer emuglConfig_get_renderer(const char* gpu_mode);

void free_emugl_host_gpu_props(emugl_host_gpu_prop_list props);

// Initialize an EmuglConfig instance based on the AVD's hardware properties
// and the command-line -gpu option, if any.
//
// |config| is the instance to initialize.
// |gpu_enabled| is the value of the hw.gpu.enabled hardware property.
// |gpu_mode| is the value of the hw.gpu.mode hardware property.
// |gpu_option| is the value of the '-gpu <mode>' option, or NULL.
// |bitness| is the host bitness (0, 32 or 64).
// |no_window| is true if the '-no-window' emulator flag was used.
// |blacklisted| is true if the GPU driver is on the list of
// crashy GPU drivers.
//
// Returns true on success, or false if there was an error (e.g. bad
// mode or option value), in which case the |status| field will contain
// a small error message.
bool emuglConfig_init(EmuglConfig* config,
                      bool gpu_enabled,
                      const char* gpu_mode,
                      const char* gpu_option,
                      int bitness,
                      bool no_window,
                      bool blacklisted,
                      bool google_apis);

// Setup GPU emulation according to a given |backend|.
// |bitness| is the host bitness, and can be 0 (autodetect), 32 or 64.
void emuglConfig_setupEnv(const EmuglConfig* config);

ANDROID_END_HEADER

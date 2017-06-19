/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SnapshotDirGetter.h"

#include <stdio.h>

static std::string default_snapshot_dir_getter(bool create) {
    fprintf(stderr, "Error: Reading / writing snapshot in current folder.\n");
    return ".";
}

emugl_get_snapshot_dir_t emugl_get_snapshot_dir = default_snapshot_dir_getter;

void set_emugl_get_snapshot_dir(emugl_get_snapshot_dir_t get_snapshot_dir) {
    emugl_get_snapshot_dir = get_snapshot_dir;
}

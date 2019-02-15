#!/bin/sh

# Copyright 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

. $(dirname "$0")/utils/common.shi

shell_import utils/aosp_dir.shi
shell_import utils/package_builder.shi

PROGRAM_PARAMETERS=""

PROGRAM_DESCRIPTION=\
"Build dEQP against emulator combined guest/host driver."

option_register_var "--case=<dEQP-case-pattern>" OPT_DEQP_CASE "Run a particular set of dEQP tests"
option_register_var "--validation=<enable|disable>" OPT_VALIDATION "Enable vulkan validation layer while running tests"

aosp_dir_register_option
package_builder_register_options

option_parse "$@"

aosp_dir_parse_option

DEQP_BUILD_DIR=$AOSP_DIR/external/deqp/build
OPT_BUILD_DIR=$DEQP_BUILD_DIR

package_builder_process_options deqp

DEQP_DIR=$AOSP_DIR/external/deqp

# TODO: Use the upstream one
# VK_CTS_MUSTPASS_CASELIST=$AOSP_DIR/external/deqp/android/cts/master/vk-master.txt
VK_CTS_MUSTPASS_CASELIST=$AOSP_DIR/external/qemu/android/scripts/unix/vk-master-emu-expected.txt

DEQP_BUILD_DIR=$AOSP_DIR/external/deqp/build
DEQP_VK_EXEC_DIR=$DEQP_BUILD_DIR/external/vulkancts/modules/vulkan/
DEQP_TESTLOG_DST=$AOSP_DIR/external/qemu/dEQP-Log.qpa
DEQP_CASES="--deqp-caselist-file=$VK_CTS_MUSTPASS_CASELIST"

if [ "$OPT_DEQP_CASE" ]; then
DEQP_CASES="--deqp-case=$OPT_DEQP_CASE"
fi

if [ "$OPT_VALIDATION" ]; then
DEQP_VALIDATION="--deqp-validation=$OPT_VALIDATION"
export VULKAN_SDK=$AOSP_DIR/prebuilts/android-emulator-build/common/vulkan/linux-x86_64/
fi

DEQP_VK_EXEC_NAME="./deqp-vk $DEQP_CASES $DEQP_VALIDATION --deqp-log-filename=$DEQP_TESTLOG_DST"

cd $DEQP_VK_EXEC_DIR

# Set environment variables

# Known issue: swiftshader not quite working yet due to library rpath issues.
export ANDROID_EMU_TEST_WITH_HOST_GPU=1

# Add layer path.
export VK_LAYER_PATH=testlib64/layers

for SYSTEM in $LOCAL_HOST_SYSTEMS; do
    case $SYSTEM in
        linux*)
            log "LINUX"
            # Allow running via SSH
            # Add layer library path as well
            export LD_LIBRARY_PATH=lib64:testlib64/layers:/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
            export DISPLAY=:0
            ;;
    esac
done

$DEQP_VK_EXEC_NAME

log "Done running dEQP."

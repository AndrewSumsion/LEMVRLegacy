#!/bin/sh

# Copyright 2015 The Android Open Source Project
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
shell_import utils/emulator_prebuilts.shi
shell_import utils/install_dir.shi
shell_import utils/option_parser.shi
shell_import utils/package_list_parser.shi
shell_import utils/package_builder.shi

PROGRAM_PARAMETERS=""

PROGRAM_DESCRIPTION=\
"Build prebuilt breakpad for Linux, Windows and Darwin."

package_builder_register_options

aosp_dir_register_option
prebuilts_dir_register_option
install_dir_register_option "common/breakpad"

option_parse "$@"

if [ "$PARAMETER_COUNT" != 0 ]; then
    panic "This script takes no arguments. See --help for details."
fi

prebuilts_dir_parse_option
aosp_dir_parse_option
install_dir_parse_option

ARCHIVE_DIR=$PREBUILTS_DIR/archive
if [ ! -d "$ARCHIVE_DIR" ]; then
    dump "Downloading dependencies sources first."
    $(program_directory)/download-sources.sh \
        --verbosity=$(get_verbosity) \
        --prebuilts-dir="$PREBUILTS_DIR" ||
            panic "Could not download source archives!"
fi
if [ ! -d "$ARCHIVE_DIR" ]; then
    panic "Missing archive directory: $ARCHIVE_DIR"
fi
PACKAGE_LIST=$ARCHIVE_DIR/PACKAGES.TXT
if [ ! -f "$PACKAGE_LIST" ]; then
    panic "Missing package list file, run download-sources.sh: $PACKAGE_LIST"
fi

package_builder_process_options breakpad

package_list_parse_file "$PACKAGE_LIST"

# $1+: Extra configuration options.
build_breakpad_package () {
    builder_unpack_package_source "breakpad" "$ARCHIVE_DIR"
    builder_unpack_package_source "linux-syscall-support" "$ARCHIVE_DIR"
    builder_build_autotools_package breakpad "$@"
}

# Perform a Darwin build through ssh to a remote machine.
# $1: Darwin host name.
# $2: List of darwin target systems to build for.
do_remote_darwin_build () {
    builder_prepare_remote_darwin_build \
            "/tmp/$USER-rebuild-darwin-ssh-$$/breakpad-build" \
            "$ARCHIVE_DIR"

    builder_run_remote_darwin_build

    for SYSTEM in $DARWIN_SYSTEMS; do
        builder_remote_darwin_retrieve_install_dir $SYSTEM $INSTALL_DIR
    done
}

if [ "$DARWIN_SSH" -a "$DARWIN_SYSTEMS" ]; then
    # Perform remote Darwin build first.
    dump "Remote breakpad build for: $DARWIN_SYSTEMS"
    do_remote_darwin_build "$DARWIN_SSH" "$DARWIN_SYSTEMS"
fi

for SYSTEM in $LOCAL_HOST_SYSTEMS; do
    (
        builder_prepare_for_host_no_binprefix "$SYSTEM" "$AOSP_DIR"

        dump "$(builder_text) Building breakpad"

        build_breakpad_package

        # Copy binaries necessary for the build itself as well as static
        # libraries.

        copy_directory_files \
                "$(builder_install_prefix)" \
                "$INSTALL_DIR/$SYSTEM" \
                lib/libbreakpad_client.a \
                lib/libbreakpad.a \
                lib/libdisasm.a \

        copy_directory \
                "$(builder_install_prefix)/include/breakpad" \
                "$INSTALL_DIR/$SYSTEM/include/breakpad"

        copy_directory_files \
                "$(builder_install_prefix)" \
                "$INSTALL_DIR/$SYSTEM" \
                bin/dump_syms$(builder_host_ext) \
                bin/dump_syms_dwarf$(builder_host_ext) \
                bin/dump_syms_macho$(builder_host_ext) \
                bin/sym_upload$(builder_host_ext) \
                bin/minidump_stackwalk$(builder_host_ext) \
                bin/minidump_upload$(builder_host_ext)

    ) || panic "[$SYSTEM] Could not build breakpad!"

done

log "Done building breakpad"

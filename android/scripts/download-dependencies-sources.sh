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

shell_import utils/emulator_prebuilts.shi
shell_import utils/option_parser.shi
shell_import utils/package_list_parser.shi

###
###  Utilities
###

# Download a file.
# $1: Source URL
# $2: Destination directory.
# $3: [Optional] expected SHA-1 sum of downloaded file.
download_package () {
    # Assume the packages are already downloaded under $ARCHIVE_DIR
    local DST_DIR PKG_URL PKG_NAME SHA1SUM REAL_SHA1SUM

    PKG_URL=$1
    PKG_NAME=$(basename "$PKG_URL")
    DST_DIR=$2
    SHA1SUM=$3

    log "Downloading $PKG_NAME..."
    (cd "$DST_DIR" && run curl -L -o "$PKG_NAME" "$PKG_URL") ||
            panic "Can't download '$PKG_URL'"

    if [ "$SHA1SUM" ]; then
        log "Checking $PKG_NAME content"
        REAL_SHA1SUM=$(compute_file_sha1 "$DST_DIR"/$PKG_NAME)
        if [ "$REAL_SHA1SUM" != "$SHA1SUM" ]; then
            panic "Error: Invalid SHA-1 sum for $PKG_NAME: $REAL_SHA1SUM (expected $SHA1SUM)"
        fi
    fi
}

# Uncompress a tarball or zip file.
# $1: Source package path.
# $2: Destination directory.
unpack_package () {
    local PKG PKG_NAME PKG_BASENAME DST_DIR
    PKG=$1
    DST_DIR=$2
    PKG_NAME=$(basename "$PKG")
    case $PKG_NAME in
        *.zip)
            PKG_BASENAME=${PKG_NAME%.zip}
            ;;
        *.tar.gz)
            PKG_BASENAME=${PKG_NAME%.tar.gz}
            ;;
        *.tar.bz2)
            PKG_BASENAME=${PKG_NAME%.tar.bz2}
            ;;
        *.tar.xz)
            PKG_BASENAME=${PKG_NAME%.tar.xz}
            ;;
        *)
            panic "Unsupported package type: $PKG_NAME"
    esac

    log "Uncompressing $PKG_NAME into $DST_DIR"
    case $PKG_NAME in
        *.tar.gz|*.tar.bz2|*.tar.xz)
            run tar xf "$PKG" -C "$DST_DIR"
            ;;
        *.zip)
            run unzip -q -u "$PKG" -d "$DST_DIR"
            ;;
        *)
            panic "Unknown archive type: $PKG_NAME"
            ;;
    esac
}

# Clone a git repository with a maximum depth of 1, and checkout a
# specific branch.
# $1: Source git URL.
# $2: Destination directory.
# $3: [Optional] Branch/commit to checkout.
git_clone_depth1 () {
    local GIT_URL DST_DIR BRANCH
    GIT_URL=$1
    DST_DIR=$2
    BRANCH=${3:-master}

    if [ -d "$DST_DIR" ]; then
        panic "Git destination directory already exists: $DST_DIR"
    fi
    run git clone --depth 1 --branch "$BRANCH" "$GIT_URL" "$DST_DIR" ||
            panic "Could not clone git repository: $GIT_URL"
}

###
###  Command-line parsing
###

PROGRAM_DESCRIPTION=\
"This script is used to download the source tarballs of Android emulator
library dependencies (e.g. libffi, libz, glib, etc..) and place them under
<prebuilts>/archive, where the default value of <prebuilts> is:

    $DEFAULT_PREBUILTS_DIR

Use the --prebuilts-dir=<path> option to change it to a new value, or
alternatively, define the ANDROID_EMULATOR_PREBUILTS_DIR environment
variable.

If a package file is already in <prebuilts>/archive, the script checks its
SHA-1 to avoid re-downloading it. You can however use --force to always
download."

PROGRAM_PARAMETERS=""

OPT_FORCE=
option_register_var "--force" OPT_FORCE "Always download files."

PACKAGE_LIST=$(program_directory)/../dependencies/PACKAGES.TXT
option_register_var "--list=<file>" PACKAGE_LIST "Specify package list file"

prebuilts_dir_register_option

option_parse "$@"

if [ "$PARAMETER_COUNT" != "0" ]; then
    panic "This script doesn't take arguments. See --help."
fi

if [ ! -f "$PACKAGE_LIST" ]; then
    panic "Missing package list file: $PACKAGE_LIST"
fi

PACKAGE_DIR=$(dirname "$PACKAGE_LIST")

prebuilts_dir_parse_option

###
###  Do the work.
###

# Create archive directory.
ARCHIVE_DIR=$PREBUILTS_DIR/archive
run mkdir -p "$ARCHIVE_DIR" ||
        panic "Could not create archive directory: $ARCHIVE_DIR"

# Get each dependency individually.
# This is a callback for package_list_parse, which expects the definition
# of a few local variables (see package_list_parser.shi documentation).
get_package () {
    local PKG_FILE

    if [ "$PKG_URL" ]; then
        PKG_NAME=$(basename "$PKG_URL")
        PKG_FILE=$ARCHIVE_DIR/$PKG_NAME
    else
        panic "Missing URL=<url> field on line $LINE_NUMBER: $PACKAGE_LIST"
    fi
}

package_list_parse_file "$PACKAGE_LIST"
for PACKAGE in $(package_list_get_packages); do
    PKG_URL=$(package_list_get_url $PACKAGE)
    if [ "$PKG_URL" ]; then
        PKG_FILE=$ARCHIVE_DIR/$(package_list_get_filename $PACKAGE)
        PKG_SHA1=$(package_list_get_sha1 $PACKAGE)
        if [ -f "$PKG_FILE" -a "$OPT_FORCE" ]; then
            run rm -f "$PKG_FILE"
        fi
        if [ -f "$PKG_FILE" -a -n "$PKG_SHA1" ]; then
            dump "Checking existing file: $PKG_FILE"
            ARCHIVE_SHA1=$(compute_file_sha1 "$PKG_FILE")
            if [ "$ARCHIVE_SHA1" != "$PKG_SHA1" ]; then
                dump "WARNING: Re-downloading existing file due to bad SHA-1 ($ARCHIVE_SHA1, expected $PKG_SHA1): $PKG_FILE"
                run rm -f "$PKG_FILE"
            fi
        fi
        if [ ! -f "$PKG_FILE" ]; then
            dump "Downloading $PKG_URL"
            download_package "$PKG_URL" "$ARCHIVE_DIR" "$PKG_SHA1"
        fi
        PKG_PATCHES=$(package_list_get_patches $PACKAGE)
        if [ "$PKG_PATCHES" ]; then
            PKG_PATCHES_SRC=$PACKAGE_DIR/$PKG_PATCHES
            PKG_PATCHES_DST=$ARCHIVE_DIR/$(basename "$PKG_PATCHES")
            dump "Copying patches file: $PKG_PATCHES_DST"
            if [ ! -f "$PKG_PATCHES_SRC" ]; then
                panic "Missing patches file from line $COUNT: $PKG_PATCHES_SRC"
            fi
            run mkdir -p $(dirname "$PKG_PATCHES_DST") &&
            run cp -f "$PKG_PATCHES_SRC" "$PKG_PATCHES_DST" ||
                    panic "Could not copy: $PKG_PATCHES_SRC to $PKG_PATCHES_DST"
        fi
    else  # no PKG_URL
        panic "Missing URL=<url> field for package $PACKAGE"
    fi
done

run cp -f "$PACKAGE_LIST" "$ARCHIVE_DIR"/PACKAGES.TXT

dump "Done."

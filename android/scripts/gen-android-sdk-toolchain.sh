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
shell_import utils/option_parser.shi

PROGRAM_PARAMETERS="<install-dir>"

PROGRAM_DESCRIPTION=\
"Generate a special toolchain wrapper that can be used to build binaries to be
included with the Android SDK. These binaries are guaranteed to run on systems
older than the build machine.

This is achieved by using the prebuilt toolchains found under:

  \$AOSP/prebuilts/gcc/<system>/host/

Where \$AOSP is the path to an AOSP checkout, and <system> corresponds to the
host system this script runs on (i.e. linux-x86_64 or darwin-x86_64). The toolchain
will be capable of handling C++11

If you have the 'ccache' program installed, the wrapper will use it
automatically unless you use the --no-ccache option.

The script will put under <install-dir>/ various tools, e.g.
'x86_64-linux-c++' for the 64-bit Linux compiler.

You can use the --print=<tool> option to print the corresponding name
or path, *instead* of creating a toolchain. Valid values for <tool> are:

   binprefix       -> Print the binprefix (e.g. 'x86_64-linux-')
   cc              -> Print the compiler name (e.g. 'x86_64_linux-cc')
   c++             -> Print the c++ compiler name.
   ld, ar, as, ... -> Same for other tools.
   sysroot         -> Print the path to directory corresponding to the current
                      toolchain."

aosp_dir_register_option

OPT_HOST=
option_register_var "--host=<system>" OPT_HOST "Host system to support."

OPT_BINPREFIX=
option_register_var "--binprefix=<prefix>" OPT_BINPREFIX "Specify toolchain binprefix [autodetected]"

OPT_PREFIX=
option_register_var "--prefix=<prefix>" OPT_PREFIX "Specify extra include/lib prefix."

OPT_PRINT=
option_register_var "--print=<tool>" OPT_PRINT "Print the exact name of a specific tool."

OPT_CCACHE=
option_register_var "--ccache=<program>" OPT_CCACHE "Use specific ccache program."

OPT_NO_CCACHE=
option_register_var "--no-ccache" OPT_NO_CCACHE "Don't try to probe and use ccache."

OPT_CXX11=
option_register_var "--cxx11" OPT_CXX11 "Enable C++11 features."

option_parse "$@"

if [ "$PARAMETER_COUNT" != 1 ]; then
    panic "This script requires a single parameter! See --help."
fi

INSTALL_DIR=$PARAMETER_1

# Determine host system type.
BUILD_HOST=$(get_build_os)
BUILD_ARCH=$(get_build_arch)
BUILD_BUILD_TARGET_TAG=${BUILD_HOST}-${BUILD_ARCH}
log "Found current build machine: $BUILD_BUILD_TARGET_TAG"

# Handle CCACHE related arguments.
CCACHE=
if [ "$OPT_CCACHE" ]; then
    if [ "$OPT_NO_CCACHE" ]; then
        panic "You cannot use both --ccache=<program> and --no-ccache at the same time."
    fi
    CCACHE=$(find_program "$OPT_CCACHE")
    if [ -z "$CCACHE" ]; then
        panic "Missing ccache program: $OPT_CCACHE"
    fi
elif [ -z "$OPT_NO_CCACHE" ]; then
    CCACHE=$(find_program ccache)
    if [ "$CCACHE" ]; then
        log "Auto-config: --ccache=$CCACHE"
    fi
fi

aosp_dir_parse_option

# Handle --host option.
if [ "$OPT_HOST" ]; then
    case $OPT_HOST in
        linux-x86_64|darwin-x86_64|windows-x86|windows-x86_64)
            ;;
        *)
            panic "Invalid --host value: $OPT_HOST"
            ;;
    esac
    HOST=$OPT_HOST
else
    HOST=$BUILD_BUILD_TARGET_TAG
    log "Auto-config: --host=$HOST"
fi

# Handle --binprefix option.
BINPREFIX=
if [ "$OPT_BINPREFIX" ]; then
    # Allow a final - after the binprefix
    BINPREFIX=${OPT_BINPREFIX%%-}
    if [ "$BINPREFIX" ]; then
        BINPREFIX=${BINPREFIX}-
    fi
fi

# Generate a small toolchain wrapper program
#
# $1: program name, without any prefix (e.g. gcc, g++, ar, etc..)
# $2: source prefix (e.g. 'i586-mingw32msvc-')
# $3: destination prefix (e.g. 'i586-px-mingw32msvc-')
# $4: destination directory for the generated program
# $5: option, CLANG installation path.
#
# You may also define the following variables to pass extra tool flags:
#
#    EXTRA_CFLAGS
#    EXTRA_POSTCFLAGS
#    EXTRA_CXXFLAGS
#    EXTRA_POSTCXXFLAGS
#    EXTRA_LDFLAGS
#    EXTRA_POSTLDFLAGS
#    EXTRA_ARFLAGS
#    EXTRA_ASFLAGS
#    EXTRA_WINDRESFLAGS
#
# As well as a special variable containing commands to setup the
# environment before tool invocation:
#
#    EXTRA_ENV_SETUP
#
gen_wrapper_program ()
{
    local PROG="$1"
    local SRC_PREFIX="$2"
    local DST_PREFIX="$3"
    local DST_FILE="$4/${SRC_PREFIX}$PROG"
    local CLANG_BINDIR="$5"
    local FLAGS=""
    local POST_FLAGS=""
    local DST_PROG="$PROG"

    case $PROG in
      cc|gcc|cpp|clang)
          FLAGS=$FLAGS" $EXTRA_CFLAGS"
          POST_FLAGS=" $POST_CFLAGS"
          ;;
      c++|g++|clang++)
          FLAGS=$FLAGS" $EXTRA_CXXFLAGS"
          POST_FLAGS=" $POST_CXXFLAGS"
          ;;
      ar) FLAGS=$FLAGS" $EXTRA_ARFLAGS";;
      as) FLAGS=$FLAGS" $EXTRA_ASFLAGS";;
      ld|ld.bfd|ld.gold)
        FLAGS=$FLAGS" $EXTRA_LDFLAGS"
        POST_FLAGS=" $POST_LDFLAGS"
        ;;
      windres) FLAGS=$FLAGS" $EXTRA_WINDRESFLAGS";;
    esac

    # Redirect gcc -> clang if we are using clang.
    if [ "$CLANG_BINDIR" ]; then
        CLANG_BINDIR=${CLANG_BINDIR%/}
        case $PROG in
            cc|gcc|clang)
                DST_PROG=clang
                DST_PREFIX=$CLANG_BINDIR/
                ;;
            c++|g++|clang++)
                DST_PROG=clang++
                DST_PREFIX=$CLANG_BINDIR/
                ;;
            clang-tidy)
                DST_PREFIX=$CLANG_BINDIR/
                ;;
        esac
    fi

    if [ -z "$DST_PREFIX" ]; then
        # Avoid infinite loop by getting real path of destination
        # program
        DST_PROG=$(which "$PROG" 2>/dev/null || true)
        if [ -z "$DST_PROG" ]; then
            log "Ignoring: ${SRC_PREFIX}$PROG"
            return
        fi
        DST_PREFIX=$(dirname "$DST_PROG")/
        DST_PROG=$(basename "$DST_PROG")
    fi

    if [ ! -f "${DST_PREFIX}$DST_PROG" ]; then
        case $DST_PROG in
            cc)
                # Our toolset doen't have separate cc binary for some platforms;
                # let those use 'gcc' directly.
                DST_PROG=gcc
                if [ ! -f "${DST_PREFIX}$DST_PROG" ]; then
                    log "  Skipping: ${SRC_PREFIX}$PROG  [missing destination program, ${DST_PREFIX}$DST_PROG]"
                    return
                fi
                ;;
            *)
                log "  Skipping: ${SRC_PREFIX}$PROG  [missing destination program, ${DST_PREFIX}$DST_PROG]"
                return
                ;;
         esac
    fi

    if [ "$CCACHE" ]; then
        DST_PREFIX="$CCACHE $DST_PREFIX"
    fi

    cat > "$DST_FILE" << EOF
#!/bin/sh
# Auto-generated by $(program_name), DO NOT EDIT!!
# Environment setup
$EXTRA_ENV_SETUP
# Tool invocation.
${DST_PREFIX}$DST_PROG $FLAGS "\$@" $POST_FLAGS

EOF
    chmod +x "$DST_FILE"
    log "  Generating: ${SRC_PREFIX}$PROG"
}

# $1: source prefix
# $2: destination prefix
# $3: destination directory.
# $4: optional. Clang installation path.
gen_wrapper_toolchain () {
    local SRC_PREFIX="$1"
    local DST_PREFIX="$2"
    local DST_DIR="$3"
    local CLANG_BINDIR="$4"
    local PROG
    local COMPILERS="cc gcc clang c++ g++ clang++ cpp ld clang-tidy"
    local PROGRAMS="as ar ranlib strip strings nm objdump objcopy dlltool"

    log "Generating toolchain wrappers in: $DST_DIR"
    run mkdir -p "$DST_DIR"

    if [ -n "$SRC_PREFIX" ]; then
        SRC_PREFIX=${SRC_PREFIX%%-}-
    fi

    case $SRC_PREFIX in
        *mingw*)
            PROGRAMS="$PROGRAMS windres"
            case $CURRENT_HOST in
                windows-x86)
                    EXTRA_WINDRESFLAGS="--target=pe-i386"
                    ;;
            esac
            ;;
    esac

    if [ "$CCACHE" ]; then
        # If this is clang, disable ccache-induced warnings and
        # restore colored diagnostics.
        # http://petereisentraut.blogspot.fr/2011/05/ccache-and-clang.html
        if (${DST_PREFIX}gcc --version 2>/dev/null | grep --color=never -q clang); then
            EXTRA_CLANG_FLAGS="-Qunused-arguments -fcolor-diagnostics"
            EXTRA_CFLAGS="$EXTRA_CFLAGS $EXTRA_CLANG_FLAGS"
            EXTRA_CXXFLAGS="$EXTRA_CXXFLAGS $EXTRA_CLANG_FLAGS"
        fi
    fi

    for PROG in $COMPILERS; do
        gen_wrapper_program $PROG "$SRC_PREFIX" "$DST_PREFIX" "$DST_DIR" "$CLANG_BINDIR"
    done

    for PROG in $PROGRAMS; do
        gen_wrapper_program $PROG "$SRC_PREFIX" "$DST_PREFIX" "$DST_DIR"
    done

    EXTRA_CFLAGS=
    EXTRA_CXXFLAGS=
    EXTRA_LDFLAGS=
    EXTRA_ARFLAGS=
    EXTRA_ASFLAGS=
    EXTRA_WINDRESFLAGS=
    EXTRA_ENV_SETUP=
}


# Configure the darwin toolchain.
prepare_build_for_darwin() {
    OSX_VERSION=$(sw_vers -productVersion)
    OSX_DEPLOYMENT_TARGET=10.8
    OSX_SDK_SUPPORTED="10.10 10.11 10.12 10.13"
    OSX_SDK_INSTALLED_LIST=$(xcodebuild -showsdks 2>/dev/null | \
            grep --color=never macosx | sed -e "s/.*macosx10\.//g" | sort -n | \
            sed -e 's/^/10./g' | tr '\n' ' ')
    if [ -z "$OSX_SDK_INSTALLED_LIST" ]; then
        panic "Please install XCode on this machine!"
    fi
    log "OSX: Installed SDKs: $OSX_SDK_INSTALLED_LIST"
    for supported_sdk in $(echo "$OSX_SDK_SUPPORTED" | tr ' ' '\n' | sort -r)
    do
        POSSIBLE_OSX_SDK_VERSION=$(echo "$OSX_SDK_INSTALLED_LIST" | tr ' ' '\n' | grep $supported_sdk | head -1)
        if [ -n "$POSSIBLE_OSX_SDK_VERSION" ]; then
            OSX_SDK_VERSION=$POSSIBLE_OSX_SDK_VERSION
        fi
    done
    log "OSX: Using SDK version $OSX_SDK_VERSION"
    if [ -z "$OSX_SDK_VERSION" ]; then
        panic "No supported OSX SDKs found on the machine (Need any of: [$OSX_SDK_SUPPORTED], have: [$OSX_SDK_INSTALLED_LIST])"
    fi

    XCODE_PATH=$(xcode-select -print-path 2>/dev/null)
    log "OSX: XCode path: $XCODE_PATH"

    OSX_SDK_ROOT=$XCODE_PATH/Platforms/MacOSX.platform/Developer/SDKs/MacOSX${OSX_SDK_VERSION}.sdk
    log "OSX: Looking for $OSX_SDK_ROOT"
    if [ ! -d "$OSX_SDK_ROOT" ]; then
        OSX_SDK_ROOT=/Developer/SDKs/MacOSX${OSX_SDK_VERSION}.sdk
        log "OSX: Looking for $OSX_SDK_ROOT"
        if [ ! -d "$OSX_SDK_ROOT" ]; then
            panic "Could not find SDK $OSX_SDK_VERSION at $OSX_SDK_ROOT"
        fi
    fi
    log "OSX: Using SDK at $OSX_SDK_ROOT"
    EXTRA_ENV_SETUP="export SDKROOT=$OSX_SDK_ROOT"
    CLANG_BINDIR=$PREBUILT_TOOLCHAIN_DIR/bin
    PREBUILT_TOOLCHAIN_DIR=

    GNU_CONFIG_HOST=

    common_FLAGS="-target x86_64-apple-darwin12.0.0"
    var_append common_FLAGS " -isysroot $OSX_SDK_ROOT"
    var_append common_FLAGS " -mmacosx-version-min=$OSX_DEPLOYMENT_TARGET"
    var_append common_FLAGS " -DMACOSX_DEPLOYMENT_TARGET=$OSX_DEPLOYMENT_TARGET"
    EXTRA_CFLAGS="$common_FLAGS -B/usr/bin"
    EXTRA_CXXFLAGS="$common_FLAGS -B/usr/bin"
    var_append EXTRA_CXXFLAGS "-stdlib=libc++"
    EXTRA_LDFLAGS="$common_FLAGS"
    DST_PREFIX=

    if [ "$OPT_CXX11" ]; then
        var_append EXTRA_CXXFLAGS "-std=c++14" "-Werror=c++14-compat"
    fi
}

prepare_build_for_linux() {
    GCC_DIR="${PREBUILT_TOOLCHAIN_DIR}/lib/gcc/x86_64-linux/4.8/"
    CLANG_BINDIR=$AOSP_DIR/$(aosp_prebuilt_clang_dir_for linux)
    CLANG_DIR=$(realpath $CLANG_BINDIR/..)
    GNU_CONFIG_HOST=x86_64-linux
    CLANG_VERSION=$(${CLANG_BINDIR}/clang -v 2>&1 | head -n 1 | awk '{print $4}')
    SYSROOT="${PREBUILT_TOOLCHAIN_DIR}/sysroot"

    # Clang will invoke the linker, but will not correctly infer the
    # -gcc-toolchain, so we have to manually configure the various paths.
    # Note that this can result in warnings of unused flags if we are not
    # linking and merely translating a .c or .cpp file
    local GCC_LINK_FLAGS=
    var_append GCC_LINK_FLAGS "--gcc-toolchain=$PREBUILT_TOOLCHAIN_DIR"
    var_append GCC_LINK_FLAGS "-B$PREBUILT_TOOLCHAIN_DIR/lib/gcc/x86_64-linux/4.8/"
    var_append GCC_LINK_FLAGS "-L$PREBUILT_TOOLCHAIN_DIR/lib/gcc/x86_64-linux/4.8/"
    var_append GCC_LINK_FLAGS "-L$PREBUILT_TOOLCHAIN_DIR/x86_64-linux/lib64/"
    var_append GCC_LINK_FLAGS "-fuse-ld=${PREBUILT_TOOLCHAIN_DIR}/bin/x86_64-linux-ld"
    var_append GCC_LINK_FLAGS "-L${CLANG_BINDIR}/../lib64/clang/${CLANG_VERSION}/lib/linux/"
    var_append GCC_LINK_FLAGS "-L${CLANG_BINDIR}/../lib64/"
    var_append GCC_LINK_FLAGS "--sysroot=${SYSROOT}"

    if [ $(get_verbosity) -gt 3 ]; then
      # This will get pretty crazy, but useful if you want to debug linker issues.
      var_append GCC_LINK_FLAGS "-Wl,-verbose"
      var_append GCC_LINK_FLAGS "-v"
    else
      # You're likely to always hit this due to our linker workarounds..
      var_append GCC_LINK_FLAGS "-Wno-unused-command-line-argument"
    fi


    EXTRA_CFLAGS="-m64"
    # var_append EXTRA_CFLAGS "-std=c89"
    var_append EXTRA_CFLAGS "-isystem $SYSROOT/usr/include"
    var_append EXTRA_CFLAGS "-isystem $SYSROOT/usr/include/x86_64-linux-gnu"
    var_append EXTRA_CFLAGS ${GCC_LINK_FLAGS}


    EXTRA_CXXFLAGS="-m64"
    var_append EXTRA_CXXFLAGS "-stdlib=libc++"
    var_append EXTRA_CXXFLAGS ${GCC_LINK_FLAGS}

    if [ "$OPT_CXX11" ]; then
        var_append EXTRA_CXXFLAGS "-std=c++14" "-Werror=c++14-compat"
    fi

    # Make sure we can find libc++
    EXTRA_LDFLAGS="-m64"
    var_append EXTRA_LDFLAGS "-L${CLANG_BINDIR}/../lib64/clang/${CLANG_VERSION}/lib/linux/"
    var_append EXTRA_LDFLAGS "-L${CLANG_BINDIR}/../lib64/"
    # Make sure we don't accidently pick up libstdc++
    var_append EXTRA_LDFLAGS "-nodefaultlibs"

    # If we manually call the linker we need to pass in the correct default libs
    # we link against. These have to go last!
    POST_LDFLAGS=
    var_append POST_LDFLAGS "-lc"
    var_append POST_LDFLAGS "-lc++"
    var_append POST_LDFLAGS "-lm"
    var_append POST_LDFLAGS "-lgcc"
    var_append POST_LDFLAGS "-lgcc_s"
}

prepare_build_for_windows () {
    local GCC_LINK_FLAGS="-Wno-missing-braces -Wno-aggressive-loop-optimizations"

    case $CURRENT_HOST in
      windows-x86)
          GNU_CONFIG_HOST=i686-w64-mingw32
          EXTRA_CFLAGS="-m32"
          EXTRA_CXXFLAGS="-m32"
          ;;
      windows-x86_64)
          GNU_CONFIG_HOST=x86_64-w64-mingw32
          EXTRA_CFLAGS="-m64"
          EXTRA_CXXFLAGS="-m64"
          ;;
    esac

    if [ "$OPT_CXX11" ]; then
        var_append EXTRA_CXXFLAGS "-std=c++11" "-Werror=c++11-compat"
    fi

    var_append EXTRA_CFLAGS ${GCC_LINK_FLAGS}
    var_append EXTRA_CXXFLAGS ${GCC_LINK_FLAGS}
 }


# Prints info based on the passed in parameter
# $1 The information we want to print, from { binprefix, sysroot, clang-dir,
# clang-version}
print_info () {
    local PRINT=$1

    # If $BINPREFIX is not empty, ensure it has a trailing -.
    if [ "$BINPREFIX" ]; then
        BINPREFIX=${BINPREFIX%%-}-
    fi
    case $PRINT in
        binprefix)
            printf "%s\n" "$BINPREFIX"
            ;;
        sysroot)
            local SUBDIR
            SUBDIR="$(aosp_prebuilt_toolchain_sysroot_subdir_for "${CURRENT_HOST}")"
            if [ -n "${SUBDIR}" ]; then
                printf "${AOSP_DIR}/${SUBDIR}"
            fi
            ;;
        clang-dir)
          printf "%s\n" "${CLANG_DIR}"
            ;;
        clang-version)
            CLANG_VERSION=$(${CLANG_BINDIR}/clang -v 2>&1 | head -n 1 | awk '{print $4}')
            printf "%s\n" "$CLANG_VERSION"
            ;;
        libcplusplus)
          printf "%s\n" "$CLANG_DIR/lib64/libc++.so"
          ;;
        libasan-dir)
          printf "%s" "$CLANG_DIR/lib64/clang/5.0/lib/linux/"
          ;;
        *)
            printf "%s\n" "${BINPREFIX}$PRINT"
            ;;
    esac
}


# Prepare the build for a given host system.
# $1: Host system tag (e.g. linux-x86_64)
prepare_build_for_host () {
    local CURRENT_HOST=$1
    EXTRA_ENV_SETUP=
    CLANG_BINDIR=
    PREBUILT_TOOLCHAIN_SUBDIR=$(aosp_prebuilt_toolchain_subdir_for $CURRENT_HOST)
    PREBUILT_TOOLCHAIN_DIR=$AOSP_DIR/$PREBUILT_TOOLCHAIN_SUBDIR
    TOOLCHAIN_PREFIX=$(aosp_prebuilt_toolchain_prefix_for $CURRENT_HOST)
    DST_PREFIX=$PREBUILT_TOOLCHAIN_DIR/bin/$TOOLCHAIN_PREFIX

    case $CURRENT_HOST in
        linux-*)
            prepare_build_for_linux
            ;;
        darwin-*)
            prepare_build_for_darwin
            ;;
        windows-*)
            prepare_build_for_windows
    esac


    if [ "$OPT_BINPREFIX" ]; then
        BINPREFIX=${OPT_BINPREFIX%%-}
    else
        BINPREFIX=$GNU_CONFIG_HOST
    fi

    if [ "$OPT_PREFIX" ]; then
        var_append EXTRA_CFLAGS "-I$OPT_PREFIX/include"
        var_append EXTRA_CXXFLAGS "-I$OPT_PREFIX/include"
        var_append EXTRA_LDFLAGS "-L$OPT_PREFIX/lib"
    fi

    if [ "$OPT_PRINT" ]; then
        print_info $OPT_PRINT
    else
        log "Generating ${BINPREFIX%%-} wrapper toolchain in $INSTALL_DIR"
        gen_wrapper_toolchain "$BINPREFIX" "$DST_PREFIX" "$INSTALL_DIR" "$CLANG_BINDIR"

        # Create pkgconfig link for other scripts.
        case $CURRENT_HOST in
            linux-*)
                ln -sfn "$PREBUILT_TOOLCHAIN_DIR/sysroot/usr/lib/pkgconfig" "$INSTALL_DIR/pkgconfig"
                ;;
        esac
    fi
}

prepare_build_for_host $HOST

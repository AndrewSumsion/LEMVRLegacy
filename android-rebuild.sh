#!/bin/bash
#
# this script is used to rebuild all QEMU binaries for the host
# platforms.
#
# assume that the device tree is in TOP
#

set -e
export LANG=C
export LC_ALL=C

VERBOSE=0

MINGW=
NO_TESTS=
OUT_DIR=objs

for OPT; do
    case $OPT in
        --mingw)
            MINGW=true
            ;;
        --verbose)
            VERBOSE=$(( $VERBOSE + 1 ))
            ;;
        --no-tests)
            NO_TESTS=true
            ;;
        --out-dir=*)
            OUT_DIR=${OPT##--out-dir=}
            ;;
        --help|-?)
            VERBOSE=2
            ;;
    esac
done

panic () {
    echo "ERROR: $@"
    exit 1
}

run () {
  if [ "$VERBOSE" -ge 1 ]; then
    "$@"
  else
    "$@" >/dev/null 2>&1
  fi
}

HOST_OS=$(uname -s)
case $HOST_OS in
    Linux)
        HOST_NUM_CPUS=`cat /proc/cpuinfo | grep processor | wc -l`
        ;;
    Darwin|FreeBsd)
        HOST_NUM_CPUS=`sysctl -n hw.ncpu`
        ;;
    CYGWIN*|*_NT-*)
        HOST_NUM_CPUS=$NUMBER_OF_PROCESSORS
        ;;
    *)  # let's play safe here
        HOST_NUM_CPUS=1
esac


# Return the type of a given file, using the /usr/bin/file command.
# $1: executable path.
# Out: file type string, or empty if the path is wrong.
get_file_type () {
    /usr/bin/file "$1" 2>/dev/null
}

# Return true iff the file type string |$1| contains the expected
# substring |$2|.
# $1: executable file type
# $2: expected file type substring
check_file_type_substring () {
    printf "%s\n" "$1" | grep -q -E -e "$2"
}

# Define EXPECTED_32BIT_FILE_TYPE and EXPECTED_64BIT_FILE_TYPE depending
# on the current target platform. Then EXPECTED_EMULATOR_BITNESS and
# EXPECTED_EMULATOR_FILE_TYPE accordingly.
if [ "$MINGW" ]; then
    EXPECTED_32BIT_FILE_TYPE="PE32 executable \(console\) Intel 80386"
    EXPECTED_64BIT_FILE_TYPE="PE32\+ executable \(console\) x86-64"
    EXPECTED_EMULATOR_BITNESS=32
    EXPECTED_EMULATOR_FILE_TYPE=$EXPECTED_32BIT_FILE_TYPE
elif [ "$HOST_OS" = "Darwin" ]; then
    EXPECTED_32BIT_FILE_TYPE="Mach-O executable i386"
    EXPECTED_64BIT_FILE_TYPE="Mach-O 64-bit executable x86_64"
    EXPECTED_EMULATOR_BITNESS=64
    EXPECTED_EMULATOR_FILE_TYPE=$EXPECTED_64BIT_FILE_TYPE
else
    EXPECTED_32BIT_FILE_TYPE="ELF 32-bit LSB +executable, Intel 80386"
    EXPECTED_64BIT_FILE_TYPE="ELF 32-bit LSB +executable, x86-64"
    EXPECTED_EMULATOR_BITNESS=32
    EXPECTED_EMULATOR_FILE_TYPE=$EXPECTED_32BIT_FILE_TYPE
fi

# Build the binaries from sources.
cd `dirname $0`
rm -rf objs
echo "Configuring build."
run ./android-configure.sh --out-dir=$OUT_DIR "$@" ||
    panic "Configuration error, please run ./android-configure.sh to see why."

echo "Building sources."
run make -j$HOST_NUM_CPUS OBJS_DIR="$OUT_DIR" ||
    panic "Could not build sources, please run 'make' to see why."

# Copy qemu-android binaries, if any.
PREBUILTS_DIR=$ANDROID_EMULATOR_PREBUILTS_DIR
if [ -z "$PREBUILTS_DIR" ]; then
  PREBUILTS_DIR=/tmp/$USER-emulator-prebuilts
fi
QEMU_ANDROID_HOSTS=
if [ -d "$PREBUILTS_DIR/qemu-android" ]; then
    HOST_PREFIX=
    if [ "$MINGW" ]; then
      HOST_PREFIX=windows
    else
      case $HOST_OS in
        Linux)
          HOST_PREFIX=linux
          ;;
        Darwin)
          HOST_PREFIX=darwin
          ;;
      esac
    fi
    if [ "$HOST_PREFIX" ]; then
        QEMU_ANDROID_BINARIES=$(cd "$PREBUILTS_DIR"/qemu-android && ls $HOST_PREFIX-*/qemu-system-* 2>/dev/null || true)
    fi
fi
if [ -z "$QEMU_ANDROID_BINARIES" ]; then
    echo "WARNING: Missing qemu-android prebuilts. Please run rebuild-qemu-android.sh!"
else
    echo "Copying prebuilt $HOST_PREFIX qemu-android binaries to $OUT_DIR/qemu/"
    for QEMU_ANDROID_BINARY in $QEMU_ANDROID_BINARIES; do
        run mkdir -p "$OUT_DIR/qemu/$(dirname "$QEMU_ANDROID_BINARY")" &&
        run cp "$PREBUILTS_DIR"/qemu-android/$QEMU_ANDROID_BINARY \
                $OUT_DIR/qemu/$QEMU_ANDROID_BINARY ||
            panic "Could not copy $HOST_PREFIX/$QEMU_ANDROID_BINARY !?"
    done
fi

RUN_32BIT_TESTS=
RUN_64BIT_TESTS=true
RUN_EMUGEN_TESTS=true

TEST_SHELL=
EXE_SUFFIX=
if [ "$MINGW" ]; then
  TEST_SHELL=wine
  EXE_SUFFIX=.exe

  # Check for Wine on this machine.
  WINE_CMD=$(which $TEST_SHELL 2>/dev/null || true)
  if [ -z "$NO_TESTS" -a -z "$WINE_CMD" ]; then
    echo "WARNING: Wine is not installed on this machine!! Unit tests will be ignored!!"
    NO_TESTS=true
  fi

  RUN_32BIT_TESTS=true
fi

if [ "$HOST_OS" = "Linux" ]; then
    # Linux 32-bit binaries are deprecated but not removed yet!
    RUN_32BIT_TESTS=true
fi


if [ -z "$NO_TESTS" ]; then
    FAILURES=""

    echo "Checking for 'emulator' launcher program."
    EMULATOR=$OUT_DIR/emulator$EXE_SUFFIX
    if [ ! -f "$EMULATOR" ]; then
        echo "    - FAIL: $EMULATOR is missing!"
        FAILURES="$FAILURES emulator"
    fi

    echo "Checking that 'emulator' is a $EXPECTED_EMULATOR_BITNESS-bit program."
    EMULATOR_FILE_TYPE=$(get_file_type "$EMULATOR")
    if ! check_file_type_substring "$EMULATOR_FILE_TYPE" "$EXPECTED_EMULATOR_FILE_TYPE"; then
        echo "    - FAIL: $EMULATOR is not a 32-bit executable!"
        echo "        File type: $EMULATOR_FILE_TYPE"
        echo "        Expected : $EXPECTED_EMULATOR_FILE_TYPE"
        FAILURES="$FAILURES emulator-bitness-check"
    fi

    if [ "$RUN_32BIT_TESTS" ]; then
        echo "Running 32-bit unit test suite."
        for UNIT_TEST in emulator_unittests emugl_common_host_unittests android_skin_unittests; do
        echo "   - $UNIT_TEST"
        run $TEST_SHELL $OUT_DIR/$UNIT_TEST$EXE_SUFFIX || FAILURES="$FAILURES $UNIT_TEST"
        done
    fi

    if [ "$RUN_64BIT_TESTS" ]; then
        echo "Running 64-bit unit test suite."
        for UNIT_TEST in emulator64_unittests emugl64_common_host_unittests android64_skin_unittests; do
            echo "   - $UNIT_TEST"
            run $TEST_SHELL $OUT_DIR/$UNIT_TEST$EXE_SUFFIX || FAILURES="$FAILURES $UNIT_TEST"
        done
    fi

    if [ "$RUN_EMUGEN_TESTS" ]; then
        EMUGEN_UNITTESTS=$OUT_DIR/emugen_unittests
        if [ ! -f "$EMUGEN_UNITTESTS" ]; then
            echo "FAIL: Missing binary: $EMUGEN_UNITTESTS"
            FAILURES="$FAILURES emugen_unittests-binary"
        else
            echo "Running emugen_unittests."
            run $EMUGEN_UNITTESTS ||
                FAILURES="$FAILURES emugen_unittests"
        fi
        echo "Running emugen regression test suite."
        # Note that the binary is always built for the 'build' machine type,
        # I.e. if --mingw is used, it's still a Linux executable.
        EMUGEN=$OUT_DIR/emugen
        if [ ! -f "$EMUGEN" ]; then
            echo "FAIL: Missing 'emugen' binary: $EMUGEN"
            FAILURES="$FAILURES emugen-binary"
        else
            # The first case is for a remote build with package-release.sh
            TEST_SCRIPT=$(dirname "$0")/../opengl/host/tools/emugen/tests/run-tests.sh
            if [ ! -f "$TEST_SCRIPT" ]; then
                # This is the usual location.
                TEST_SCRIPT=$(dirname "$0")/distrib/android-emugl/host/tools/emugen/tests/run-tests.sh
            fi
            if [ ! -f "$TEST_SCRIPT" ]; then
                echo " FAIL: Missing script: $TEST_SCRIPT"
                FAILURES="$FAILURES emugen-test-script"
            else
                run $TEST_SCRIPT --emugen=$EMUGEN ||
                    FAILURES="$FAILURES emugen-test-suite"
            fi
        fi
    fi

    # Check that the windows executables all have icons.
    # First need to locate the windres tool.
    if [ "$MINGW" ]; then
        echo "Checking windows executables icons."
        if [ ! -f "$OUT_DIR/config.make" ]; then
            echo "FAIL: Could not find \$OUT_DIR/config.make !?"
            FAILURES="$FAILURES out-dir-config-make"
        else
            WINDRES=$(awk '/^HOST_WINDRES:=/ { print $2; } $1 == "HOST_WINDRES" { print $3; }' $OUT_DIR/config.make) ||
            if true; then
                echo "FAIL: Could not find host 'windres' program"
                FAILURES="$FAILURES host-windres"
            fi
            EXECUTABLES="emulator.exe emulator-arm.exe emulator-x86.exe emulator-mips.exe"
            EXECUTABLES="$EXECUTABLES emulator64-arm.exe emulator64-x86.exe emulator64-mips.exe"
            EXPECTED_ICONS=14
            for EXEC in $EXECUTABLES; do
                if [ ! -f "$OUT_DIR"/$EXEC ]; then
                    echo "FAIL: Missing windows executable: $EXEC"
                    FAILURES="$FAILURES windows-$EXEC"
                else
                    NUM_ICONS=$($WINDRES --input-format=coff --output-format=rc $OUT_DIR/$EXEC | grep RT_ICON | wc -l)
                    if [ "$NUM_ICONS" != "$EXPECTED_ICONS" ]; then
                        echo "FAIL: Invalid icon count in $EXEC ($NUM_ICONS, expected $EXPECTED_ICONS)"
                        FAILURES="$FAILURES windows-$EXEC-icons"
                    fi
                fi
            done
        fi
    fi
    if [ "$FAILURES" ]; then
        panic "Unit test failures: $FAILURES"
    fi
else
    echo "Ignoring unit tests suite."
fi

echo "Done. !!"

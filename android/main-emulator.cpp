/* Copyright (C) 2011 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/

/* This is the source code to the tiny "emulator" launcher program
 * that is in charge of starting the target-specific emulator binary
 * for a given AVD, i.e. either 'emulator-arm' or 'emulator-x86'
 *
 * This program will be replaced in the future by what is currently
 * known as 'emulator-ui', but is a good placeholder until this
 * migration is completed.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <iostream>
#include <fstream>
#include <streambuf>

#include "android/avd/scanner.h"
#include "android/avd/util.h"
#include "android/base/files/PathUtils.h"
#include "android/base/memory/ScopedPtr.h"
#include "android/base/system/System.h"
#include "android/opengl/emugl_config.h"
#include "android/qt/qt_setup.h"
#include "android/utils/compiler.h"
#include "android/utils/debug.h"
#include "android/utils/exec.h"
#include "android/utils/host_bitness.h"
#include "android/utils/panic.h"
#include "android/utils/path.h"
#include "android/utils/bufprint.h"
#include "android/utils/win32_cmdline_quote.h"

using android::base::ScopedCPtr;
using android::base::System;
using android::base::RunOptions;

#define DEBUG 1

#if DEBUG
#  define D(...)  do { if (android_verbose) printf("emulator:" __VA_ARGS__); } while (0)
#else
#  define D(...)  do{}while(0)
#endif

/* The extension used by dynamic libraries on the host platform */
#ifdef _WIN32
#  define DLL_EXTENSION  ".dll"
#elif defined(__APPLE__)
#  define DLL_EXTENSION  ".dylib"
#else
#  define DLL_EXTENSION  ".so"
#endif

// Name of GPU emulation main library for (32-bit and 64-bit versions)
#define GLES_EMULATION_LIB    "libOpenglRender" DLL_EXTENSION
#define GLES_EMULATION_LIB64  "lib64OpenglRender" DLL_EXTENSION

/* Forward declarations */
static char* getClassicEmulatorPath(const char* progDir,
                                    const char* avdArch,
                                    int* wantedBitness);

static char* getQemuExecutablePath(const char* programPath,
                                   const char* avdArch,
                                   int wantedBitness);

static void updateLibrarySearchPath(int wantedBitness, bool useSystemLibs);

static bool checkAvdSystemDirForKernelRanchu(const char* avdName,
                                             const char* avdArch,
                                             const char* androidOut);

static bool checkForGoogleAPIs(const char* avdName);
static int getApiLevel(const char* avdName);

#ifdef _WIN32
static const char kExeExtension[] = ".exe";
#else
static const char kExeExtension[] = "";
#endif

#define ARRAY_SIZE(x)  (sizeof(x)/sizeof(x[0]))

// Return true if string |str| is in |list|, which is an array of string
// pointers of |listSize| elements.
static bool isStringInList(const char* str,
                           const char* const* list,
                           size_t listSize) {
    size_t n = 0;
    for (n = 0; n < listSize; ++n) {
        if (!strcmp(str, list[n])) {
            return true;
        }
    }
    return false;
}

// Return true if the CPU architecture |avdArch| is supported by QEMU1,
// i.e. the 'goldfish' virtual board.
static bool isCpuArchSupportedByGoldfish(const char* avdArch) {
    static const char* const kSupported[] = {"arm", "mips", "x86", "x86_64"};
    return isStringInList(avdArch, kSupported, ARRAY_SIZE(kSupported));
}


// Return true if the CPU architecture |avdArch| is supported by QEMU2,
// i.e. the 'ranchu' virtual board.
static bool isCpuArchSupportedByRanchu(const char* avdArch) {
    static const char* const kSupported[] =
            {"arm64", "mips", "mips64", "x86", "x86_64"};
    return isStringInList(avdArch, kSupported, ARRAY_SIZE(kSupported));
}

/* Main routine */
int main(int argc, char** argv)
{
    const char* avdName = NULL;
    const char* avdArch = NULL;
    const char* gpu = NULL;
    char*       emulatorPath;
    const char* engine = NULL;
    bool force_32bit = false;
    bool no_window = false;
    bool useSystemLibs = false;

    /* Define ANDROID_EMULATOR_DEBUG to 1 in your environment if you want to
     * see the debug messages from this launcher program.
     */
    const char* debug = getenv("ANDROID_EMULATOR_DEBUG");

    if (debug != NULL && *debug && *debug != '0') {
        android_verbose = 1;
    }

#ifdef __linux__
    /* Define ANDROID_EMULATOR_USE_SYSTEM_LIBS to 1 in your environment if you
     * want the effect of -use-system-libs to be permanent.
     */
    const char* system_libs = getenv("ANDROID_EMULATOR_USE_SYSTEM_LIBS");
    if (system_libs && system_libs[0] && system_libs[0] != '0') {
        useSystemLibs = true;
    }
#endif  // __linux__

    /* Parse command-line and look for
     * 1) an avd name either in the form or '-avd <name>' or '@<name>'
     * 2) '-force-32bit' which always use 32-bit emulator on 64-bit platforms
     * 3) '-verbose'/'-debug-all'/'-debug all'/'-debug-init'/'-debug init'
     *    to enable verbose mode.
     */
    for (int nn = 1; nn < argc; nn++) {
        const char* opt = argv[nn];

        if (!strcmp(opt, "-accel-check")) {
            // forward the option to our answering machine
            auto& sys = *System::get();
            const auto path = sys.findBundledExecutable("emulator-check");
            if (path.empty()) {
                derror("can't find the emulator-check executable "
                       "(corrupted tools installation?)");
                return -1;
            }

            System::ProcessExitCode exit_code;
            bool ret = sys.runCommand(
                    {path, "accel"},
                    RunOptions::WaitForCompletion | RunOptions::ShowOutput,
                    System::kInfinite, &exit_code);
            return ret ? exit_code : -1;
        }

        if (!strcmp(opt,"-qemu"))
            break;

        if (!strcmp(opt,"-verbose") || !strcmp(opt,"-debug-all")
                 || !strcmp(opt,"-debug-init")) {
            android_verbose = 1;
            base_enable_verbose_logs();
        }

        if (!strcmp(opt,"-debug") && nn + 1 < argc &&
            (!strcmp(argv[nn + 1], "all") || !strcmp(argv[nn + 1], "init"))) {
            android_verbose = 1;
            base_enable_verbose_logs();
        }

        if (!strcmp(opt,"-gpu") && nn + 1 < argc) {
            gpu = argv[nn + 1];
            nn++;
            continue;
        }

        if (!strcmp(opt,"-ranchu")) {
            // Nothing: the option is deprecated and defaults to auto-detect.
            continue;
        }

        if (!strcmp(opt, "-engine") && nn + 1 < argc) {
            engine = argv[nn + 1];
            nn++;
            continue;
        }

        if (!strcmp(opt,"-force-32bit")) {
            force_32bit = true;
            continue;
        }

        if (!strcmp(opt,"-no-window")) {
            no_window = true;
            continue;
        }

#ifdef __linux__
        if (!strcmp(opt, "-use-system-libs")) {
            useSystemLibs = true;
            continue;
        }
#endif  // __linux__

        if (!strcmp(opt,"-list-avds")) {
            AvdScanner* scanner = avdScanner_new(NULL);
            for (;;) {
                const char* name = avdScanner_next(scanner);
                if (!name) {
                    break;
                }
                printf("%s\n", name);
            }
            avdScanner_free(scanner);
            exit(0);
        }

        if (!avdName) {
            if (!strcmp(opt,"-avd") && nn+1 < argc) {
                avdName = argv[nn+1];
            }
            else if (opt[0] == '@' && opt[1] != '\0') {
                avdName = opt+1;
            }
        }
    }

    /* If ANDROID_EMULATOR_FORCE_32BIT is set to 'true' or '1' in the
     * environment, set -force-32bit automatically.
     */
    {
        const char kEnvVar[] = "ANDROID_EMULATOR_FORCE_32BIT";
        const char* val = getenv(kEnvVar);
        if (val && (!strcmp(val, "true") || !strcmp(val, "1"))) {
            if (!force_32bit) {
                D("Auto-config: -force-32bit (%s=%s)\n", kEnvVar, val);
                force_32bit = true;
            }
        }
    }

    int hostBitness = android_getHostBitness();
    int wantedBitness = hostBitness;

#if defined(__linux__)
    if (!force_32bit && hostBitness == 32) {
        fprintf(stderr,
"ERROR: 32-bit Linux Android emulator binaries are DEPRECATED, to use them\n"
"       you will have to do at least one of the following:\n"
"\n"
"       - Use the '-force-32bit' option when invoking 'emulator'.\n"
"       - Set ANDROID_EMULATOR_FORCE_32BIT to 'true' in your environment.\n"
"\n"
"       Either one will allow you to use the 32-bit binaries, but please be\n"
"       aware that these will disappear in a future Android SDK release.\n"
"       Consider moving to a 64-bit Linux system before that happens.\n"
"\n"
        );
        exit(1);
    }
#endif  // __linux__

    if (force_32bit) {
        wantedBitness = 32;
    }

#if defined(__APPLE__)
    // Not sure when the android_getHostBitness will break again
    // but we are not shiping 32bit for OSX long time ago.
    // https://code.google.com/p/android/issues/detail?id=196779
    if (force_32bit) {
        fprintf(stderr,
"WARNING: 32-bit OSX Android emulator binaries are not supported, use 64bit.\n");
    }
    wantedBitness = 64;
#endif

    // When running in a platform build environment, point to the output
    // directory where image partition files are located.
    const char* androidOut = NULL;

    /* If there is an AVD name, we're going to extract its target architecture
     * by looking at its config.ini
     */
    if (avdName != NULL) {
        D("Found AVD name '%s'\n", avdName);
        avdArch = path_getAvdTargetArch(avdName);
        D("Found AVD target architecture: %s\n", avdArch);
    } else {
        /* Otherwise, using the ANDROID_PRODUCT_OUT directory */
        androidOut = getenv("ANDROID_PRODUCT_OUT");

        if (androidOut != NULL) {
            D("Found ANDROID_PRODUCT_OUT: %s\n", androidOut);
            avdArch = path_getBuildTargetArch(androidOut);
            D("Found build target architecture: %s\n",
              avdArch ? avdArch : "<NULL>");
        }
    }

    if (avdArch == NULL) {
        avdArch = "x86";
        D("Can't determine target AVD architecture: defaulting to %s\n", avdArch);
    }

    /* Find program directory. */
    const auto progDir = android::base::System::get()->getProgramDirectory();

    enum RanchuState {
        RANCHU_AUTODETECT,
        RANCHU_ON,
        RANCHU_OFF,
    } ranchu = RANCHU_AUTODETECT;

    if (engine) {
        if (!strcmp(engine, "auto")) {
            ranchu = RANCHU_AUTODETECT;
        } else if (!strcmp(engine, "classic")) {
            ranchu = RANCHU_OFF;
        } else if (!strcmp(engine, "qemu2")) {
            ranchu = RANCHU_ON;
        } else {
            APANIC("Invalid -engine value '%s', please use one of: auto, classic, qemu2",
                   engine);
        }
    }

    if (ranchu == RANCHU_AUTODETECT) {
        if (!avdName) {
            ranchu = RANCHU_ON;
        } else {
            // Auto-detect which emulation engine to launch.
            bool cpuHasRanchu = isCpuArchSupportedByRanchu(avdArch);
            bool cpuHasGoldfish = isCpuArchSupportedByGoldfish(avdArch);

            if (cpuHasRanchu) {
                if (cpuHasGoldfish) {
                    // Need to auto-detect the default engine.
                    // TODO: Deal with -kernel <file>, -systemdir <dir> and platform
                    // builds appropriately. For now this only works reliably for
                    // regular SDK AVD configurations.
                    if (checkAvdSystemDirForKernelRanchu(avdName, avdArch,
                                                         androidOut)) {
                        D("Auto-config: -engine qemu2 (based on configuration)\n");
                        ranchu = RANCHU_ON;
                    } else {
                        D("Auto-config: -engine classic (based on configuration)\n");
                        ranchu = RANCHU_OFF;
                    }
                } else {
                    D("Auto-config: -engine qemu2 (%s default)\n", avdArch);
                    ranchu = RANCHU_ON;
                }
            } else if (cpuHasGoldfish) {
                D("Auto-config: -engine classic (%s default)\n", avdArch);
                ranchu = RANCHU_OFF;
            } else {
                APANIC("CPU architecture '%s' is not supported\n", avdArch);
            }
        }
    }

    // Sanity checks.
    if (avdName) {
        if (ranchu == RANCHU_OFF && !isCpuArchSupportedByGoldfish(avdArch)) {
            APANIC("CPU Architecture '%s' is not supported by the classic emulator",
                   avdArch);
        }
        if (ranchu == RANCHU_ON && !isCpuArchSupportedByRanchu(avdArch)) {
            APANIC("CPU Architecture '%s' is not supported by the QEMU2 emulator",
                   avdArch);
        }
    }
#ifdef _WIN32
    // Windows version of Qemu1 works only in x86 mode
    if (ranchu == RANCHU_OFF) {
        wantedBitness = 32;
    }
#endif

    if (ranchu == RANCHU_ON) {
        emulatorPath = getQemuExecutablePath(progDir.c_str(),
                                             avdArch,
                                             wantedBitness);
    } else {
        emulatorPath = getClassicEmulatorPath(progDir.c_str(),
                                              avdArch,
                                              &wantedBitness);
    }
    D("Found target-specific %d-bit emulator binary: %s\n", wantedBitness, emulatorPath);

    /* Replace it in our command-line */
    argv[0] = emulatorPath;

    /* Setup library paths so that bundled standard shared libraries are picked
     * up by the re-exec'ed emulator
     */
    updateLibrarySearchPath(wantedBitness, useSystemLibs);

    /* We need to find the location of the GLES emulation shared libraries
     * and modify either LD_LIBRARY_PATH or PATH accordingly
     */
    bool gpuEnabled = false;
    const char* gpuMode = NULL;

    if (avdName) {
        gpuMode = path_getAvdGpuMode(avdName);
        gpuEnabled = (gpuMode != NULL);
    }

    // Detect if this is google API's

    bool google_apis = checkForGoogleAPIs(avdName);
    int api_level = getApiLevel(avdName);

    bool has_guest_renderer = (!strcmp(avdArch, "x86") ||
                               !strcmp(avdArch, "x86_64")) &&
                               (api_level >= 23) &&
                               google_apis;

    bool blacklisted = false;
    bool on_blacklist = false;

    // If the user has specified a renderer
    // that is neither "auto" nor "host",
    // don't check the blacklist.
    if (!((!gpu && gpuMode && strcmp(gpuMode, "auto") &&
                    strcmp(gpuMode, "host")) ||
                (gpu && strcmp(gpu, "auto") &&
                 strcmp(gpu, "host") &&
                 strcmp(gpu, "on")))) {
         on_blacklist = isHostGpuBlacklisted();
    }

    if (avdName) {
        // This is for testing purposes only.
        android::base::ScopedCPtr<const char> testGpuBlacklist(
                path_getAvdGpuBlacklisted(avdName));
        if (testGpuBlacklist.get()) {
            on_blacklist = !strcmp(testGpuBlacklist.get(), "yes");
        }
    }

    if ((!gpu && gpuMode && !strcmp(gpuMode, "auto")) ||
        (gpu && !strcmp(gpu, "auto"))) {
        if (on_blacklist) {
            fprintf(stderr, "Your GPU drivers may have a bug. "
                    "Switching to software rendering.\n");
        }
        blacklisted = on_blacklist;
        setGpuBlacklistStatus(blacklisted);
    } else if (on_blacklist &&
               ((!gpu && gpuMode && !strcmp(gpuMode, "host"))  ||
                (gpu && !strcmp(gpu, "host")) ||
                (gpu && !strcmp(gpu, "on")))) {
        fprintf(stderr, "Your GPU drivers may have a bug. "
                        "If you experience graphical issues, "
                        "please consider switching to software rendering.\n");
    }

    EmuglConfig config;
    if (!emuglConfig_init(
                &config, gpuEnabled, gpuMode, gpu, wantedBitness, no_window,
                blacklisted, has_guest_renderer)) {
        fprintf(stderr, "ERROR: %s\n", config.status);
        exit(1);
    }
    D("%s\n", config.status);

    emuglConfig_setupEnv(&config);

    /* Add <lib>/qt/ to the library search path. */
    androidQtSetupEnv(wantedBitness);

#ifdef _WIN32
    // Take care of quoting all parameters before sending them to execv().
    // See the "Eveyone quotes command line arguments the wrong way" on
    // MSDN.
    int n;
    for (n = 0; n < argc; ++n) {
        // Technically, this leaks the quoted strings, but we don't care
        // since this process will terminate after the execv() anyway.
        argv[n] = win32_cmdline_quote(argv[n]);
        D("Quoted param: [%s]\n", argv[n]);
    }
#endif

    if (android_verbose) {
        int i;
        printf("emulator: Running :%s\n", emulatorPath);
        for(i = 0; i < argc; i++) {
            printf("emulator: qemu backend: argv[%02d] = \"%s\"\n", i, argv[i]);
        }
        /* Dump final command-line parameters to make debugging easier */
        printf("emulator: Concatenated backend parameters:\n");
        for (i = 0; i < argc; i++) {
            /* To make it easier to copy-paste the output to a command-line,
             * quote anything that contains spaces.
             */
            if (strchr(argv[i], ' ') != NULL) {
                printf(" '%s'", argv[i]);
            } else {
                printf(" %s", argv[i]);
            }
        }
        printf("\n");
    }

    // Launch it with the same set of options !
    // Note that on Windows, the first argument must _not_ be quoted or
    // Windows will fail to find the program.
    safe_execv(emulatorPath, argv);

    /* We could not launch the program ! */
    fprintf(stderr, "Could not launch '%s': %s\n", emulatorPath, strerror(errno));
    return errno;
}

static char* bufprint_emulatorName(char* p,
                                   char* end,
                                   const char* progDir,
                                   const char* prefix,
                                   const char* archSuffix) {
    if (progDir) {
        p = bufprint(p, end, "%s/", progDir);
    }
    p = bufprint(p, end, "%s%s%s", prefix, archSuffix, kExeExtension);
    return p;
}

/* Probe the filesystem to check if an emulator executable named like
 * <progDir>/<prefix><arch> exists.
 *
 * |progDir| is an optional program directory. If NULL, the executable
 * will be searched in the current directory.
 * |archSuffix| is an architecture-specific suffix, like "arm", or 'x86"
 * |wantedBitness| points to an integer describing the wanted bitness of
 * the program. The function might modify it, in the case where it is 64
 * but only 32-bit versions of the executables are found (in this case,
 * |*wantedBitness| is set to 32).
 * On success, returns the path of the executable (string must be freed by
 * the caller). On failure, return NULL.
 */
static char* probeTargetEmulatorPath(const char* progDir,
                                     const char* archSuffix,
                                     int* wantedBitness) {
    char path[PATH_MAX], *pathEnd = path + sizeof(path), *p;

    static const char kEmulatorPrefix[] = "emulator-";
    static const char kEmulator64Prefix[] = "emulator64-";

    // First search for the 64-bit emulator binary.
    if (*wantedBitness == 64) {
        p = bufprint_emulatorName(path,
                                  pathEnd,
                                  progDir,
                                  kEmulator64Prefix,
                                  archSuffix);
        D("Probing program: %s\n", path);
        if (p < pathEnd && path_exists(path)) {
            return strdup(path);
        }
    }

    // Then for the 32-bit one.
    p = bufprint_emulatorName(path,
                                pathEnd,
                                progDir,
                                kEmulatorPrefix,
                                archSuffix);
    D("Probing program: %s\n", path);
    if (p < pathEnd && path_exists(path)) {
        *wantedBitness = 32;
        return strdup(path);
    }

    return NULL;
}

// Find the path to the classic emulator binary that supports CPU architecture
// |avdArch|. |progDir| is the program's directory.
static char* getClassicEmulatorPath(const char* progDir,
                                    const char* avdArch,
                                    int* wantedBitness) {
    const char* emulatorSuffix = emulator_getBackendSuffix(avdArch);
    if (!emulatorSuffix) {
        APANIC("This emulator cannot emulate %s CPUs!\n", avdArch);
    }
    D("Looking for emulator-%s to emulate '%s' CPU\n", emulatorSuffix,
      avdArch);

    char* result = probeTargetEmulatorPath(progDir,
                                           emulatorSuffix,
                                           wantedBitness);
    if (!result) {
        APANIC("Missing emulator engine program for '%s' CPU.\n", avdArch);
    }
    D("return result: %s\n", result);
    return result;
}

// Convert an emulator-specific CPU architecture name |avdArch| into the
// corresponding QEMU one. Return NULL if unknown.
static const char* getQemuArch(const char* avdArch) {
    static const struct {
        const char* arch;
        const char* qemuArch;
    } kQemuArchs[] = {
        {"arm", "armel"},
        {"arm64", "aarch64"},
        {"mips", "mipsel"},
        {"mips64", "mips64el"},
        {"x86","i386"},
        {"x86_64","x86_64"},
    };
    size_t n;
    for (n = 0; n < ARRAY_SIZE(kQemuArchs); ++n) {
        if (!strcmp(avdArch, kQemuArchs[n].arch)) {
            return kQemuArchs[n].qemuArch;
        }
    }
    return NULL;
}

// Return the path of the QEMU executable. |progDir| is the directory
// containing the current program. |avdArch| is the CPU architecture name.
// of the current program (i.e. the 'emulator' launcher).
// Return NULL in case of error.
static char* getQemuExecutablePath(const char* progDir,
                                   const char* avdArch,
                                   int wantedBitness) {
// The host operating system name.
#ifdef __linux__
    static const char kHostOs[] = "linux";
#elif defined(__APPLE__)
    static const char kHostOs[] = "darwin";
#elif defined(_WIN32)
    static const char kHostOs[] = "windows";
#endif
    const char* hostArch = (wantedBitness == 64) ? "x86_64" : "x86";
    const char* qemuArch = getQemuArch(avdArch);
    if (!qemuArch) {
        APANIC("QEMU2 emulator does not support %s CPU architecture", avdArch);
    }

    char fullPath[PATH_MAX];
    char* fullPathEnd = fullPath + sizeof(fullPath);
    char* tail = bufprint(fullPath,
                          fullPathEnd,
                          "%s/qemu/%s-%s/qemu-system-%s%s",
                          progDir,
                          kHostOs,
                          hostArch,
                          qemuArch,
                          kExeExtension);
    if (tail >= fullPathEnd) {
        APANIC("QEMU executable path too long (clipped) [%s]. "
               "Can not use QEMU2 emulator. ", fullPath);
    }
    return strdup(fullPath);
}

static void updateLibrarySearchPath(int wantedBitness, bool useSystemLibs) {
    const char* libSubDir = (wantedBitness == 64) ? "lib64" : "lib";
    char fullPath[PATH_MAX];
    char* tail = fullPath;

    char* launcherDir = get_launcher_directory();
    tail = bufprint(fullPath, fullPath + sizeof(fullPath), "%s/%s", launcherDir,
                    libSubDir);

    if (tail >= fullPath + sizeof(fullPath)) {
        APANIC("Custom library path too long (clipped) [%s]. "
               "Can not use bundled libraries. ",
               fullPath);
    }

    D("Adding library search path: '%s'\n", fullPath);
    add_library_search_dir(fullPath);

#ifdef __linux__
    if (!useSystemLibs) {
        // Use bundled libstdc++
        tail = bufprint(fullPath, fullPath + sizeof(fullPath),
                        "%s/%s/libstdc++", launcherDir, libSubDir);

        if (tail >= fullPath + sizeof(fullPath)) {
            APANIC("Custom library path too long (clipped) [%s]. "
                "Can not use bundled libraries. ",
                fullPath);
        }

        D("Adding library search path: '%s'\n", fullPath);
        add_library_search_dir(fullPath);
    }
#else  // !__linux__
    (void)useSystemLibs;
#endif  // !__linux__

    free(launcherDir);
}

// Verify and AVD's system image directory to see if it supports ranchu.
static bool checkAvdSystemDirForKernelRanchu(const char* avdName,
                                             const char* avdArch,
                                             const char* androidOut) {
    bool result = false;
    char* kernel_file = NULL;

    // For now, just check that a kernel-ranchu file exists. All official
    // system images should have that if they support ranchu.
    if (androidOut) {
        // This is running inside an Android platform build.
        const char* androidBuildTop = getenv("ANDROID_BUILD_TOP");
        if (!androidBuildTop || !androidBuildTop[0]) {
            D("Cannot find Android build top directory, assume no ranchu "
              "support!\n");
            return false;
        }
        D("Found ANDROID_BUILD_TOP: %s\n", androidBuildTop);
        if (!path_exists(androidBuildTop)) {
            D("Invalid Android build top: %s\n", androidBuildTop);
            return false;
        }
        asprintf(&kernel_file, "%s/prebuilts/qemu-kernel/%s/%s",
                 androidBuildTop, avdArch, "kernel-ranchu");
    } else {
        // This is a regular SDK AVD launch.
        char* sdkRootPath = path_getSdkRoot();
        char* systemImagePath = path_getAvdSystemPath(avdName, sdkRootPath);
        asprintf(&kernel_file, "%s/%s", systemImagePath, "kernel-ranchu");

        AFREE(systemImagePath);
        AFREE(sdkRootPath);
    }
    result = path_exists(kernel_file);
    D("Probing for %s: file %s\n", kernel_file, result ? "exists" : "missing");

    AFREE(kernel_file);
    return result;
}

static std::string get_key_val(const char* avdName, const char* key) {
    // Running without an avd (inside android build folder, for instance).
    if (!avdName) {
        return "";
    }

    char* sdkRootPath = path_getSdkRoot();
    char* systemImagePath = path_getAvdSystemPath(avdName, sdkRootPath);

    std::string buildprop_file = std::string(systemImagePath) + "/build.prop";

    std::ifstream file(buildprop_file);
    std::string temp;
    while (std::getline(file, temp)) {
        size_t keypos = temp.find(key);
        if (keypos != std::string::npos) {
            keypos = temp.find("=");
            if (keypos == std::string::npos) {
                // build.prop key without =, crazy!
                continue;
            }
            std::string val = temp.substr(keypos + 1, temp.length() + 1);
            return val;
        }
    }
    return std::string();
}

static bool checkForGoogleAPIs(const char* avdName) {
    std::string api_type = get_key_val(avdName, "ro.product.name");
    return (api_type.find("sdk_google") != std::string::npos) ||
           (api_type.find("google_sdk") != std::string::npos);
}

static int getApiLevel(const char* avdName) {
    std::string api_level = get_key_val(avdName, "ro.build.version.sdk");
    if (api_level.empty()) {
        return -1;
    }

    // for api 10 arm system images, there is no "ro.build.version.sdk"
    // so, return -1;
    try {
        return std::stoi(api_level);
    } catch (const std::exception& e) {
        D("Warning: Cannot find the api level for this AVD: %s\n", e.what());
        return -1;
    }
}

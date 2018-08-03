# Rules to build QEMU2 for the Android emulator from sources.
# This file is included from external/qemu/Makefile or one of its sub-Makefiles.

# NOTE: Previous versions of the build system defined HOST_OS and HOST_ARCH
# instead of BUILD_TARGET_OS and BUILD_TARGET_ARCH, so take care of these to
# ensure this continues to build with both versions.
#
# TODO: Remove this once the external/qemu build script changes are
#       all submitted.
BUILD_TARGET_OS ?= $(HOST_OS)
BUILD_TARGET_ARCH ?= $(HOST_ARCH)
BUILD_TARGET_TAG ?= $(BUILD_TARGET_OS)-$(BUILD_TARGET_ARCH)
BUILD_TARGET_BITS ?= $(HOST_BITS)

# Same for OBJS_DIR -> BUILD_OBJS_DIR
BUILD_OBJS_DIR ?= $(OBJS_DIR)

QEMU2_OLD_LOCAL_PATH := $(LOCAL_PATH)

# Assume this is under android-qemu2-glue/build/ so get up two directories
# to find the top-level one.
LOCAL_PATH := $(QEMU2_TOP_DIR)

# If $(BUILD_TARGET_OS) is $1, then return $2, otherwise $3
qemu2-if-os = $(if $(filter $1,$(BUILD_TARGET_OS)),$2,$3)

# If $(BUILD_TARGET_ARCH) is $1, then return $2, otherwise $3
qemu2-if-build-target-arch = $(if $(filter $1,$(BUILD_TARGET_ARCH)),$2,$3)

# If $(BUILD_TARGET_OS) is not $1, then return $2, otherwise $3
qemu2-ifnot-os = $(if $(filter-out $1,$(BUILD_TARGET_OS)),$2,$3)

# If $(BUILD_TARGET_OS) is windows, return $1, otherwise $2
qemu2-if-windows = $(call qemu2-if-os,windows,$1,$2)

# If $(BUILD_TARGET_OS) is not windows, return $1, otherwise $2
qemu2-if-posix = $(call qemu2-ifnot-os,windows,$1,$2)

qemu2-if-darwin = $(call qemu2-if-os,darwin,$1,$2)
qemu2-if-linux = $(call qemu2-if-os,linux,$1,$2)

QEMU2_AUTO_GENERATED_DIR := $(BUILD_OBJS_DIR)/build/qemu2-qapi-auto-generated

QEMU2_DEPS_TOP_DIR := $(QEMU2_DEPS_PREBUILTS_DIR)/$(BUILD_TARGET_TAG)
QEMU2_DEPS_LDFLAGS := -L$(QEMU2_DEPS_TOP_DIR)/lib

QEMU2_GLIB_INCLUDES := $(QEMU2_DEPS_TOP_DIR)/include/glib-2.0 \
                       $(QEMU2_DEPS_TOP_DIR)/lib/glib-2.0/include

QEMU2_GLIB_LDLIBS := \
    -lglib-2.0 \
    $(call qemu2-if-darwin, -liconv -lintl) \
    $(call qemu2-if-linux, -lpthread -lrt) \
    $(call qemu2-if-windows, -lole32) \

QEMU2_PIXMAN_INCLUDES := $(QEMU2_DEPS_TOP_DIR)/include/pixman-1
QEMU2_PIXMAN_LDLIBS := -lpixman-1

QEMU2_SDL2_INCLUDES := $(QEMU2_DEPS_TOP_DIR)/include/SDL2
QEMU2_SDL2_LDLIBS := \
    $(call qemu2-if-windows, \
        -lmingw32 \
    ) \
    -lSDL2 \
    $(call qemu2-if-linux, \
        -lX11 \
    ) \
    $(call qemu2-if-darwin,, \
        -lSDL2main \
    ) \
    $(call qemu2-if-windows, \
        -limm32 \
        -ldinput8 \
        -ldxguid \
        -ldxerr8 \
        -luser32 \
        -lgdi32 \
        -lwinmm \
        -lole32 \
        -loleaut32 \
        -lpsapi \
        -lshell32 \
        -lversion \
        -luuid \
    , \
        -ldl \
    ) \

ifeq (darwin,$(BUILD_TARGET_OS))
# NOTE: Because the following contain commas, we cannot use qemu2-if-darwin!
QEMU2_SDL2_LDLIBS += \
    -Wl,-framework,OpenGL \
    -Wl,-framework,ForceFeedback \
    -lobjc \
    -Wl,-framework,Cocoa \
    -Wl,-framework,Carbon \
    -Wl,-framework,IOKit \
    -Wl,-framework,CoreAudio \
    -Wl,-framework,AudioToolbox \
    -Wl,-framework,AudioUnit \

endif

# Ensure config-host.h can be found properly.
QEMU2_INCLUDES := $(LOCAL_PATH)/android-qemu2-glue/config/$(BUILD_TARGET_TAG)

ifeq ($(BUILD_TARGET_OS),linux)
  QEMU2_INCLUDES += $(QEMU2_TOP_DIR)/linux-headers
endif

# Ensure QEMU2 headers are found.
QEMU2_INCLUDES += \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/include \
    $(QEMU2_AUTO_GENERATED_DIR) \

QEMU2_INCLUDES += $(QEMU2_GLIB_INCLUDES) \
                  $(QEMU2_PIXMAN_INCLUDES) \
                  $(LIBUSB_INCLUDES) \
                  $(VIRGLRENDERER_INCLUDES)

QEMU2_CFLAGS := \
    $(EMULATOR_COMMON_CFLAGS) \
    $(call qemu2-if-windows,\
        -DWIN32_LEAN_AND_MEAN \
        -D__USE_MINGW_ANSI_STDIO=1 \
        -mms-bitfields) \
    -fno-strict-aliasing \
    -fno-common \
    $(LIBCURL_CFLAGS) \
    -D_GNU_SOURCE \
    -D_FILE_OFFSET_BITS=64 \
    $(call qemu2-if-darwin, -Wno-initializer-overrides) \
    $(call if-target-clang,\
        -Wno-address-of-packed-member \
        -Wno-tautological-pointer-compare \
        -Wno-tautological-compare \
        -Wno-tautological-pointer-compare \
        -Wno-format-security) \

QEMU2_CFLAGS += \
    -Wno-unused-function \
    $(call if-target-clang,, \
        -Wno-unused-variable \
        -Wno-unused-but-set-variable \
        -Wno-maybe-uninitialized \
        ) \

# Enable faster migration code when saving RAM to a snapshot
QEMU2_CFLAGS += \
    -DCONFIG_MIGRATION_RAM_SINGLE_ITERATION

# Enable VIRGL
QEMU2_CFLAGS += \
    -DCONFIG_VIRGL

#include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-glue.mk

#include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-qt.mk

include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-sources.mk

# A static library containing target-independent code
$(call start-emulator-library,libqemu2-common)

LOCAL_CFLAGS += \
    $(QEMU2_CFLAGS) \
    -DPOISON_CONFIG_ANDROID \

LOCAL_C_INCLUDES += \
    $(QEMU2_INCLUDES) \
    $(LOCAL_PATH)/slirp \
    $(LOCAL_PATH)/tcg \
    $(ZLIB_INCLUDES) \
    $(LIBBLUEZ_INCLUDES) \

LOCAL_SRC_FILES += \
    $(QEMU2_AUTO_GENERATED_DIR)/trace-root.c \
    $(QEMU2_COMMON_SOURCES) \
    $(QEMU2_COMMON_SOURCES_$(BUILD_TARGET_TAG)) \

$(call gen-hw-config-defs)
QEMU2_INCLUDES += $(QEMU_HW_CONFIG_DEFS_INCLUDES)

$(call end-emulator-library)

# Static library containing all the utilities.
$(call start-emulator-library,libqemu2-util)
LOCAL_CFLAGS += \
    $(QEMU2_CFLAGS) \
    -DPOISON_CONFIG_ANDROID \

LOCAL_C_INCLUDES += \
    $(QEMU2_INCLUDES) \

LOCAL_SRC_FILES += \
    $(QEMU2_LIB_qemuutil) \
    $(QEMU2_LIB_qemuutil_$(BUILD_TARGET_TAG)) \

# Include the simple tracer if requested
ifneq (,$(QEMU2_TRACE))
    LOCAL_SRC_FILES += trace/simple.c
    LOCAL_CFLAGS += -DCONFIG_TRACE_SIMPLE=1
endif
$(call end-emulator-library)


include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-glue.mk

QEMU2_TARGET := x86
include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-target.mk

QEMU2_TARGET := x86_64
include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-target.mk

ifeq (,$(CONFIG_MIN_BUILD))
    QEMU2_TARGET := arm
    include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-target.mk

    QEMU2_TARGET := arm64
    include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-target.mk

    QEMU2_TARGET := mips
    include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-target.mk

    QEMU2_TARGET := mips64
    include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu2-target.mk
endif   # !CONFIG_MIN_BUILD

# TODO(jansene): This gets included twice in the windows build (32 bit/64 bit)
# causing targets to be overridden.
include $(LOCAL_PATH)/android-qemu2-glue/build/Makefile.qemu-img.mk

LOCAL_PATH := $(QEMU2_OLD_LOCAL_PATH)

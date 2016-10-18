# A static library containing the android-emu glue code

QEMU2_GLUE_INCLUDES := $(ANDROID_EMU_INCLUDES)

$(call start-emulator-library,libqemu2-glue)

LOCAL_CFLAGS += $(QEMU2_CFLAGS)

LOCAL_C_INCLUDES += \
    $(QEMU2_INCLUDES) \
    $(QEMU2_GLUE_INCLUDES) \
    $(LOCAL_PATH)/slirp \

LOCAL_SRC_FILES := \
    android_qemud.cpp \
    base/async/Looper.cpp \
    base/files/QemuFileStream.cpp \
    display.cpp \
    emulation/android_pipe_device.cpp \
    emulation/charpipe.c \
    emulation/CharSerialLine.cpp \
    emulation/goldfish_sync.cpp \
    emulation/serial_line.cpp \
    emulation/VmLock.cpp \
    looper-qemu.cpp \
    net-android.cpp \
    qemu-battery-agent-impl.c \
    qemu-cellular-agent-impl.c \
    qemu-clipboard-agent-impl.cpp \
    qemu-display-agent-impl.cpp \
    qemu-finger-agent-impl.c \
    qemu-location-agent-impl.c \
    qemu-net-agent-impl.c \
    qemu-sensors-agent-impl.c \
    qemu-setup.cpp \
    qemu-telephony-agent-impl.c \
    qemu-user-event-agent-impl.c \
    qemu-vm-operations-impl.c \
    qemu-window-agent-impl.c \
    telephony/modem_init.c \
    utils/stream.cpp \

LOCAL_SRC_FILES := $(LOCAL_SRC_FILES:%=android-qemu2-glue/%)

$(call end-emulator-library)

QEMU2_GLUE_STATIC_LIBRARIES := \
    libqemu2-glue \
    emulator-libui \
    $(EMULATOR_LIBUI_STATIC_LIBRARIES)

QEMU2_GLUE_LDFLAGS := $(EMULATOR_LIBUI_LDFLAGS)

QEMU2_GLUE_LDLIBS := $(EMULATOR_LIBUI_LDLIBS)

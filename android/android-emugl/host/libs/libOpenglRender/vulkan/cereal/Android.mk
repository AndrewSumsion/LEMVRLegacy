
# Autogenerated makefile
# android/android-emugl/host/libs/libOpenglRender/vulkan-registry/xml/genvk.py -registry android/android-emugl/host/libs/libOpenglRender/vulkan-registry/xml/vk.xml cereal -o android/android-emugl/host/libs/libOpenglRender/vulkan/cereal
# Please do not modify directly;
# re-run android/scripts/generate-vulkan-sources.sh,
# or directly from Python by defining:
# VULKAN_REGISTRY_XML_DIR : Directory containing genvk.py and vk.xml
# CEREAL_OUTPUT_DIR: Where to put the generated sources.
# python3 $VULKAN_REGISTRY_XML_DIR/genvk.py -registry $VULKAN_REGISTRY_XML_DIR/vk.xml cereal -o $CEREAL_OUTPUT_DIR

LOCAL_PATH := $(call my-dir)

# For Vulkan libraries

cereal_C_INCLUDES := \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/../ \
    $(EMUGL_PATH)/host/include/vulkan \

cereal_STATIC_LIBRARIES := \
    android-emu \
    android-emu-base \


$(call emugl-begin-static-library,lib$(BUILD_TARGET_SUFFIX)OpenglRender_vulkan_cereal)

LOCAL_C_INCLUDES += $(cereal_C_INCLUDES)

LOCAL_STATIC_LIBRARIES += $(cereal_STATIC_LIBRARIES)

LOCAL_SRC_FILES := \
    common/goldfish_vk_marshaling.cpp \
    guest/goldfish_vk_frontend.cpp \
    common/goldfish_vk_testing.cpp \
    common/goldfish_vk_deepcopy.cpp \
    common/goldfish_vk_handlemap.cpp \

$(call emugl-end-module)

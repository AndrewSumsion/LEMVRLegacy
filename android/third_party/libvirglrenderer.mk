$(call define-emulator-prebuilt-library,\
    emulator-virglrenderer,\
    $(VIRGLRENDERER_PREBUILTS_DIR)/$(BUILD_TARGET_TAG)/lib/libvirglrenderer.a)

VIRGLRENDERER_INCLUDES := $(VIRGLRENDERER_PREBUILTS_DIR)/$(BUILD_TARGET_TAG)/include/virgl

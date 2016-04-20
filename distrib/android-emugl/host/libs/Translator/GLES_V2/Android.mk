LOCAL_PATH := $(call my-dir)

host_common_SRC_FILES := \
     GLESv2Imp.cpp       \
     GLESv2Context.cpp   \
     GLESv2Validate.cpp  \
     ShaderParser.cpp    \
     ShaderValidator.cpp \
     ProgramData.cpp


### GLES_V2 host implementation (On top of OpenGL) ########################
$(call emugl-begin-shared-library,lib$(BUILD_TARGET_SUFFIX)GLES_V2_translator)
$(call emugl-import, libGLcommon)

LOCAL_SRC_FILES := $(host_common_SRC_FILES)

$(call emugl-end-module)

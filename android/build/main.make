# Copyright (C) 2008 The Android Open Source Project
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
#

# disable implicit rules
.SUFFIXES:
%:: %,v
%:: RCS/%
%:: RCS/%,v
%:: s.%
%:: SCCS/s.%
%.c: %.w %.ch

# this is a set of definitions that allow the usage of Makefile.android
# even if we're not using the Android build system.
#

_BUILD_CORE_DIR  := android/build
OBJS_DIR      := objs
CONFIG_MAKE   := $(OBJS_DIR)/build/config.make
CONFIG_HOST_H := $(OBJS_DIR)/build/config-host.h
_BUILD_SYMBOLS_DIR := $(OBJS_DIR)/build/symbols

ifeq ($(wildcard $(CONFIG_MAKE)),)
    $(error "The configuration file '$(CONFIG_MAKE)' doesn't exist, please run the 'android-configure.sh' script")
endif

include $(CONFIG_MAKE)

include $(_BUILD_CORE_DIR)/definitions.make

.PHONY: all libraries executables clean clean-config clean-objs-dir \
        clean-executables clean-libraries

CLEAR_VARS                := $(_BUILD_CORE_DIR)/clear_vars.make
BUILD_HOST_EXECUTABLE     := $(_BUILD_CORE_DIR)/host_executable.make
BUILD_HOST_STATIC_LIBRARY := $(_BUILD_CORE_DIR)/host_static_library.make
BUILD_HOST_SHARED_LIBRARY := $(_BUILD_CORE_DIR)/host_shared_library.make
PREBUILT_STATIC_LIBRARY := $(_BUILD_CORE_DIR)/prebuilt_static_library.make

DEPENDENCY_DIRS :=

all: libraries executables symbols
EXECUTABLES :=
SYMBOLS     :=
LIBRARIES   :=
INTERMEDIATE_SYMBOLS :=

clean: clean-intermediates

distclean: clean clean-config

# let's roll
include Makefile.top.mk

libraries: $(LIBRARIES)
executables: $(EXECUTABLES)
symbols: $(INTERMEDIATE_SYMBOLS) $(SYMBOLS)

clean-intermediates:
	rm -rf $(OBJS_DIR)/intermediates $(EXECUTABLES) $(LIBRARIES) $(SYMBOLS) $(_BUILD_SYMBOLS_DIR)

clean-config:
	rm -f $(CONFIG_MAKE) $(CONFIG_HOST_H)

# include dependency information
DEPENDENCY_DIRS := $(sort $(DEPENDENCY_DIRS))
-include $(wildcard $(DEPENDENCY_DIRS:%=%/*.d))

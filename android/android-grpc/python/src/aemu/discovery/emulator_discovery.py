#  Copyright (C) 2020 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import logging
import os
import platform
import re

from aemu.discovery.emulator_description import EmulatorDescription

try:
    from urllib.parse import urlparse
except:
    from urlparse import urlparse


class EmulatorDiscovery(object):
    """This class discovers all the running _emulators by parsing the available discovery files."""

    ANDROID_SUBDIR = ".android"

    # Format of our discovery files.
    _PID_FILE_ = re.compile("pid_(\\d+).ini")

    def __init__(self):
        self.discovery_dirs = get_discovery_directories()
        self.discover()

    def discover(self):
        """Discovers all running _emulators by scanning the discovery dir."""
        self._emulators = set()
        for discovery_dir in self.discovery_dirs:
            logging.debug("Discovering emulators in %s", discovery_dir)
            if os.path.exists(discovery_dir):
                for file in os.listdir(discovery_dir):
                    m = self._PID_FILE_.match(file)
                    if m:
                        logging.debug("Found %s", file)
                        emu = self._parse_ini(os.path.join(discovery_dir, file))
                        if emu:
                            self._emulators.add(EmulatorDescription(m.group(1), emu))

    def _parse_ini(self, ini_file):
        """Parse an emulator ini file."""
        logging.debug("Discovering emulator: %s", ini_file)
        emu = dict()
        with open(ini_file, "r") as ini:
            for line in ini.readlines():
                line = line.strip()
                if "=" in line:
                    isi = line.index("=")
                    emu[line[:isi]] = line[isi + 1 :]
        return emu

    def available(self):
        return len(self._emulators)

    def emulators(self):
        return frozenset(self._emulators)

    def find_emulator(self, prop, value):
        """Finds the emulator description by checking if the property contains the given value."""
        for emu in self._emulators:
            if emu.get(prop) == value:
                return emu

        return None

    def find_by_pid(self, pid):
        """Finds the emulator description by pid."""
        return self.find_emulator("pid", str(pid))

    def first(self):
        """Gets the first discovered emulator."""
        return next(iter(self._emulators))


def get_discovery_directories():
    """Gets all the discovery directories that could contain emulator pid files."""
    path = None
    if platform.system() == "Windows" and "LOCALAPPDATA" in os.environ:
        path = os.path.join(os.environ.get("LOCALAPPDATA"), "Temp")
    if platform.system() == "Linux":
        path = os.environ.get("XDG_RUNTIME_DIR")
        if path is None or not os.path.exists(path):
            path = os.path.join("run", "user", str(os.getuid()))
    if platform.system() == "Darwin" and "HOME" in os.environ:
        path = os.path.join(
            os.environ.get("HOME"), "Library", "Caches", "TemporaryItems"
        )

    paths = _get_user_directories()
    paths.append(path)
    return [os.path.join(path, "avd", "running") for path in paths if path != None]


def _get_user_directories():
    # See ConfigDirs::getUserDirectory()
    logging.debug("Retrieving user directories")
    paths = []

    if "ANDROID_EMULATOR_HOME" in os.environ:
        paths.append(os.environ.get("ANDROID_EMULATOR_HOME"))

    if "ANDROID_SDK_HOME" in os.environ:
        paths.append(
            os.path.join(
                os.environ.get("ANDROID_SDK_HOME"), EmulatorDiscovery.ANDROID_SUBDIR
            )
        )

    if "ANDROID_AVD_HOME" in os.environ:
        paths.append(os.environ.get("ANDROID_AVD_HOME"))

    paths.append(os.path.join(os.environ.get("HOME"), EmulatorDiscovery.ANDROID_SUBDIR))
    return paths


def get_default_emulator():
    """Returns the first discovered emulator.

    Useful if you expect only one running emulator."""
    return EmulatorDiscovery().first()

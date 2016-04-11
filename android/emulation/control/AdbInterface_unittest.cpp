// Copyright (C) 2016 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/emulation/control/AdbInterface.h"

#include "android/base/testing/TestSystem.h"
#include "android/emulation/ConfigDirs.h"

#include <gtest/gtest.h>

#include <fstream>

using android::base::System;
using android::base::TestSystem;
using android::base::TestTempDir;
using android::emulation::AdbInterface;

TEST(AdbInterface, freshAdbVersion) {
    TestSystem system("/progdir", System::kProgramBitness, "/homedir",
                      "/appdir");
    TestTempDir* dir = system.getTempRoot();
    ASSERT_TRUE(dir->makeSubDir("Sdk"));
    ASSERT_TRUE(dir->makeSubDir("Sdk/platform-tools"));
    std::string output_file =
        dir->makeSubPath("Sdk/platform-tools/source.properties");
    std::ofstream ofs(output_file);
    ASSERT_TRUE(ofs.is_open());
    ofs << "### Comment\nArchive.HostOs=linux\nPkg.License=\\nNoliense\n"
           "Pkg.LicenseRef=android-sdk-license\nPkg.Revision=23.1.0\n"
           "Pkg.SourceUrl=https\\://dl.google.com/android/repository/repository-12.xml\n";
    ofs.close();
    system.envSet("ANDROID_SDK_ROOT", std::string(dir->path()) +"/Sdk");
    AdbInterface adb;
    EXPECT_TRUE(adb.isAdbVersionCurrent());
    EXPECT_EQ(std::string(dir->path()) + "/Sdk/platform-tools/adb", adb.detectedAdbPath());
}


TEST(AdbInterface, staleAdbMinorVersion) {
    TestSystem system("/progdir", System::kProgramBitness, "/homedir",
                      "/appdir");
    TestTempDir* dir = system.getTempRoot();
    ASSERT_TRUE(dir->makeSubDir("Sdk"));
    ASSERT_TRUE(dir->makeSubDir("Sdk/platform-tools"));
    std::string output_file =
        dir->makeSubPath("Sdk/platform-tools/source.properties");
    std::ofstream ofs(output_file);
    ASSERT_TRUE(ofs.is_open());
    ofs << "### Comment\nArchive.HostOs=linux\nPkg.License=\\nNoliense\n"
           "Pkg.LicenseRef=android-sdk-license\nPkg.Revision=23.0.0\n"
           "Pkg.SourceUrl=https\\://dl.google.com/android/repository/repository-12.xml\n";
    ofs.close();
    system.envSet("ANDROID_SDK_ROOT", std::string(dir->path()) + "/Sdk");
    AdbInterface adb;
    EXPECT_FALSE(adb.isAdbVersionCurrent());
    EXPECT_EQ(std::string(dir->path()) + "/Sdk/platform-tools/adb", adb.detectedAdbPath());
}

TEST(AdbInterface, staleAdbMajorVersion) {
    TestSystem system("/progdir", System::kProgramBitness, "/homedir",
                      "/appdir");
    TestTempDir* dir = system.getTempRoot();
    ASSERT_TRUE(dir->makeSubDir("Sdk"));
    ASSERT_TRUE(dir->makeSubDir("Sdk/platform-tools"));
    std::string output_file =
        dir->makeSubPath("Sdk/platform-tools/source.properties");
    std::ofstream ofs(output_file);
    ASSERT_TRUE(ofs.is_open());
    ofs << "### Comment\nArchive.HostOs=linux\nPkg.License=\\nNoliense\n"
           "Pkg.LicenseRef=android-sdk-license\nPkg.Revision=22.1.0\n"
           "Pkg.SourceUrl=https\\://dl.google.com/android/repository/repository-12.xml\n";
    ofs.close();
    system.envSet("ANDROID_SDK_ROOT", std::string(dir->path()) + "/Sdk");
    AdbInterface adb;
    EXPECT_FALSE(adb.isAdbVersionCurrent());
    EXPECT_EQ(std::string(dir->path()) + "/Sdk/platform-tools/adb", adb.detectedAdbPath());
}


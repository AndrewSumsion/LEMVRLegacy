/* Copyright (C) 2016 The Android Open Source Project
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
#include "sim_access_rules.h"

#include "android/base/containers/Lookup.h"
#include "android/base/memory/LazyInstance.h"
#include "android/cmdline-option.h"
#include "android/telephony/TagLengthValue.h"
#include "android/telephony/proto/sim_access_rules.pb.h"
#include "android/utils/debug.h"

#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "google/protobuf/text_format.h"

#include <fstream>
#include <string>
#include <unordered_map>

using ::google::protobuf::TextFormat;
using ::google::protobuf::io::IstreamInputStream;

namespace android {

struct SimAccessRules {
    // Map DF name (also known as Application ID, AID) to a set of access rules
    const std::unordered_map<std::string, AllRefArDo> kDefaultAccessRules = {
            {"A00000015141434C00",

             // This is the hash of the certificate used to sign the CTS test
             // app for this feature. The PermArDo is a set of permission bits
             // that are currently ignored. The specification at
             // https://source.android.com/devices/tech/config/uicc.html
             // mentions that the field is required but doesn't mention what it
             // should contain. The platform code (as of this writing) reads the
             // value but doesn't do anything with it.
             AllRefArDo{RefArDo{
                     RefDo{DeviceAppIdRefDo{
                                   "61ed377e85d386a8dfee6b864bd85b0bfaa5af81"},
                           PkgRefDo{"android.carrierapi.cts"}},
                     ArDo{PermArDo{"0000000000000000"}}}}}};
};

static android::base::LazyInstance<SimAccessRules> sGlobals = {};

RefDo parseRefDo(const android_emulator::RefDo& input) {
    if (!input.has_device_app_id_ref_do()) {
        dwarning(
                "No device_app_id_ref_do in proto, using empty "
                "DeviceAppIdRefDo.");
    }
    if (!input.has_pkg_ref_do()) {
        return RefDo(DeviceAppIdRefDo(input.device_app_id_ref_do()));
    } else {
        return RefDo(DeviceAppIdRefDo(input.device_app_id_ref_do()),
                     PkgRefDo(input.pkg_ref_do()));
    }
}

ApduArDo parseApduArDo(const android_emulator::ArDo::Apdu& input) {
    if (input.has_general_access_rule()) {
        return ApduArDo(static_cast<ApduArDo::Allow>(input.general_access_rule()));
    } else {
        std::vector<std::string> rules;
        for (auto& rule : input.specific_access_rules().rules()) {
            rules.push_back(rule);
        }
        return ApduArDo(rules);
    }
}

ArDo parseArDo(const android_emulator::ArDo& input) {
    if (input.has_perm_ar_do()) {
        if (input.has_apdu_ar_do() || input.has_ncf_ar_do()) {
            dwarning(
                    "Found invalid combination of ((apdo_ar_do || nfc_ar_do) "
                    "&& perm_ar_do) in proto, using only perm_ar_do.");
        }
        return ArDo(PermArDo(input.perm_ar_do()));
    } else if (input.has_apdu_ar_do() && input.has_ncf_ar_do()) {
        return ArDo(parseApduArDo(input.apdu_ar_do()),
                    NfcArDo(static_cast<NfcArDo::Allow>(input.ncf_ar_do())));
    } else if (input.has_apdu_ar_do()) {
        return ArDo(parseApduArDo(input.apdu_ar_do()));
    } else if (input.has_ncf_ar_do()) {
        return ArDo(NfcArDo(static_cast<NfcArDo::Allow>(input.ncf_ar_do())));
    } else {
        dwarning("No ar_do found in proto, using default PermArDo.");
        return ArDo{PermArDo{"0000000000000000"}};
    }
}

AllRefArDo parseAllRefArDo(const android_emulator::AllRefArDo& input) {
    std::vector<RefArDo> refArDos;
    for (auto& refArDo : input.ref_ar_dos()) {
        auto refDo = parseRefDo(refArDo.ref_do());
        auto arDo = parseArDo(refArDo.ar_do());
        refArDos.emplace_back(refDo, arDo);
    }
    return AllRefArDo(refArDos);
}

std::unordered_map<std::string, AllRefArDo> parseSimAccessRules(
        const android_emulator::SimAccessRules& input) {
    std::unordered_map<std::string, AllRefArDo> simAccessRules;
    for (auto& accessRule : input.sim_access_rules()) {
        simAccessRules.emplace(accessRule.first,
                               parseAllRefArDo(accessRule.second));
    }
    return simAccessRules;
}

}  // namespace android

extern "C" const char* sim_get_access_rules(const char* name) {
    std::unordered_map<std::string, android::AllRefArDo> simAccessRules;

    if (android_cmdLineOptions &&
        android_cmdLineOptions->sim_access_rules_file != nullptr) {
        std::ifstream file(android_cmdLineOptions->sim_access_rules_file);
        if (file.is_open()) {
            IstreamInputStream istream(&file);
            android_emulator::SimAccessRules sim_access_rules_proto;
            TextFormat::Parse(&istream, &sim_access_rules_proto);
            simAccessRules =
                    android::parseSimAccessRules(sim_access_rules_proto);
        } else {
            dwarning("Failed to open file '%s', using default SIM access rules",
                     android_cmdLineOptions->sim_access_rules_file);
            simAccessRules = android::sGlobals->kDefaultAccessRules;
        }
    } else {
        simAccessRules = android::sGlobals->kDefaultAccessRules;
    }
    const auto rule = android::base::find(simAccessRules, name);
    return rule ? rule->c_str() : nullptr;
}

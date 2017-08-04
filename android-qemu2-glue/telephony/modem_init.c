// Copyright 2015 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android-qemu2-glue/telephony/modem_init.h"

#include "android/telephony/modem_driver.h"
#include "android-qemu2-glue/utils/stream.h"

#include "qemu/osdep.h"
#include "hw/hw.h"

#include <assert.h>

extern int sim_is_present();

#define MODEM_DEV_STATE_SAVE_VERSION 1

static void modem_state_save(QEMUFile* file, void* opaque)
{
    Stream* const s = stream_from_qemufile(file);
    amodem_state_save((AModem)opaque, (SysFile*)s);
    stream_free(s);
}

static int modem_state_load(QEMUFile* file, void* opaque, int version_id)
{
    if (version_id != MODEM_DEV_STATE_SAVE_VERSION)
        return -1;

    Stream* const s = stream_from_qemufile(file);
    const int res = amodem_state_load((AModem)opaque, (SysFile*)s);
    stream_free(s);

    return res;
}


void qemu_android_modem_init(int base_port) {
    android_modem_init(base_port, sim_is_present());

    assert(android_modem_serial_line != NULL);

    register_savevm(NULL,
                    "android_modem",
                    0,
                    MODEM_DEV_STATE_SAVE_VERSION,
                    modem_state_save,
                    modem_state_load,
                    android_modem);
}

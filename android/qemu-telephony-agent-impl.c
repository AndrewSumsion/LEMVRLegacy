/* Copyright (C) 2015 The Android Open Source Project
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

#include "android/qemu-control-impl.h"

#include "android/emulation/control/telephony_agent.h"
#include "android/telephony/modem.h"
#include "telephony/modem_driver.h"

#include <ctype.h>
#include <stdio.h>

static int gsm_number_is_bad(const char*);

static TelephonyResponse telephony_telephonyCmd(TelephonyOperation op,
                                                const char *phoneNumber)
{
    int resp;
    int holdCommand;

    switch (op) {
        case Tel_Op_Init_Call:
            if (gsm_number_is_bad(phoneNumber)) {
                return Tel_Resp_Bad_Number;
            }

            if (!android_modem) {
                printf("%s: Do not have an Android modem\n", __FILE__);
                return Tel_Resp_Action_Failed;
            }

            amodem_add_inbound_call(android_modem, phoneNumber);
            return Tel_Resp_OK;

        case Tel_Op_Accept_Call:
        case Tel_Op_Reject_Call_Explicit:
        case Tel_Op_Reject_Call_Busy:
            printf("===== telephony-agents-impl.c: Operation is not implemented\n"); // ??

        case Tel_Op_Disconnect_Call:
            if (gsm_number_is_bad(phoneNumber)) {
                return Tel_Resp_Bad_Number;
            }

            if (!android_modem) {
                printf("%s: Do not have an Android modem\n", __FILE__);
                return Tel_Resp_Action_Failed;
            }

            resp = amodem_disconnect_call(android_modem, phoneNumber);
            return (resp < 0) ? Tel_Resp_Invalid_Action : Tel_Resp_OK;

        case Tel_Op_Place_Call_On_Hold:
        case Tel_Op_Take_Call_Off_Hold:
            if (gsm_number_is_bad(phoneNumber)) {
                return Tel_Resp_Bad_Number;
            }

            if (!android_modem) {
                printf("%s: Do not have an Android modem\n", __FILE__);
                return Tel_Resp_Action_Failed;
            }

            holdCommand = (op == Tel_Op_Place_Call_On_Hold) ? A_CALL_HELD : A_CALL_ACTIVE;
            resp = amodem_update_call(android_modem, phoneNumber, holdCommand);

            return (resp < 0) ? Tel_Resp_Invalid_Action : Tel_Resp_OK;

        default:
            return Tel_Resp_Bad_Operation;
    }
}


// TODO: This is very similar to 'gsm_check_number' in android-qemu1-glue/console.c
//       I should probably instead use sms_address_from_str() in telephony/sms.c
static int
gsm_number_is_bad(const char* numStr)
{
    int  idx;
    int  nDigits = 0;

    if (!numStr) return 1;

    for (idx = 0; numStr[idx] != 0; idx++) {
        int  c = numStr[idx];
        if ( isdigit(c) ) {
            nDigits++;
        } else if (c != '+' && c != '#') {
            return 1;
        }
    }

    return (nDigits <= 0);
}

static AModem telephony_getModem() {
    return android_modem;
}

static const QAndroidTelephonyAgent sQAndroidTelephonyAgent = {
    .telephonyCmd = telephony_telephonyCmd,
    .getModem = telephony_getModem
};
const QAndroidTelephonyAgent* const gQAndroidTelephonyAgent =
        &sQAndroidTelephonyAgent;

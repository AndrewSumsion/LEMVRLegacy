/*
** Copyright (c) 2014, Intel Corporation
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

#pragma once

#include "android/utils/compiler.h"

#include <stddef.h>
#include <stdint.h>

ANDROID_BEGIN_HEADER

/*
 * android_get_x86_cpuid_function_max: retrieve x86 CPUID function max
 * supported by this processor.  This is corresponds to the value passed
 * to android_get_x86_cpuid |function| parameter
 */
uint32_t android_get_x86_cpuid_function_max();

/*
 * android_get_x86_cpuid_extended_function_max: retrieve x86 CPUID extended
 * function max supported by this processor.  This is corresponds to the
 * value passed to android_get_x86_cpuid |function| parameter
 */
uint32_t android_get_x86_cpuid_extended_function_max();

/* The following list of CPUID features is based on Table 1 in section 'ABI
 * Management' of NDK Programmer's Guide (Android NDK r10c). */
/* Applicable when calling CPUID with EAX=1 */
#define CPUID_EDX_MMX      (1 << 23)
#define CPUID_EDX_SSE      (1 << 25)
#define CPUID_EDX_SSE2     (1 << 26)
#define CPUID_ECX_SSE3     (1 << 0)
#define CPUID_ECX_SSSE3    (1 << 9)
#define CPUID_ECX_SSE41    (1 << 19)
#define CPUID_ECX_SSE42    (1 << 20)
#define CPUID_ECX_POPCNT   (1 << 23)

/*
 * android_get_x86_cpuid: retrieve x86 CPUID for host CPU.
 *
 * Executes the x86 CPUID instruction on the host CPU with the given
 * parameters, and saves the results in the given locations. Does nothing on
 * non-x86 hosts.
 *
 * |function| is the 'CPUID leaf' (the EAX parameter to CPUID), and |count|
 * the 'CPUID sub-leaf' (the ECX parameter to CPUID), givne as input
 *
 * parameters. |eax|,|ebx|,|ecx| and |edx| are optional pointers to variables
 * that will be set on exit to the value of the corresponding register;
 * if one of this parameter is NULL, it is ignored.
 */
void android_get_x86_cpuid(uint32_t function, uint32_t count,
                           uint32_t *eax, uint32_t *ebx,
                           uint32_t *ecx, uint32_t *edx);

/*
 * android_get_x86_cpuid_vendor_id: retrieve x86 CPUID vendor id as a null
 * terminated string
 *
 * examples: "GenuineIntel" "AuthenticAMD" "VMwareVMware"
 *
 * |vendor_id_len| - must be at least 13 bytes
 */
void android_get_x86_cpuid_vendor_id(char* vendor_id, size_t vendor_id_len);

/*
 * android_get_x86_vendor_id_vmhost: identify known VM vendor ids
 *
 * Returns 1 if |vendor_id| retrieved from cpuid is one of four known VM
 * host vendor id strings.  Returns 0 otherwise.
 */
int android_get_x86_cpuid_vendor_id_is_vmhost(const char* vendor_id);

/*
 * android_get_x86_cpuid_vmx_support: returns 1 if the CPU supports Intel
 * VM-x features, returns 0 otherwise
 */
int android_get_x86_cpuid_vmx_support();

/*
 * android_get_x86_cpuid_svm_support: returns 1 if the CPU supports AMD
 * SVM features, returns 0 otherwise
 */
int android_get_x86_cpuid_svm_support();

/*
 * android_get_x86_cpuid_nx_support: returns 1 if the CPU supports Intel
 * NX (no execute) features, returns 0 otherwise
 */
int android_get_x86_cpuid_nx_support();

/*
 * android_get_x86_cpuid_is_vcpu: returns 1 if the CPU is a running under
 * a Hypervisor
 */
int android_get_x86_cpuid_is_vcpu();

ANDROID_END_HEADER

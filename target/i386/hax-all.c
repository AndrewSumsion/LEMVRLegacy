/*
 * QEMU HAX support
 *
 * Copyright IBM, Corp. 2008
 *           Red Hat, Inc. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *  Glauber Costa     <gcosta@redhat.com>
 *
 * Copyright (c) 2011 Intel Corporation
 *  Written by:
 *  Jiang Yunhong<yunhong.jiang@intel.com>
 *  Xin Xiaohui<xiaohui.xin@intel.com>
 *  Zhang Xiantao<xiantao.zhang@intel.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

/*
 * HAX common code for both windows and darwin
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"
#include "exec/ioport.h"

#include "qemu-common.h"
#include "hax-i386.h"
#include "sysemu/accel.h"
#include "sysemu/sysemu.h"
#include "qemu/main-loop.h"
#include "hw/boards.h"

#define DEBUG_HAX 0

#define DPRINTF(fmt, ...) \
    do { \
        if (DEBUG_HAX) { \
            fprintf(stdout, fmt, ## __VA_ARGS__); \
        } \
    } while (0)

/* Current version */
const uint32_t hax_cur_version = 0x4; /* API v4: unmapping and MMIO moves */
/* Minimum HAX kernel version */
const uint32_t hax_min_version = 0x4; /* API v4: supports unmapping */

static bool hax_allowed;

struct hax_state hax_global;

static void hax_vcpu_sync_state(CPUArchState *env, int modified);
static int hax_arch_get_registers(CPUArchState *env);

int hax_enabled(void)
{
    return hax_allowed;
}

/* HAXM UG Support */
#define TYPE_HAX_ACCEL ACCEL_CLASS_NAME("hax")

#define HAX_EMUL_ONE    0x1
#define HAX_EMUL_REAL   0x2
#define HAX_EMUL_HLT    0x4
#define HAX_EMUL_EXITLOOP    0x5

#define HAX_EMULATE_STATE_MMIO  0x1
#define HAX_EMULATE_STATE_REAL  0x2
#define HAX_EMULATE_STATE_NONE  0x3
#define HAX_EMULATE_STATE_INITIAL       0x4

#define HAX_NON_UG_PLATFORM 0x0
#define HAX_UG_PLATFORM     0x1

static int ug_support = 0;

/* Called after hax_init */
int hax_ug_platform(void)
{
      return ug_support;
}

bool hax_gpa_protection_supported(void)
{
    return hax_global.supports_ram_protection;
}

int hax_vcpu_active(CPUState* cpu)
{
    return ug_support ||
           cpu->hax_vcpu->emulation_state == HAX_EMULATE_STATE_NONE;
}

/* Currently non-PG modes are emulated by QEMU */
int hax_vcpu_emulation_mode(CPUState * cpu)
{
    CPUArchState *env = (CPUArchState *) (cpu->env_ptr);
    return !(env->cr[0] & CR0_PG_MASK);
}

static int hax_prepare_emulation(CPUArchState * env)
{
    /* Flush all emulation states */
    tlb_flush(ENV_GET_CPU(env));
    tb_flush(ENV_GET_CPU(env));
    /* Sync the vcpu state from hax kernel module */
    hax_vcpu_sync_state(env, 0);
    return 0;
}

/*
 * Check whether to break the translation block loop
 * break tbloop after one MMIO emulation, or after finish emulation mode
 */
static int hax_stop_tbloop(CPUArchState * env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    switch (cpu->hax_vcpu->emulation_state) {
    case HAX_EMULATE_STATE_MMIO:
        if (cpu->hax_vcpu->resync) {
            hax_prepare_emulation(env);
            cpu->hax_vcpu->resync = 0;
            return 0;
        }
        return 1;
        break;
    case HAX_EMULATE_STATE_INITIAL:
    case HAX_EMULATE_STATE_REAL:
        if (!hax_vcpu_emulation_mode(cpu))
            return 1;
        break;
    default:
        fprintf(stderr, "Invalid emulation state in hax_sto_tbloop state %x\n",
                cpu->hax_vcpu->emulation_state);
        break;
    }

    return 0;
}

int hax_stop_emulation(CPUState * cpu)
{
    CPUArchState *env = (CPUArchState *) (cpu->env_ptr);

    if (hax_stop_tbloop(env)) {
        cpu->hax_vcpu->emulation_state = HAX_EMULATE_STATE_NONE;
        /*
         * QEMU emulation changes vcpu state,
         * Sync the vcpu state to HAX kernel module
         */
        hax_vcpu_sync_state(env, 1);
        cpu->vcpu_dirty = false;
        return 1;
    }

    return 0;
}

int hax_stop_translate(CPUState * cpu)
{
    struct hax_vcpu_state *vstate = cpu->hax_vcpu;

    assert(vstate->emulation_state);
    if (vstate->emulation_state == HAX_EMULATE_STATE_MMIO)
        return 1;

    return 0;
}

int valid_hax_tunnel_size(uint16_t size)
{
    return size >= sizeof(struct hax_tunnel);
}

hax_fd hax_vcpu_get_fd(CPUArchState *env)
{
    struct hax_vcpu_state *vcpu = ENV_GET_CPU(env)->hax_vcpu;
    if (!vcpu) {
        return HAX_INVALID_FD;
    }
    return vcpu->fd;
}

static int hax_get_capability(struct hax_state *hax)
{
    int ret;
    struct hax_capabilityinfo capinfo, *cap = &capinfo;

    ret = hax_capability(hax, cap);
    if (ret) {
        return ret;
    }

    if ((cap->wstatus & HAX_CAP_WORKSTATUS_MASK) == HAX_CAP_STATUS_NOTWORKING) {
        if (cap->winfo & HAX_CAP_FAILREASON_VT) {
            DPRINTF
                ("VTX feature is not enabled, HAX driver will not work.\n");
        } else if (cap->winfo & HAX_CAP_FAILREASON_NX) {
            DPRINTF
                ("NX feature is not enabled, HAX driver will not work.\n");
        }
        return -ENXIO;

    }

    if ((cap->winfo & HAX_CAP_UG)) {
        ug_support = 1;
    }

    // NOTE: If HAX_DISABLE_UNRESTRICTED_GUEST is defined and set to 1 or 'true'
    // then disable "unrestricted guest" on modern cpus that support it. This
    // is useful to test and debug the code-path used for older CPUs that
    // don't have that feature.
    if (ug_support) {
        const char* env = getenv("HAX_DISABLE_UNRESTRICTED_GUEST");
        if (env && (!strcmp(env, "1") || !strcmp(env, "true"))) {
            DPRINTF(
                "VTX unrestricted guest disabled by environment variable.\n");
            ug_support = 0;
        }
    }

    hax->supports_64bit_ramblock = !!(cap->winfo & HAX_CAP_64BIT_RAMBLOCK);
    hax->supports_64bit_setram = !!(cap->winfo & HAX_CAP_64BIT_SETRAM);
    hax->supports_ram_protection = !!(cap->winfo & HAX_CAP_RAM_PROTECTION);

    if (cap->wstatus & HAX_CAP_MEMQUOTA) {
        if (cap->mem_quota < hax->mem_quota) {
            fprintf(stderr, "The VM memory needed exceeds the driver limit.\n");
            return -ENOSPC;
        }
    }
    return 0;
}

static int hax_version_support(struct hax_state *hax)
{
    int ret;
    struct hax_module_version version;

    ret = hax_mod_version(hax, &version);
    if (ret < 0) {
        return 0;
    }

    if (hax_min_version > version.cur_version) {
        fprintf(stderr, "Incompatible HAX module version %d,",
                version.cur_version);
        fprintf(stderr, "requires minimum version %d\n", hax_min_version);
        return 0;
    }
    if (hax_cur_version < version.compat_version) {
        fprintf(stderr, "Incompatible QEMU HAX API version %x,",
                hax_cur_version);
        fprintf(stderr, "requires minimum HAX API version %x\n",
                version.compat_version);
        return 0;
    }

    return 1;
}

int hax_vcpu_create(int id)
{
    struct hax_vcpu_state *vcpu = NULL;
    int ret;

    if (!hax_global.vm) {
        fprintf(stderr, "vcpu %x created failed, vm is null\n", id);
        return -1;
    }

    if (hax_global.vm->vcpus[id]) {
        fprintf(stderr, "vcpu %x allocated already\n", id);
        return 0;
    }

    vcpu = g_malloc(sizeof(struct hax_vcpu_state));
    if (!vcpu) {
        fprintf(stderr, "Failed to alloc vcpu state\n");
        return -ENOMEM;
    }

    memset(vcpu, 0, sizeof(struct hax_vcpu_state));

    ret = hax_host_create_vcpu(hax_global.vm->fd, id);
    if (ret) {
        fprintf(stderr, "Failed to create vcpu %x\n", id);
        goto error;
    }

    vcpu->vcpu_id = id;
    vcpu->fd = hax_host_open_vcpu(hax_global.vm->id, id);
    if (hax_invalid_fd(vcpu->fd)) {
        fprintf(stderr, "Failed to open the vcpu\n");
        ret = -ENODEV;
        goto error;
    }

    hax_global.vm->vcpus[id] = vcpu;

    ret = hax_host_setup_vcpu_channel(vcpu);
    if (ret) {
        fprintf(stderr, "Invalid hax tunnel size\n");
        ret = -EINVAL;
        goto error;
    }
    return 0;

  error:
    /* vcpu and tunnel will be closed automatically */
    if (vcpu && !hax_invalid_fd(vcpu->fd)) {
        hax_close_fd(vcpu->fd);
    }

    hax_global.vm->vcpus[id] = NULL;
    g_free(vcpu);
    return -1;
}

int hax_vcpu_destroy(CPUState *cpu)
{
    struct hax_vcpu_state *vcpu = cpu->hax_vcpu;

    if (!hax_global.vm) {
        fprintf(stderr, "vcpu %x destroy failed, vm is null\n", vcpu->vcpu_id);
        return -1;
    }

    if (!vcpu) {
        return 0;
    }

    /*
     * 1. The hax_tunnel is also destroied when vcpu destroy
     * 2. close fd will cause hax module vcpu be cleaned
     */
    hax_close_fd(vcpu->fd);
    hax_global.vm->vcpus[vcpu->vcpu_id] = NULL;
    g_free(vcpu);
    return 0;
}

int hax_init_vcpu(CPUState *cpu)
{
    int ret;

    ret = hax_vcpu_create(cpu->cpu_index);
    if (ret < 0) {
        fprintf(stderr, "Failed to create HAX vcpu\n");
        exit(-1);
    }

    cpu->hax_vcpu = hax_global.vm->vcpus[cpu->cpu_index];
    cpu->hax_vcpu->emulation_state = HAX_EMULATE_STATE_INITIAL;
    cpu->vcpu_dirty = true;
    qemu_register_reset(hax_reset_vcpu_state, (CPUArchState *) (cpu->env_ptr));

    return ret;
}

struct hax_vm *hax_vm_create(struct hax_state *hax)
{
    struct hax_vm *vm;
    int vm_id = 0, ret;

    if (hax_invalid_fd(hax->fd)) {
        return NULL;
    }

    if (hax->vm) {
        return hax->vm;
    }

    vm = g_malloc(sizeof(struct hax_vm));
    if (!vm) {
        return NULL;
    }
    memset(vm, 0, sizeof(struct hax_vm));
    ret = hax_host_create_vm(hax, &vm_id);
    if (ret) {
        fprintf(stderr, "Failed to create vm %x\n", ret);
        goto error;
    }
    vm->id = vm_id;
    vm->fd = hax_host_open_vm(hax, vm_id);
    if (hax_invalid_fd(vm->fd)) {
        fprintf(stderr, "Failed to open vm %d\n", vm_id);
        goto error;
    }

    hax->vm = vm;
    return vm;

  error:
    g_free(vm);
    hax->vm = NULL;
    return NULL;
}

int hax_vm_destroy(struct hax_vm *vm)
{
    int i;

    for (i = 0; i < HAX_MAX_VCPU; i++)
        if (vm->vcpus[i]) {
            fprintf(stderr, "VCPU should be cleaned before vm clean\n");
            return -1;
        }
    hax_close_fd(vm->fd);
    g_free(vm);
    hax_global.vm = NULL;
    return 0;
}

static void hax_handle_interrupt(CPUState *cpu, int mask)
{
    cpu->interrupt_request |= mask;

    if (!qemu_cpu_is_self(cpu)) {
        qemu_cpu_kick(cpu);
    }
}

static int hax_init(ram_addr_t ram_size)
{
    struct hax_state *hax = NULL;
    struct hax_qemu_version qversion;
    int ret;
    int i;

    hax = &hax_global;

    memset(hax, 0, sizeof(struct hax_state));
    hax->mem_quota = ram_size;
    for (i = 0; i < HAX_MAX_SLOTS; ++i) {
        hax->memslots[i].id = i;
        hax->memslots[i].flags = HAX_RAM_INFO_INVALID;
    }

    hax->fd = hax_mod_open();
    if (hax_invalid_fd(hax->fd)) {
        hax->fd = 0;
        ret = -ENODEV;
        goto error;
    }

    ret = hax_get_capability(hax);

    if (ret) {
        if (ret != -ENOSPC) {
            ret = -EINVAL;
        }
        goto error;
    }

    if (!hax_version_support(hax)) {
        ret = -EINVAL;
        goto error;
    }

    hax->vm = hax_vm_create(hax);
    if (!hax->vm) {
        fprintf(stderr, "Failed to create HAX VM\n");
        ret = -EINVAL;
        goto error;
    }

    hax_memory_init();

    qversion.cur_version = hax_cur_version;
    qversion.min_version = hax_min_version;
    hax_notify_qemu_version(hax->vm->fd, &qversion);
    cpu_interrupt_handler = hax_handle_interrupt;

    return ret;
  error:
    if (hax->vm) {
        hax_vm_destroy(hax->vm);
    }
    if (hax->fd) {
        hax_mod_close(hax);
    }

    return ret;
}

static int hax_accel_init(MachineState *ms)
{
    int ret = hax_init(ms->ram_size);

    if (ret && (ret != -ENOSPC)) {
        fprintf(stderr, "No accelerator found.\n");
    } else {
        /* need tcg for non-UG platform in real mode */
        if (!hax_ug_platform())
           tcg_exec_init(tcg_tb_size * 1024 * 1024);

        fprintf(stdout, "HAX is %s and emulator runs in %s mode.\n",
                !ret ? "working" : "not working",
                !ret ? "fast virt" : "emulation");
    }
    return ret;
}

uint64_t hax_mem_limit(void)
{
    uint64_t limit = (uint64_t)(-1);
    int ret = 0;
    struct hax_capabilityinfo capinfo, *cap = &capinfo;
    struct hax_state dummy;

    dummy.fd = hax_mod_open();
    /* if there is no hax, return max mem limit */
    if (hax_invalid_fd(dummy.fd))
        return limit;

    ret = hax_capability(&dummy, cap);
    if (!ret) {
        limit = 4093ULL*1024*1024;
        if (cap->winfo & HAX_CAP_64BIT_RAMBLOCK)
            limit = 6ULL*1024*1024*1024;
        if (cap->winfo & HAX_CAP_64BIT_SETRAM)
            limit = (uint64_t)(-1);
    }

    hax_close_fd(dummy.fd);
    return limit;
}

static int hax_handle_fastmmio(CPUArchState *env, struct hax_fastmmio *hft)
{
    if (hft->direction < 2) {
        cpu_physical_memory_rw(hft->gpa, (uint8_t *) &hft->value, hft->size,
                               hft->direction);
    } else {
        /*
         * HAX API v4 supports transferring data between two MMIO addresses,
         * hft->gpa and hft->gpa2 (instructions such as MOVS require this):
         *  hft->direction == 2: gpa ==> gpa2
         */
        uint64_t value;
        cpu_physical_memory_rw(hft->gpa, (uint8_t *) &value, hft->size, 0);
        cpu_physical_memory_rw(hft->gpa2, (uint8_t *) &value, hft->size, 1);
    }

    return 0;
}

static int hax_handle_io(CPUArchState *env, uint32_t df, uint16_t port,
                         int direction, int size, int count, void *buffer)
{
    uint8_t *ptr;
    int i;
    MemTxAttrs attrs = { 0 };

    if (!df) {
        ptr = (uint8_t *) buffer;
    } else {
        ptr = buffer + size * count - size;
    }
    for (i = 0; i < count; i++) {
        address_space_rw(&address_space_io, port, attrs,
                         ptr, size, direction == HAX_EXIT_IO_OUT);
        if (!df) {
            ptr += size;
        } else {
            ptr -= size;
        }
    }

    return 0;
}

static int hax_vcpu_interrupt(CPUArchState *env)
{
    CPUState *cpu = ENV_GET_CPU(env);
    struct hax_vcpu_state *vcpu = cpu->hax_vcpu;
    struct hax_tunnel *ht = vcpu->tunnel;

    /*
     * Try to inject an interrupt if the guest can accept it
     * Unlike KVM, HAX kernel check for the eflags, instead of qemu
     */
    if (ht->ready_for_interrupt_injection &&
        (cpu->interrupt_request & CPU_INTERRUPT_HARD)) {
        int irq;

        irq = cpu_get_pic_interrupt(env);
        if (irq >= 0) {
            hax_inject_interrupt(env, irq);
            cpu->interrupt_request &= ~CPU_INTERRUPT_HARD;
        }
    }

    /* If we have an interrupt but the guest is not ready to receive an
     * interrupt, request an interrupt window exit.  This will
     * cause a return to userspace as soon as the guest is ready to
     * receive interrupts. */
    if ((cpu->interrupt_request & CPU_INTERRUPT_HARD)) {
        ht->request_interrupt_window = 1;
    } else {
        ht->request_interrupt_window = 0;
    }
    return 0;
}

void hax_raise_event(CPUState *cpu)
{
    struct hax_vcpu_state *vcpu = cpu->hax_vcpu;

    if (!vcpu) {
        return;
    }
    vcpu->tunnel->user_event_pending = 1;
}

/*
 * Ask hax kernel module to run the CPU for us till:
 * 1. Guest crash or shutdown
 * 2. Need QEMU's emulation like guest execute MMIO instruction
 * 3. Guest execute HLT
 * 4. QEMU have Signal/event pending
 * 5. An unknown VMX exit happens
 */
static int hax_vcpu_hax_exec(CPUArchState *env, int ug_platform)
{
    int ret = 0;
    CPUState *cpu = ENV_GET_CPU(env);
    X86CPU *x86_cpu = X86_CPU(cpu);
    struct hax_vcpu_state *vcpu = cpu->hax_vcpu;
    struct hax_tunnel *ht = vcpu->tunnel;

    if (!ug_platform) {
        if (hax_vcpu_emulation_mode(cpu)) {
            DPRINTF("Trying to execute vcpu at eip:" TARGET_FMT_lx "\n",
                    env->eip);
            fprintf(stderr, "Trying to execute vcpu at eip:" TARGET_FMT_lx "\n",
                    env->eip);
            return HAX_EMUL_EXITLOOP;
        }

        cpu->halted = 0;

        if (cpu->interrupt_request & CPU_INTERRUPT_POLL) {
            cpu->interrupt_request &= ~CPU_INTERRUPT_POLL;
            apic_poll_irq(x86_cpu->apic_state);
        }
        goto vcpu_run;
    }

    if (!hax_enabled()) {
        DPRINTF("Trying to vcpu execute at eip:" TARGET_FMT_lx "\n", env->eip);
        return 0;
    }

    cpu->halted = 0;

    if (cpu->interrupt_request & CPU_INTERRUPT_POLL) {
        cpu->interrupt_request &= ~CPU_INTERRUPT_POLL;
        apic_poll_irq(x86_cpu->apic_state);
    }

    if (cpu->interrupt_request & CPU_INTERRUPT_INIT) {
        DPRINTF("\nhax_vcpu_hax_exec: handling INIT for %d\n",
                cpu->cpu_index);
        do_cpu_init(x86_cpu);
        hax_vcpu_sync_state(env, 1);
    }

    if (cpu->interrupt_request & CPU_INTERRUPT_SIPI) {
        DPRINTF("hax_vcpu_hax_exec: handling SIPI for %d\n",
                cpu->cpu_index);
        hax_vcpu_sync_state(env, 0);
        do_cpu_sipi(x86_cpu);
        hax_vcpu_sync_state(env, 1);
    }

vcpu_run:
    do {
        if (cpu->vcpu_dirty) {
            hax_vcpu_sync_state(env, 1);
            cpu->vcpu_dirty = false;
        }

        int hax_ret;

        if (cpu->exit_request) {
            ret = HAX_EMUL_EXITLOOP;
            break;
        }

        hax_vcpu_interrupt(env);

        if (!ug_platform) {
            hax_ret = hax_vcpu_run(vcpu);
        } else {
            qemu_mutex_unlock_iothread();
            cpu_exec_start(cpu);
            hax_ret = hax_vcpu_run(vcpu);
            cpu_exec_end(cpu);
            qemu_mutex_lock_iothread();
            current_cpu = cpu;
        }

        /* Simply continue the vcpu_run if system call interrupted */
        if (hax_ret == -EINTR || hax_ret == -EAGAIN) {
            DPRINTF("io window interrupted\n");
            continue;
        }

        if (hax_ret < 0) {
            fprintf(stderr, "vcpu run failed for vcpu  %x\n", vcpu->vcpu_id);
            abort();
        }
        switch (ht->_exit_status) {
        case HAX_EXIT_IO:
            ret = hax_handle_io(env, ht->pio._df, ht->pio._port,
                            ht->pio._direction,
                            ht->pio._size, ht->pio._count, vcpu->iobuf);
            break;
        case HAX_EXIT_FAST_MMIO:
            ret = hax_handle_fastmmio(env, (struct hax_fastmmio *) vcpu->iobuf);
            break;
        /* Guest state changed, currently only for shutdown */
        case HAX_EXIT_STATECHANGE:
            fprintf(stdout, "VCPU shutdown request\n");
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            hax_vcpu_sync_state(env, 0);
            ret = HAX_EMUL_EXITLOOP;
            break;
        case HAX_EXIT_UNKNOWN_VMEXIT:
            fprintf(stderr, "Unknown VMX exit %x from guest\n",
                    ht->_exit_reason);
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            hax_vcpu_sync_state(env, 0);
            cpu_dump_state(cpu, stderr, fprintf, 0);
            ret = -1;
            break;
        case HAX_EXIT_HLT:
            if (!(cpu->interrupt_request & CPU_INTERRUPT_HARD) &&
                !(cpu->interrupt_request & CPU_INTERRUPT_NMI)) {
                /* hlt instruction with interrupt disabled is shutdown */
                env->eflags |= IF_MASK;
                cpu->halted = 1;
                cpu->exception_index = EXCP_HLT;
                ret = HAX_EMUL_HLT;
            }
            break;
        /* these situations will continue to hax module */
        case HAX_EXIT_INTERRUPT:
        case HAX_EXIT_PAUSED:
            break;
        case HAX_EXIT_MMIO:
            ret = HAX_EMUL_ONE;
            break;
        case HAX_EXIT_REAL:
            ret = HAX_EMUL_REAL;
            break;
        case HAX_EXIT_PAGEFAULT: {
            // HAXM fundamentally needs to unprotect guest RAM in 2MB chunks.
#define HAXM_CHUNK_SIZE 0x200000
            uint64_t gpa_chunk = ht->pagefault.gpa & ~(uint64_t)(HAXM_CHUNK_SIZE - 1);
            uint64_t gpa;
            for (gpa = gpa_chunk; gpa < gpa_chunk + HAXM_CHUNK_SIZE; gpa += 0x1000) {
              bool found = false;
              void* hva = hax_gpa2hva(gpa, &found);
              if (found)
                  qemu_ram_load(hva, 0x1000);
            }
            break;
        }
        default:
            fprintf(stderr, "Unknown exit %x from HAX\n", ht->_exit_status);
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            hax_vcpu_sync_state(env, 0);
            cpu_dump_state(cpu, stderr, fprintf, 0);
            ret = HAX_EMUL_EXITLOOP;
            break;
        }
    } while (!ret);

    if (cpu->exit_request) {
        cpu->exit_request = 0;
        cpu->exception_index = EXCP_INTERRUPT;
    }
    return ret;
}

static void do_hax_cpu_synchronize_state(CPUState *cpu, run_on_cpu_data arg)
{
    CPUArchState *env = cpu->env_ptr;

    hax_arch_get_registers(env);
    cpu->vcpu_dirty = true;
}

void hax_cpu_synchronize_state(CPUState *cpu)
{
    if (!cpu->vcpu_dirty) {
        run_on_cpu(cpu, do_hax_cpu_synchronize_state, RUN_ON_CPU_NULL);
    }
}

static void do_hax_cpu_synchronize_post_reset(CPUState *cpu,
                                              run_on_cpu_data arg)
{
    CPUArchState *env = cpu->env_ptr;

    hax_vcpu_sync_state(env, 1);
    cpu->vcpu_dirty = false;
}

void hax_cpu_synchronize_post_reset(CPUState *cpu)
{
    run_on_cpu(cpu, do_hax_cpu_synchronize_post_reset, RUN_ON_CPU_NULL);
}

static void do_hax_cpu_synchronize_post_init(CPUState *cpu, run_on_cpu_data arg)
{
    CPUArchState *env = cpu->env_ptr;

    hax_vcpu_sync_state(env, 1);
    cpu->vcpu_dirty = false;
}

void hax_cpu_synchronize_post_init(CPUState *cpu)
{
    run_on_cpu(cpu, do_hax_cpu_synchronize_post_init, RUN_ON_CPU_NULL);
}

/*
 * return 1 when need emulate, 0 when need exit loop
 */
int hax_vcpu_exec(CPUState * cpu)
{
    int next = 0, ret = 0;
    struct hax_vcpu_state *vcpu;
    CPUArchState *env = (CPUArchState *) (cpu->env_ptr);

    if (cpu->hax_vcpu->emulation_state != HAX_EMULATE_STATE_NONE)
        return 1;

    vcpu = cpu->hax_vcpu;
    next = hax_vcpu_hax_exec(env, HAX_NON_UG_PLATFORM);
    switch (next) {
    case HAX_EMUL_ONE:
        ret = 1;
        vcpu->emulation_state = HAX_EMULATE_STATE_MMIO;
        hax_prepare_emulation(env);
        break;
    case HAX_EMUL_REAL:
        ret = 1;
        vcpu->emulation_state = HAX_EMULATE_STATE_REAL;
        hax_prepare_emulation(env);
        break;
    case HAX_EMUL_HLT:
    case HAX_EMUL_EXITLOOP:
        break;
    default:
        fprintf(stderr, "Unknown hax vcpu exec return %x\n", next);
        abort();
    }

    return ret;
}

static void do_hax_cpu_synchronize_pre_loadvm(CPUState *cpu, run_on_cpu_data arg)
{
    cpu->vcpu_dirty = true;
}

void hax_cpu_synchronize_pre_loadvm(CPUState *cpu)
{
    run_on_cpu(cpu, do_hax_cpu_synchronize_pre_loadvm, RUN_ON_CPU_NULL);
}

int hax_smp_cpu_exec(CPUState *cpu)
{
    CPUArchState *env = (CPUArchState *) (cpu->env_ptr);
    int why;
    int ret;

    while (1) {
        if (cpu->exception_index >= EXCP_INTERRUPT) {
            ret = cpu->exception_index;
            cpu->exception_index = -1;
            break;
        }

        why = hax_vcpu_hax_exec(env, HAX_UG_PLATFORM);

        if ((why != HAX_EMUL_HLT) && (why != HAX_EMUL_EXITLOOP)) {
            fprintf(stderr, "Unknown hax vcpu return %x\n", why);
            abort();
        }
    }

    return ret;
}

static void set_v8086_seg(struct segment_desc_t *lhs, const SegmentCache *rhs)
{
    memset(lhs, 0, sizeof(struct segment_desc_t));
    lhs->selector = rhs->selector;
    lhs->base = rhs->base;
    lhs->limit = rhs->limit;
    lhs->type = 3;
    lhs->present = 1;
    lhs->dpl = 3;
    lhs->operand_size = 0;
    lhs->desc = 1;
    lhs->long_mode = 0;
    lhs->granularity = 0;
    lhs->available = 0;
}

static void get_seg(SegmentCache *lhs, const struct segment_desc_t *rhs)
{
    lhs->selector = rhs->selector;
    lhs->base = rhs->base;
    lhs->limit = rhs->limit;
    lhs->flags = (rhs->type << DESC_TYPE_SHIFT)
        | (rhs->present * DESC_P_MASK)
        | (rhs->dpl << DESC_DPL_SHIFT)
        | (rhs->operand_size << DESC_B_SHIFT)
        | (rhs->desc * DESC_S_MASK)
        | (rhs->long_mode << DESC_L_SHIFT)
        | (rhs->granularity * DESC_G_MASK) | (rhs->available * DESC_AVL_MASK);
}

static void set_seg(struct segment_desc_t *lhs, const SegmentCache *rhs)
{
    unsigned flags = rhs->flags;

    memset(lhs, 0, sizeof(struct segment_desc_t));
    lhs->selector = rhs->selector;
    lhs->base = rhs->base;
    lhs->limit = rhs->limit;
    lhs->type = (flags >> DESC_TYPE_SHIFT) & 15;
    lhs->present = (flags & DESC_P_MASK) != 0;
    lhs->dpl = rhs->selector & 3;
    lhs->operand_size = (flags >> DESC_B_SHIFT) & 1;
    lhs->desc = (flags & DESC_S_MASK) != 0;
    lhs->long_mode = (flags >> DESC_L_SHIFT) & 1;
    lhs->granularity = (flags & DESC_G_MASK) != 0;
    lhs->available = (flags & DESC_AVL_MASK) != 0;
}

static void hax_getput_reg(uint64_t *hax_reg, target_ulong *qemu_reg, int set)
{
    target_ulong reg = *hax_reg;

    if (set) {
        *hax_reg = *qemu_reg;
    } else {
        *qemu_reg = reg;
    }
}

/* The sregs has been synced with HAX kernel already before this call */
static int hax_get_segments(CPUArchState *env, struct vcpu_state_t *sregs)
{
    get_seg(&env->segs[R_CS], &sregs->_cs);
    get_seg(&env->segs[R_DS], &sregs->_ds);
    get_seg(&env->segs[R_ES], &sregs->_es);
    get_seg(&env->segs[R_FS], &sregs->_fs);
    get_seg(&env->segs[R_GS], &sregs->_gs);
    get_seg(&env->segs[R_SS], &sregs->_ss);

    get_seg(&env->tr, &sregs->_tr);
    get_seg(&env->ldt, &sregs->_ldt);
    env->idt.limit = sregs->_idt.limit;
    env->idt.base = sregs->_idt.base;
    env->gdt.limit = sregs->_gdt.limit;
    env->gdt.base = sregs->_gdt.base;
    return 0;
}

static int hax_set_segments(CPUArchState *env, struct vcpu_state_t *sregs)
{
    if ((env->eflags & VM_MASK)) {
        set_v8086_seg(&sregs->_cs, &env->segs[R_CS]);
        set_v8086_seg(&sregs->_ds, &env->segs[R_DS]);
        set_v8086_seg(&sregs->_es, &env->segs[R_ES]);
        set_v8086_seg(&sregs->_fs, &env->segs[R_FS]);
        set_v8086_seg(&sregs->_gs, &env->segs[R_GS]);
        set_v8086_seg(&sregs->_ss, &env->segs[R_SS]);
    } else {
        set_seg(&sregs->_cs, &env->segs[R_CS]);
        set_seg(&sregs->_ds, &env->segs[R_DS]);
        set_seg(&sregs->_es, &env->segs[R_ES]);
        set_seg(&sregs->_fs, &env->segs[R_FS]);
        set_seg(&sregs->_gs, &env->segs[R_GS]);
        set_seg(&sregs->_ss, &env->segs[R_SS]);

        if (env->cr[0] & CR0_PE_MASK) {
            /* force ss cpl to cs cpl */
            sregs->_ss.selector = (sregs->_ss.selector & ~3) |
                                  (sregs->_cs.selector & 3);
            sregs->_ss.dpl = sregs->_ss.selector & 3;
        }
    }

    set_seg(&sregs->_tr, &env->tr);
    set_seg(&sregs->_ldt, &env->ldt);
    sregs->_idt.limit = env->idt.limit;
    sregs->_idt.base = env->idt.base;
    sregs->_gdt.limit = env->gdt.limit;
    sregs->_gdt.base = env->gdt.base;
    return 0;
}

static int hax_sync_vcpu_register(CPUArchState *env, int set)
{
    struct vcpu_state_t regs;
    int ret;
    memset(&regs, 0, sizeof(struct vcpu_state_t));

    if (!set) {
        ret = hax_sync_vcpu_state(env, &regs, 0);
        if (ret < 0) {
            return -1;
        }
    }

    /* generic register */
    hax_getput_reg(&regs._rax, &env->regs[R_EAX], set);
    hax_getput_reg(&regs._rbx, &env->regs[R_EBX], set);
    hax_getput_reg(&regs._rcx, &env->regs[R_ECX], set);
    hax_getput_reg(&regs._rdx, &env->regs[R_EDX], set);
    hax_getput_reg(&regs._rsi, &env->regs[R_ESI], set);
    hax_getput_reg(&regs._rdi, &env->regs[R_EDI], set);
    hax_getput_reg(&regs._rsp, &env->regs[R_ESP], set);
    hax_getput_reg(&regs._rbp, &env->regs[R_EBP], set);
#ifdef TARGET_X86_64
    hax_getput_reg(&regs._r8, &env->regs[8], set);
    hax_getput_reg(&regs._r9, &env->regs[9], set);
    hax_getput_reg(&regs._r10, &env->regs[10], set);
    hax_getput_reg(&regs._r11, &env->regs[11], set);
    hax_getput_reg(&regs._r12, &env->regs[12], set);
    hax_getput_reg(&regs._r13, &env->regs[13], set);
    hax_getput_reg(&regs._r14, &env->regs[14], set);
    hax_getput_reg(&regs._r15, &env->regs[15], set);
#endif
    hax_getput_reg(&regs._rflags, &env->eflags, set);
    hax_getput_reg(&regs._rip, &env->eip, set);

    if (set) {
        regs._cr0 = env->cr[0];
        regs._cr2 = env->cr[2];
        regs._cr3 = env->cr[3];
        regs._cr4 = env->cr[4];
        hax_set_segments(env, &regs);
    } else {
        env->cr[0] = regs._cr0;
        env->cr[2] = regs._cr2;
        env->cr[3] = regs._cr3;
        env->cr[4] = regs._cr4;
        hax_get_segments(env, &regs);
    }

    if (set) {
        ret = hax_sync_vcpu_state(env, &regs, 1);
        if (ret < 0) {
            return -1;
        }
    }
    return 0;
}

static void hax_msr_entry_set(struct vmx_msr *item, uint32_t index,
                              uint64_t value)
{
    item->entry = index;
    item->value = value;
}

static int hax_get_msrs(CPUArchState *env)
{
    struct hax_msr_data md;
    struct vmx_msr *msrs = md.entries;
    int ret, i, n;

    n = 0;
    msrs[n++].entry = MSR_IA32_SYSENTER_CS;
    msrs[n++].entry = MSR_IA32_SYSENTER_ESP;
    msrs[n++].entry = MSR_IA32_SYSENTER_EIP;
    msrs[n++].entry = MSR_IA32_TSC;
#ifdef TARGET_X86_64
    msrs[n++].entry = MSR_EFER;
    msrs[n++].entry = MSR_STAR;
    msrs[n++].entry = MSR_LSTAR;
    msrs[n++].entry = MSR_CSTAR;
    msrs[n++].entry = MSR_FMASK;
    msrs[n++].entry = MSR_KERNELGSBASE;
#endif
    md.nr_msr = n;
    ret = hax_sync_msr(env, &md, 0);
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < md.done; i++) {
        switch (msrs[i].entry) {
        case MSR_IA32_SYSENTER_CS:
            env->sysenter_cs = msrs[i].value;
            break;
        case MSR_IA32_SYSENTER_ESP:
            env->sysenter_esp = msrs[i].value;
            break;
        case MSR_IA32_SYSENTER_EIP:
            env->sysenter_eip = msrs[i].value;
            break;
        case MSR_IA32_TSC:
            env->tsc = msrs[i].value;
            break;
#ifdef TARGET_X86_64
        case MSR_EFER:
            env->efer = msrs[i].value;
            break;
        case MSR_STAR:
            env->star = msrs[i].value;
            break;
        case MSR_LSTAR:
            env->lstar = msrs[i].value;
            break;
        case MSR_CSTAR:
            env->cstar = msrs[i].value;
            break;
        case MSR_FMASK:
            env->fmask = msrs[i].value;
            break;
        case MSR_KERNELGSBASE:
            env->kernelgsbase = msrs[i].value;
            break;
#endif
        }
    }

    return 0;
}

static int hax_set_msrs(CPUArchState *env)
{
    struct hax_msr_data md;
    struct vmx_msr *msrs;
    msrs = md.entries;
    int n = 0;

    memset(&md, 0, sizeof(struct hax_msr_data));
    hax_msr_entry_set(&msrs[n++], MSR_IA32_SYSENTER_CS, env->sysenter_cs);
    hax_msr_entry_set(&msrs[n++], MSR_IA32_SYSENTER_ESP, env->sysenter_esp);
    hax_msr_entry_set(&msrs[n++], MSR_IA32_SYSENTER_EIP, env->sysenter_eip);
    hax_msr_entry_set(&msrs[n++], MSR_IA32_TSC, env->tsc);
#ifdef TARGET_X86_64
    hax_msr_entry_set(&msrs[n++], MSR_EFER, env->efer);
    hax_msr_entry_set(&msrs[n++], MSR_STAR, env->star);
    hax_msr_entry_set(&msrs[n++], MSR_LSTAR, env->lstar);
    hax_msr_entry_set(&msrs[n++], MSR_CSTAR, env->cstar);
    hax_msr_entry_set(&msrs[n++], MSR_FMASK, env->fmask);
    hax_msr_entry_set(&msrs[n++], MSR_KERNELGSBASE, env->kernelgsbase);
#endif
    md.nr_msr = n;
    md.done = 0;

    return hax_sync_msr(env, &md, 1);
}

static int hax_get_fpu(CPUArchState *env)
{
    struct fx_layout fpu;
    int i, ret;

    ret = hax_sync_fpu(env, &fpu, 0);
    if (ret < 0) {
        return ret;
    }

    env->fpstt = (fpu.fsw >> 11) & 7;
    env->fpus = fpu.fsw;
    env->fpuc = fpu.fcw;
    for (i = 0; i < 8; ++i) {
        env->fptags[i] = !((fpu.ftw >> i) & 1);
    }
    memcpy(env->fpregs, fpu.st_mm, sizeof(env->fpregs));

    for (i = 0; i < 8; i++) {
        env->xmm_regs[i].ZMM_Q(0) = ldq_p(&fpu.mmx_1[i][0]);
        env->xmm_regs[i].ZMM_Q(1) = ldq_p(&fpu.mmx_1[i][8]);
        if (CPU_NB_REGS > 8) {
            env->xmm_regs[i + 8].ZMM_Q(0) = ldq_p(&fpu.mmx_2[i][0]);
            env->xmm_regs[i + 8].ZMM_Q(1) = ldq_p(&fpu.mmx_2[i][8]);
        }
    }
    env->mxcsr = fpu.mxcsr;

    return 0;
}

static int hax_set_fpu(CPUArchState *env)
{
    struct fx_layout fpu;
    int i;

    memset(&fpu, 0, sizeof(fpu));
    fpu.fsw = env->fpus & ~(7 << 11);
    fpu.fsw |= (env->fpstt & 7) << 11;
    fpu.fcw = env->fpuc;

    for (i = 0; i < 8; ++i) {
        fpu.ftw |= (!env->fptags[i]) << i;
    }

    memcpy(fpu.st_mm, env->fpregs, sizeof(env->fpregs));
    for (i = 0; i < 8; i++) {
        stq_p(&fpu.mmx_1[i][0], env->xmm_regs[i].ZMM_Q(0));
        stq_p(&fpu.mmx_1[i][8], env->xmm_regs[i].ZMM_Q(1));
        if (CPU_NB_REGS > 8) {
            stq_p(&fpu.mmx_2[i][0], env->xmm_regs[i + 8].ZMM_Q(0));
            stq_p(&fpu.mmx_2[i][8], env->xmm_regs[i + 8].ZMM_Q(1));
        }
    }

    fpu.mxcsr = env->mxcsr;

    return hax_sync_fpu(env, &fpu, 1);
}

static int hax_arch_get_registers(CPUArchState *env)
{
    int ret;

    ret = hax_sync_vcpu_register(env, 0);
    if (ret < 0) {
        return ret;
    }

    ret = hax_get_fpu(env);
    if (ret < 0) {
        return ret;
    }

    ret = hax_get_msrs(env);
    if (ret < 0) {
        return ret;
    }

    x86_update_hflags(env);
    return 0;
}

static int hax_arch_set_registers(CPUArchState *env)
{
    int ret;
    ret = hax_sync_vcpu_register(env, 1);

    if (ret < 0) {
        fprintf(stderr, "Failed to sync vcpu reg\n");
        return ret;
    }
    ret = hax_set_fpu(env);
    if (ret < 0) {
        fprintf(stderr, "FPU failed\n");
        return ret;
    }
    ret = hax_set_msrs(env);
    if (ret < 0) {
        fprintf(stderr, "MSR failed\n");
        return ret;
    }

    return 0;
}

static void hax_vcpu_sync_state(CPUArchState *env, int modified)
{
    if (hax_enabled()) {
        if (modified) {
            hax_arch_set_registers(env);
        } else {
            hax_arch_get_registers(env);
        }
    }
}

/*
 * much simpler than kvm, at least in first stage because:
 * We don't need consider the device pass-through, we don't need
 * consider the framebuffer, and we may even remove the bios at all
 */
int hax_sync_vcpus(void)
{
    if (hax_enabled()) {
        CPUState *cpu;

        cpu = first_cpu;
        if (!cpu) {
            return 0;
        }

        for (; cpu != NULL; cpu = CPU_NEXT(cpu)) {
            int ret;

            ret = hax_arch_set_registers(cpu->env_ptr);
            if (ret < 0) {
                return ret;
            }
        }
    }

    return 0;
}

void hax_reset_vcpu_state(void *opaque)
{
    CPUState *cpu;
    for (cpu = first_cpu; cpu != NULL; cpu = CPU_NEXT(cpu)) {
        DPRINTF("*********ReSet hax_vcpu->emulation_state \n");
        cpu->hax_vcpu->emulation_state = HAX_EMULATE_STATE_INITIAL;
        cpu->hax_vcpu->tunnel->user_event_pending = 0;
        cpu->hax_vcpu->tunnel->ready_for_interrupt_injection = 0;
    }
}

static void hax_accel_class_init(ObjectClass *oc, void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "HAX";
    ac->init_machine = hax_accel_init;
    ac->allowed = &hax_allowed;
}

static const TypeInfo hax_accel_type = {
    .name = ACCEL_CLASS_NAME("hax"),
    .parent = TYPE_ACCEL,
    .class_init = hax_accel_class_init,
};

static void hax_type_init(void)
{
    type_register_static(&hax_accel_type);
}

type_init(hax_type_init);

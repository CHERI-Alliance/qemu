/*
 * RISC-V Control and Status Registers.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/log_instr.h"
#ifdef TARGET_CHERI
#include "cheri-helper-utils.h"
#endif
/* CSR update logging API */
#if CONFIG_TCG_LOG_INSTR
#ifdef TARGET_CHERI
static inline cap_register_t *get_cap_csr(CPUArchState *env, uint32_t index);
#endif
void riscv_log_instr_csr_changed(CPURISCVState *env, int csrno)
{
    target_ulong value;
    if (qemu_log_instr_enabled(env)) {

#ifdef TARGET_CHERI
        if(is_cap_csr(csrno)){
            riscv_csr_cap_ops *csr_cap_info = get_csr_cap_info(csrno);
            // log the value and write it
            cap_register_t log_reg = *get_cap_csr(env, csr_cap_info->reg_num);
            cheri_log_instr_changed_capreg(env, csr_cap_info->name, &log_reg,
                                   csr_cap_info->reg_num, LRI_CSR_ACCESS);
        }
        else
#endif
        {

            if (csr_ops[csrno].read)
                csr_ops[csrno].read(env, csrno, &value);
            else if (csr_ops[csrno].op)
                csr_ops[csrno].op(env, csrno, &value, 0, /*write_mask*/0);
            else
                return;
            if (csr_ops[csrno].log_update)
                csr_ops[csrno].log_update(env, csrno, value);
        }
    }
}
#endif

#ifdef TARGET_CHERI
bool is_cap_csr(int csrno)
{
    switch (csrno)
    {
        case CSR_DPCC:
        case CSR_DSCRATCH0C:
        case CSR_DSCRATCH1C:
        case CSR_MTVECC:
        case CSR_MSCRATCHC:
        case CSR_MEPCC:
        case CSR_STVECC:
        case CSR_SSCRATCHC:
        case CSR_SEPCC:
        case CSR_DDDC:
        case CSR_MTDC:
        case CSR_STDC:
        case CSR_DDC:
        case CSR_DINFC:
        case CSR_MTIDC:
        case CSR_STIDC:
        case CSR_UTIDC:
            return true;
        default:
            return false;
    }
}
#endif

/* CSR function table public API */
void riscv_get_csr_ops(int csrno, riscv_csr_operations *ops)
{
    *ops = csr_ops[csrno & (CSR_TABLE_SIZE - 1)];
}

void riscv_set_csr_ops(int csrno, riscv_csr_operations *ops)
{
    csr_ops[csrno & (CSR_TABLE_SIZE - 1)] = *ops;
}

/* Predicates */
static RISCVException fs(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    /* loose check condition for fcsr in vector extension */
    if ((csrno == CSR_FCSR) && (env->misa & RVV)) {
        return RISCV_EXCP_NONE;
    }
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException vs(CPURISCVState *env, int csrno)
{
    if (env->misa & RVV) {
        return RISCV_EXCP_NONE;
    }
    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException ctr(CPURISCVState *env, int csrno)
{
#if !defined(CONFIG_USER_ONLY)
    CPUState *cs = env_cpu(env);
    RISCVCPU *cpu = RISCV_CPU(cs);

    if (!cpu->cfg.ext_counters) {
        /* The Counters extensions is not enabled */
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_virt_enabled(env)) {
        switch (csrno) {
        case CSR_CYCLE:
            if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                get_field(env->mcounteren, HCOUNTEREN_CY)) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_TIME:
            if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                get_field(env->mcounteren, HCOUNTEREN_TM)) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_INSTRET:
            if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                get_field(env->mcounteren, HCOUNTEREN_IR)) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        case CSR_HPMCOUNTER3...CSR_HPMCOUNTER31:
            if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3)) &&
                get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3))) {
                return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
            }
            break;
        }
        if (riscv_cpu_is_32bit(env)) {
            switch (csrno) {
            case CSR_CYCLEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_CY) &&
                    get_field(env->mcounteren, HCOUNTEREN_CY)) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_TIMEH:
                if (!get_field(env->hcounteren, HCOUNTEREN_TM) &&
                    get_field(env->mcounteren, HCOUNTEREN_TM)) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_INSTRETH:
                if (!get_field(env->hcounteren, HCOUNTEREN_IR) &&
                    get_field(env->mcounteren, HCOUNTEREN_IR)) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            case CSR_HPMCOUNTER3H...CSR_HPMCOUNTER31H:
                if (!get_field(env->hcounteren, 1 << (csrno - CSR_HPMCOUNTER3H)) &&
                    get_field(env->mcounteren, 1 << (csrno - CSR_HPMCOUNTER3H))) {
                    return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
                }
                break;
            }
        }
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException ctr32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return ctr(env, csrno);
}

#if !defined(CONFIG_USER_ONLY)
static RISCVException any(CPURISCVState *env, int csrno)
{
    return RISCV_EXCP_NONE;
}

static RISCVException any32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    return any(env, csrno);

}

static RISCVException smode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException hmode(CPURISCVState *env, int csrno)
{
    if (riscv_has_ext(env, RVS) &&
        riscv_has_ext(env, RVH)) {
        /* Hypervisor extension is supported */
        if ((env->priv == PRV_S && !riscv_cpu_virt_enabled(env)) ||
            env->priv == PRV_M) {
            return RISCV_EXCP_NONE;
        } else {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException hmode32(CPURISCVState *env, int csrno)
{
    if (!riscv_cpu_is_32bit(env)) {
        if (riscv_cpu_virt_enabled(env)) {
            return RISCV_EXCP_ILLEGAL_INST;
        } else {
            return RISCV_EXCP_VIRT_INSTRUCTION_FAULT;
        }
    }

    return hmode(env, csrno);

}

static RISCVException pmp(CPURISCVState *env, int csrno)
{
    if (riscv_feature(env, RISCV_FEATURE_PMP)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static __attribute__((unused)) RISCVException epmp(CPURISCVState *env, int csrno)
{
    if (env->priv == PRV_M && riscv_feature(env, RISCV_FEATURE_EPMP)) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}
#endif

/* User Floating-Point CSRs */
static RISCVException read_fflags(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = riscv_cpu_get_fflags(env);
    return RISCV_EXCP_NONE;
}

static RISCVException write_fflags(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    riscv_cpu_set_fflags(env, val & (FSR_AEXC >> FSR_AEXC_SHIFT));
    return RISCV_EXCP_NONE;
}

static RISCVException read_frm(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = env->frm;
    return RISCV_EXCP_NONE;
}

static RISCVException write_frm(CPURISCVState *env, int csrno,
                                target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = val & (FSR_RD >> FSR_RD_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_fcsr(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif
    *val = (riscv_cpu_get_fflags(env) << FSR_AEXC_SHIFT)
        | (env->frm << FSR_RD_SHIFT);
    if (vs(env, csrno) >= 0) {
        *val |= (env->vxrm << FSR_VXRM_SHIFT)
                | (env->vxsat << FSR_VXSAT_SHIFT);
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_fcsr(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
#if !defined(CONFIG_USER_ONLY)
    if (!env->debugger && !riscv_cpu_fp_enabled(env)) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    env->mstatus |= MSTATUS_FS;
#endif
    env->frm = (val & FSR_RD) >> FSR_RD_SHIFT;
    if (vs(env, csrno) >= 0) {
        env->vxrm = (val & FSR_VXRM) >> FSR_VXRM_SHIFT;
        env->vxsat = (val & FSR_VXSAT) >> FSR_VXSAT_SHIFT;
    }
    riscv_cpu_set_fflags(env, (val & FSR_AEXC) >> FSR_AEXC_SHIFT);
    return RISCV_EXCP_NONE;
}

static RISCVException read_vtype(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vtype;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vl(CPURISCVState *env, int csrno,
                              target_ulong *val)
{
    *val = env->vl;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vxrm(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = env->vxrm;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vxrm(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    env->vxrm = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vxsat(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vxsat;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vxsat(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->vxsat = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vstart(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->vstart;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vstart(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->vstart = val;
    return RISCV_EXCP_NONE;
}

/* User Timers and Counters */
static RISCVException read_instret(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get();
    } else {
        *val = cpu_get_host_ticks();
    }
#else
    *val = cpu_get_host_ticks();
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException read_instreth(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
#if !defined(CONFIG_USER_ONLY)
    if (icount_enabled()) {
        *val = icount_get() >> 32;
    } else {
        *val = cpu_get_host_ticks() >> 32;
    }
#else
    *val = cpu_get_host_ticks() >> 32;
#endif
    return RISCV_EXCP_NONE;
}

#if defined(CONFIG_USER_ONLY)
static RISCVException read_time(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = cpu_get_host_ticks();
    return RISCV_EXCP_NONE;
}

static RISCVException read_timeh(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = cpu_get_host_ticks() >> 32;
    return RISCV_EXCP_NONE;
}

#else /* CONFIG_USER_ONLY */

static RISCVException read_time(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->rdtime_fn(env->rdtime_fn_arg) + delta;
    return RISCV_EXCP_NONE;
}

static RISCVException read_timeh(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    uint64_t delta = riscv_cpu_virt_enabled(env) ? env->htimedelta : 0;

    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = (env->rdtime_fn(env->rdtime_fn_arg) + delta) >> 32;
    return RISCV_EXCP_NONE;
}

/* Machine constants */

#define M_MODE_INTERRUPTS  (MIP_MSIP | MIP_MTIP | MIP_MEIP)
#define S_MODE_INTERRUPTS  (MIP_SSIP | MIP_STIP | MIP_SEIP)
#define VS_MODE_INTERRUPTS (MIP_VSSIP | MIP_VSTIP | MIP_VSEIP)

static const target_ulong delegable_ints = S_MODE_INTERRUPTS |
                                           VS_MODE_INTERRUPTS;
static const target_ulong all_ints = M_MODE_INTERRUPTS | S_MODE_INTERRUPTS |
                                     VS_MODE_INTERRUPTS;
static const target_ulong delegable_excps =
    (1ULL << (RISCV_EXCP_INST_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_INST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_ILLEGAL_INST)) |
    (1ULL << (RISCV_EXCP_BREAKPOINT)) |
    (1ULL << (RISCV_EXCP_LOAD_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_LOAD_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ADDR_MIS)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_U_ECALL)) |
    (1ULL << (RISCV_EXCP_S_ECALL)) |
    (1ULL << (RISCV_EXCP_VS_ECALL)) |
    (1ULL << (RISCV_EXCP_M_ECALL)) |
    (1ULL << (RISCV_EXCP_INST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_INST_GUEST_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT)) |
    (1ULL << (RISCV_EXCP_VIRT_INSTRUCTION_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT)) |
#ifdef TARGET_CHERI
#ifndef TARGET_RISCV32
    (1ULL << (RISCV_EXCP_LOAD_CAP_PAGE_FAULT)) |
    (1ULL << (RISCV_EXCP_STORE_AMO_CAP_PAGE_FAULT)) |
#endif
    (1ULL << (RISCV_EXCP_CHERI)) |
#endif
    0;
static const target_ulong sstatus_v1_10_mask = SSTATUS_SIE | SSTATUS_SPIE |
    SSTATUS_UIE | SSTATUS_UPIE | SSTATUS_SPP | SSTATUS_FS | SSTATUS_XS |
    SSTATUS_SUM | SSTATUS_MXR
#if defined(TARGET_RISCV64)
    | SSTATUS64_UXL
#endif
    ;
static const target_ulong sip_writable_mask = SIP_SSIP | MIP_USIP | MIP_UEIP;
static const target_ulong hip_writable_mask = MIP_VSSIP;
static const target_ulong hvip_writable_mask = MIP_VSSIP | MIP_VSTIP | MIP_VSEIP;
static const target_ulong vsip_writable_mask = MIP_VSSIP;

static const char valid_vm_1_10_32[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV32] = 1
};

static const char valid_vm_1_10_64[16] = {
    [VM_1_10_MBARE] = 1,
    [VM_1_10_SV39] = 1,
    [VM_1_10_SV48] = 1,
    [VM_1_10_SV57] = 1
};

/* Machine Information Registers */
static RISCVException read_zero(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mhartid(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->mhartid;
    return RISCV_EXCP_NONE;
}

/* Machine Trap Setup */
static RISCVException read_mstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->mstatus;
    return RISCV_EXCP_NONE;
}

static int validate_vm(CPURISCVState *env, target_ulong vm)
{
    if (riscv_cpu_is_32bit(env)) {
        return valid_vm_1_10_32[vm & 0xf];
    } else {
        return valid_vm_1_10_64[vm & 0xf];
    }
}

static RISCVException write_mstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    uint64_t mstatus = env->mstatus;
    uint64_t mask = 0;
    int dirty;

    /* flush tlb on mstatus fields that affect VM */
    if ((val ^ mstatus) & (MSTATUS_MXR | MSTATUS_MPP | MSTATUS_MPV |
            MSTATUS_MPRV | MSTATUS_SUM)) {
        tlb_flush(env_cpu(env));
    }
    mask = MSTATUS_SIE | MSTATUS_SPIE | MSTATUS_MIE | MSTATUS_MPIE |
        MSTATUS_SPP | MSTATUS_FS | MSTATUS_MPRV | MSTATUS_SUM |
        MSTATUS_MPP | MSTATUS_MXR | MSTATUS_TVM | MSTATUS_TSR |
        MSTATUS_TW;

    if (!riscv_cpu_is_32bit(env)) {
        /*
         * RV32: MPV and GVA are not in mstatus. The current plan is to
         * add them to mstatush. For now, we just don't support it.
         */
        if (riscv_has_ext(env, RVH)) {
            mask |= MSTATUS_MPV | MSTATUS_GVA;
        }
    }

    mstatus = (mstatus & ~mask) | (val & mask);

    dirty = ((mstatus & MSTATUS_FS) == MSTATUS_FS) |
            ((mstatus & MSTATUS_XS) == MSTATUS_XS);
    mstatus = set_field(mstatus, MSTATUS_SD, dirty);
    env->mstatus = mstatus;

    return RISCV_EXCP_NONE;
}

static RISCVException read_mstatush(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->mstatus >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mstatush(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    uint64_t valh = (uint64_t)val << 32;
    uint64_t mask = MSTATUS_MPV | MSTATUS_GVA;

    if ((valh ^ env->mstatus) & (MSTATUS_MPV)) {
        tlb_flush(env_cpu(env));
    }

    env->mstatus = (env->mstatus & ~mask) | (valh & mask);

    return RISCV_EXCP_NONE;
}

static RISCVException read_misa(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = env->misa;
    return RISCV_EXCP_NONE;
}

static RISCVException write_misa(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MISA)) {
        /* drop write to misa */
        return RISCV_EXCP_NONE;
    }

    /*
     * XXXAR: this code is completely broken:
     * 1) you can only turn **on** misa.C if PC is not aligned to 4 bytes???
     * 2) They use GETPC() for this check! This is a QEMU internal program
     * counter (the current return address, so not even the TCG generated code
     * address since we could be multiple call stack levels down).
     *
     * Fortunately RISCV_FEATURE_MISA should never be enabled so we can't end
     * up here... If we ever do, abort() is the only safe way out!
     */
    abort();

    /* 'I' or 'E' must be present */
    if (!(val & (RVI | RVE))) {
        /* It is not, drop write to misa */
        return RISCV_EXCP_NONE;
    }

    /* 'E' excludes all other extensions */
    if (val & RVE) {
        /* when we support 'E' we can do "val = RVE;" however
         * for now we just drop writes if 'E' is present.
         */
        return RISCV_EXCP_NONE;
    }

    /* Mask extensions that are not supported by this hart */
    val &= env->misa_mask;

    /* Mask extensions that are not supported by QEMU */
    val &= (RVI | RVE | RVM | RVA | RVF | RVD | RVC | RVS | RVU);

    /* 'D' depends on 'F', so clear 'D' if 'F' is not present */
    if ((val & RVD) && !(val & RVF)) {
        val &= ~RVD;
    }

    /* Suppress 'C' if next instruction is not aligned
     * TODO: this should check next_pc
     */
    if ((val & RVC) && (GETPC() & ~3) != 0) {
        val &= ~RVC;
    }

    /* misa.MXL writes are not supported by QEMU */
    val = (env->misa & MISA_MXL) | (val & ~MISA_MXL);

    /* flush translation cache */
    if (val != env->misa) {
        tb_flush(env_cpu(env));
    }

    env->misa = val;

    return RISCV_EXCP_NONE;
}

static RISCVException read_medeleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->medeleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_medeleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->medeleg = (env->medeleg & ~delegable_excps) | (val & delegable_excps);
    return RISCV_EXCP_NONE;
}

static RISCVException read_mideleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->mideleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mideleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->mideleg = (env->mideleg & ~delegable_ints) | (val & delegable_ints);
    if (riscv_has_ext(env, RVH)) {
        env->mideleg |= VS_MODE_INTERRUPTS;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_mie(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    *val = env->mie;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mie(CPURISCVState *env, int csrno,
                                target_ulong val)
{
    env->mie = (env->mie & ~all_ints) | (val & all_ints);
    return RISCV_EXCP_NONE;
}

static RISCVException read_mcounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mcounteren;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcounteren(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->mcounteren = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mcause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mcause;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mcause(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    env->mcause = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtval(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->mtval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtval(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->mtval = val;
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_mip(CPURISCVState *env, int csrno,
                              target_ulong *ret_value,
                              target_ulong new_value, target_ulong write_mask)
{
    RISCVCPU *cpu = env_archcpu(env);
    /* Allow software control of delegable interrupts not claimed by hardware */
    target_ulong mask = write_mask & delegable_ints & ~env->miclaim;
    uint32_t old_mip;

    if (mask) {
        old_mip = riscv_cpu_update_mip(cpu, mask, (new_value & mask));
    } else {
        old_mip = env->mip;
    }

    if (ret_value) {
        *ret_value = old_mip;
    }

    return RISCV_EXCP_NONE;
}

/* Supervisor Trap Setup */
static RISCVException read_sstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    target_ulong mask = (sstatus_v1_10_mask);

    if (riscv_cpu_is_32bit(env)) {
        mask |= SSTATUS32_SD;
    } else {
        mask |= SSTATUS64_SD;
    }

    *val = env->mstatus & mask;
    return RISCV_EXCP_NONE;
}

static RISCVException write_sstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    target_ulong mask = (sstatus_v1_10_mask);
    target_ulong newval = (env->mstatus & ~mask) | (val & mask);
    return write_mstatus(env, CSR_MSTATUS, newval);
}

static RISCVException read_vsie(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    /* Shift the VS bits to their S bit location in vsie */
    *val = (env->mie & env->hideleg & VS_MODE_INTERRUPTS) >> 1;
    return RISCV_EXCP_NONE;
}

static RISCVException read_sie(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    if (riscv_cpu_virt_enabled(env)) {
        read_vsie(env, CSR_VSIE, val);
    } else {
        *val = env->mie & env->mideleg;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsie(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    /* Shift the S bits to their VS bit location in mie */
    target_ulong newval = (env->mie & ~VS_MODE_INTERRUPTS) |
                          ((val << 1) & env->hideleg & VS_MODE_INTERRUPTS);
    return write_mie(env, CSR_MIE, newval);
}

static RISCVException write_sie(CPURISCVState *env, int csrno, target_ulong val)
{
    if (riscv_cpu_virt_enabled(env)) {
        write_vsie(env, CSR_VSIE, val);
    } else {
        target_ulong newval = (env->mie & ~S_MODE_INTERRUPTS) |
                              (val & S_MODE_INTERRUPTS);
        write_mie(env, CSR_MIE, newval);
    }

    return RISCV_EXCP_NONE;
}

static RISCVException read_scounteren(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->scounteren;
    return RISCV_EXCP_NONE;
}

static RISCVException write_scounteren(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->scounteren = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_scause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->scause;
    return RISCV_EXCP_NONE;
}

static RISCVException write_scause(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->scause = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_stval(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->stval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_stval(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->stval = val;
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_vsip(CPURISCVState *env, int csrno,
                               target_ulong *ret_value,
                               target_ulong new_value, target_ulong write_mask)
{
    /* Shift the S bits to their VS bit location in mip */
    int ret = rmw_mip(env, 0, ret_value, new_value << 1,
                      (write_mask << 1) & vsip_writable_mask & env->hideleg);
    *ret_value &= VS_MODE_INTERRUPTS;
    /* Shift the VS bits to their S bit location in vsip */
    *ret_value >>= 1;
    return ret;
}

static RISCVException rmw_sip(CPURISCVState *env, int csrno,
                              target_ulong *ret_value,
                              target_ulong new_value, target_ulong write_mask)
{
    int ret;

    if (riscv_cpu_virt_enabled(env)) {
        ret = rmw_vsip(env, CSR_VSIP, ret_value, new_value, write_mask);
    } else {
        ret = rmw_mip(env, CSR_MSTATUS, ret_value, new_value,
                      write_mask & env->mideleg & sip_writable_mask);
    }

    *ret_value &= env->mideleg;
    return ret;
}

/* Supervisor Protection and Translation */
static RISCVException read_satp(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        *val = 0;
        return RISCV_EXCP_NONE;
    }

    if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
        return RISCV_EXCP_ILLEGAL_INST;
    } else {
        *val = env->satp;
    }

    return RISCV_EXCP_NONE;
}

static RISCVException write_satp(CPURISCVState *env, int csrno,
                                 target_ulong val)
{
    if (!riscv_feature(env, RISCV_FEATURE_MMU)) {
        return RISCV_EXCP_NONE;
    }
    if (validate_vm(env, get_field(val, SATP_MODE)) &&
        ((val ^ env->satp) & (SATP_MODE | SATP_ASID | SATP_PPN)))
    {
        if (env->priv == PRV_S && get_field(env->mstatus, MSTATUS_TVM)) {
            return RISCV_EXCP_ILLEGAL_INST;
        } else {
            if ((val ^ env->satp) & SATP_ASID) {
                tlb_flush(env_cpu(env));
            }
            env->satp = val;
        }
    }
    return RISCV_EXCP_NONE;
}

/* Hypervisor Extensions */
static RISCVException read_hstatus(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->hstatus;
    if (!riscv_cpu_is_32bit(env)) {
        /* We only support 64-bit VSXL */
        *val = set_field(*val, HSTATUS_VSXL, 2);
    }
    /* We only support little endian */
    *val = set_field(*val, HSTATUS_VSBE, 0);
    return RISCV_EXCP_NONE;
}

static RISCVException write_hstatus(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->hstatus = val;
    if (!riscv_cpu_is_32bit(env) && get_field(val, HSTATUS_VSXL) != 2) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support mixed HSXLEN options.");
    }
    if (get_field(val, HSTATUS_VSBE) != 0) {
        qemu_log_mask(LOG_UNIMP, "QEMU does not support big endian guests.");
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_hedeleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->hedeleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hedeleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->hedeleg = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_hideleg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = env->hideleg;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hideleg(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->hideleg = val;
    return RISCV_EXCP_NONE;
}

static RISCVException rmw_hvip(CPURISCVState *env, int csrno,
                               target_ulong *ret_value,
                               target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & hvip_writable_mask);

    *ret_value &= hvip_writable_mask;

    return ret;
}

static RISCVException rmw_hip(CPURISCVState *env, int csrno,
                              target_ulong *ret_value,
                              target_ulong new_value, target_ulong write_mask)
{
    int ret = rmw_mip(env, 0, ret_value, new_value,
                      write_mask & hip_writable_mask);

    *ret_value &= hip_writable_mask;

    return ret;
}

static RISCVException read_hie(CPURISCVState *env, int csrno,
                               target_ulong *val)
{
    *val = env->mie & VS_MODE_INTERRUPTS;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hie(CPURISCVState *env, int csrno,
                                target_ulong val)
{
    target_ulong newval = (env->mie & ~VS_MODE_INTERRUPTS) | (val & VS_MODE_INTERRUPTS);
    return write_mie(env, CSR_MIE, newval);
}

static RISCVException read_hcounteren(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    *val = env->hcounteren;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hcounteren(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    env->hcounteren = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_hgeie(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return RISCV_EXCP_NONE;
}

static RISCVException write_hgeie(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return RISCV_EXCP_NONE;
}

static RISCVException read_htval(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->htval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htval(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->htval = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_htinst(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->htinst;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htinst(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    return RISCV_EXCP_NONE;
}

static RISCVException read_hgeip(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return RISCV_EXCP_NONE;
}

static RISCVException write_hgeip(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    qemu_log_mask(LOG_UNIMP, "No support for a non-zero GEILEN.");
    return RISCV_EXCP_NONE;
}

static RISCVException read_hgatp(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->hgatp;
    return RISCV_EXCP_NONE;
}

static RISCVException write_hgatp(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->hgatp = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_htimedelta(CPURISCVState *env, int csrno,
                                      target_ulong *val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htimedelta(CPURISCVState *env, int csrno,
                                       target_ulong val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    if (riscv_cpu_is_32bit(env)) {
        env->htimedelta = deposit64(env->htimedelta, 0, 32, (uint64_t)val);
    } else {
        env->htimedelta = val;
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_htimedeltah(CPURISCVState *env, int csrno,
                                       target_ulong *val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    *val = env->htimedelta >> 32;
    return RISCV_EXCP_NONE;
}

static RISCVException write_htimedeltah(CPURISCVState *env, int csrno,
                                        target_ulong val)
{
    if (!env->rdtime_fn) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    env->htimedelta = deposit64(env->htimedelta, 32, 32, (uint64_t)val);
    return RISCV_EXCP_NONE;
}

/* Virtual CSR Registers */
static RISCVException read_vsstatus(CPURISCVState *env, int csrno,
                                    target_ulong *val)
{
    *val = env->vsstatus;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsstatus(CPURISCVState *env, int csrno,
                                     target_ulong val)
{
    uint64_t mask = (target_ulong)-1;
    env->vsstatus = (env->vsstatus & ~mask) | (uint64_t)val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vsscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsscratch;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsscratch(CPURISCVState *env, int csrno,
                                      target_ulong val)
{
    env->vsscratch = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vscause(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vscause;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vscause(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    env->vscause = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vstval(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->vstval;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vstval(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->vstval = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_vsatp(CPURISCVState *env, int csrno,
                                 target_ulong *val)
{
    *val = env->vsatp;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsatp(CPURISCVState *env, int csrno,
                                  target_ulong val)
{
    env->vsatp = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtval2(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->mtval2;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtval2(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->mtval2 = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mtinst(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = env->mtinst;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtinst(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    env->mtinst = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_menvcfg(CPURISCVState *env, int csrno, target_ulong *val)
{
    // at present the CRE bit is the only supported field in the register
    *val = env->menvcfg & MENVCFG_CRE;
    return RISCV_EXCP_NONE;
}

static RISCVException write_menvcfg(CPURISCVState *env, int csrno, target_ulong val)
{
    // at present the CRE bit is the only supported field in the register
    env->menvcfg = (val & MENVCFG_CRE);
    return RISCV_EXCP_NONE;
}

static RISCVException read_senvcfg(CPURISCVState *env, int csrno, target_ulong *val)
{
    // at present the CRE bit is the only supported field in the register
    *val = env->senvcfg & SENVCFG_CRE;
    return RISCV_EXCP_NONE;
}

static RISCVException write_senvcfg(CPURISCVState *env, int csrno, target_ulong val)
{
    // at present the CRE bit is the only supported field in the register
    env->senvcfg = (val & SENVCFG_CRE);
    return RISCV_EXCP_NONE;
}


/* Physical Memory Protection */
static RISCVException read_mseccfg(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = mseccfg_csr_read(env);
    return RISCV_EXCP_NONE;
}

static RISCVException write_mseccfg(CPURISCVState *env, int csrno,
                         target_ulong val)
{
    mseccfg_csr_write(env, val);
    return RISCV_EXCP_NONE;
}

static RISCVException read_pmpcfg(CPURISCVState *env, int csrno,
                                  target_ulong *val)
{
    *val = pmpcfg_csr_read(env, csrno - CSR_PMPCFG0);
    return RISCV_EXCP_NONE;
}

static RISCVException write_pmpcfg(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    pmpcfg_csr_write(env, csrno - CSR_PMPCFG0, val);
#ifdef CONFIG_TCG_LOG_INSTR
    if (qemu_log_instr_enabled(env)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "pmpcfg%d", csrno - CSR_PMPCFG0);
        qemu_log_instr_reg(env, buf, val, csrno, LRI_CSR_ACCESS);
    }
#endif
    return RISCV_EXCP_NONE;
}

static RISCVException read_pmpaddr(CPURISCVState *env, int csrno,
                                   target_ulong *val)
{
    *val = pmpaddr_csr_read(env, csrno - CSR_PMPADDR0);
    return RISCV_EXCP_NONE;
}

static RISCVException write_pmpaddr(CPURISCVState *env, int csrno,
                                    target_ulong val)
{
    pmpaddr_csr_write(env, csrno - CSR_PMPADDR0, val);
#ifdef CONFIG_TCG_LOG_INSTR
    if (qemu_log_instr_enabled(env)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "pmpaddr%d", csrno - CSR_PMPADDR0);
        qemu_log_instr_reg(env, buf, val, csrno, LRI_CSR_ACCESS);
    }
#endif
    return RISCV_EXCP_NONE;
}

#endif

#ifndef TARGET_CHERI
/* Integer CSR Read/write functions for CSRs which have CLEN counterparts*/
/* Machine Trap Handling */
static RISCVException read_mtvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mtvec;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mtvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->mtvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_MTVEC: reserved mode not supported\n");
    }
    return RISCV_EXCP_NONE;
}

static RISCVException read_mscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mscratch;
    return RISCV_EXCP_NONE;
}

static RISCVException write_mscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mscratch = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_mepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->mepc;
    // RISC-V privileged spec 3.1.15 Machine Exception Program Counter (mepc):
    // "The low bit of mepc (mepc[0]) is always zero. [...] Whenever IALIGN=32,
    // mepc[1] is masked on reads so that it appears to be 0."
    *val &= ~(target_ulong)(riscv_has_ext(env, RVC) ? 1 : 3);
    return RISCV_EXCP_NONE;
}

static RISCVException write_mepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->mepc = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_stvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->stvec;
    return RISCV_EXCP_NONE;
}

/* Supervisor Trap Handling */
static RISCVException write_stvec(CPURISCVState *env, int csrno, target_ulong val)
{
    /* bits [1:0] encode mode; 0 = direct, 1 = vectored, 2 >= reserved */
    if ((val & 3) < 2) {
        env->stvec = val;
    } else {
        qemu_log_mask(LOG_UNIMP, "CSR_STVEC: reserved mode not supported\n");
    }
    return RISCV_EXCP_NONE;
}
static RISCVException read_sscratch(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sscratch;
    return RISCV_EXCP_NONE;
}

static RISCVException write_sscratch(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sscratch = val;
    return RISCV_EXCP_NONE;
}

static RISCVException read_sepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->sepc;
    // RISC-V privileged spec 4.1.7 Supervisor Exception Program Counter (sepc)
    // "The low bit of sepc (sepc[0]) is always zero. [...] Whenever IALIGN=32,
    // sepc[1] is masked on reads so that it appears to be 0."
    *val &= ~(target_ulong)(riscv_has_ext(env, RVC) ? 1 : 3);
    return RISCV_EXCP_NONE;
}

static RISCVException write_sepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->sepc = val;
    return RISCV_EXCP_NONE;
}
static RISCVException read_vstvec(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vstvec;
    return RISCV_EXCP_NONE;
}

/* Hypervisor trap handling*/
static RISCVException write_vstvec(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vstvec = val;
    return RISCV_EXCP_NONE;
}
static RISCVException read_vsepc(CPURISCVState *env, int csrno, target_ulong *val)
{
    *val = env->vsepc;
    return RISCV_EXCP_NONE;
}

static RISCVException write_vsepc(CPURISCVState *env, int csrno, target_ulong val)
{
    env->vsepc = val;
    return RISCV_EXCP_NONE;
}
#else
// defined(TARGET_CHERI)
static inline cap_register_t *get_cap_csr(CPUArchState *env, uint32_t index)
{
    switch (index) {
    case CSR_MSCRATCHC:
        return &env->MScratchC;
    case CSR_MTVECC:
        return &env->MTVECC;
    case CSR_STVECC:
        return &env->STVECC;
    case CSR_MEPCC:
        return &env->MEPCC;
    case CSR_SEPCC:
        return &env->SEPCC;
    case CSR_SSCRATCHC:
        return &env->SScratchC;
    case CSR_DSCRATCH0C:
        return &env->dscratch0c;
    case CSR_DSCRATCH1C:
        return &env->dscratch1c;
    case CSR_DPCC:
        return &env->dpcc;
    case CSR_DDDC:
        return &env->dddc;
    case CSR_JVTC:
        return &env->jvtc;
    case CSR_DINFC:
        assert(false && "Should never be called to get &dinfc");
    case CSR_MTDC:
        return &env->MTDC;
    case CSR_STDC:
        return &env->STDC;
    case CSR_DDC:
        return &env->DDC;
    case CSR_MTIDC:
        return &env->mtidc;
    case CSR_STIDC:
        return &env->stidc;
    case CSR_UTIDC:
        return &env->utidc;
    default:
        assert(false && "Should have raised an invalid inst trap!");
    }
}

/* Reads a capability length csr register taking into account the current
cheri execution mode*/
static cap_register_t read_capcsr_reg(CPURISCVState *env,
                                      riscv_csr_cap_ops *csr_cap_info)
{
    cap_register_t retval = *get_cap_csr(env, csr_cap_info->reg_num);
    return retval;
}

#define get_bit(reg, x) (reg & (1 << x) ? true : false)

// Borrow the signextend function from capstone
static inline int64_t SignExtend64(uint64_t X, unsigned B)
{
    return (int64_t)(X << (64 - B)) >> (64 - B);
}


static inline uint8_t topbit_for_address_mode(CPUArchState *env){
    uint64_t vm = get_field(env->vsatp, SATP_MODE);
    uint8_t checkbit = 0;
    switch (vm) {
    case VM_1_10_SV32:
        checkbit = 31;
        break;
    case VM_1_10_SV39:
        checkbit = 38;
        break;
    case VM_1_10_SV48:
        checkbit = 47;
        break;
    case VM_1_10_SV57:
        checkbit = 56;
        break;
    default:
        g_assert_not_reached();
    }
    return checkbit;
}

/*
Check if the address is valid for the target capability.
This depends on the addrress mode
• For Sv39, bits [63:39] must equal bit 38
• For Sv48, bits [63:48] must equal bit 47
• For Sv57, bits [63:57] must equal bit 56
If address translation is not active or we are using sv32 then treat the address 
as valid.
This only applies for rv64
*/
static inline bool is_address_valid_for_cap(CPUArchState *env,
                                            cap_register_t cap,
                                            target_ulong addr)
{
#ifdef TARGET_RISCV32
    return true;
#endif
    uint64_t vm = get_field(env->vsatp, SATP_MODE);
    if (vm == VM_1_10_MBARE || vm == VM_1_10_SV32) {
        return true;
    }
    uint8_t checkbit = topbit_for_address_mode(env);
    target_ulong address = cap_get_cursor(&cap);
    target_ulong extend_address = SignExtend64(address, checkbit);
    if (address == extend_address) {
        // this is a valid address
        return true;
    }
    // need to check for infinite bounds.
    if (cap_get_base(&cap) == 0 && cap_get_top_full(&cap) == CAP_MAX_TOP) {
        return true;
    }
    return false;
}

/*
Return a valid capability address field.
This is implementation dependant and depends on the address translation mode
*/
static inline target_ulong get_valid_cap_address(CPUArchState *env,
                                                 target_ulong addr)
{
    uint64_t vm = get_field(env->vsatp, SATP_MODE);
    if (vm == VM_1_10_MBARE || vm == VM_1_10_SV32) {
        return addr;
    }
    uint8_t checkbit = topbit_for_address_mode(env);
    target_ulong extend_address = SignExtend64(addr, checkbit);
    return extend_address;
}

/*
Given a capability and address turn the address into a valid address for that
capability and return true if the address was changed 
*/ 
static inline bool validate_cap_address(CPUArchState *env, cap_register_t *cap,
                                        target_ulong *address)
{
    if (is_address_valid_for_cap(env, *cap, *address)) {
        return false;
    }
    *address = get_valid_cap_address(env, *address);
    return true;
}

/*
The function takes both the source capability as well as the cursor value.
For CLEN writes the source capabilities bounds would be taken into account
when computing the invalid address conversion..

*/
static void write_cap_csr_reg(CPURISCVState *env,
                              riscv_csr_cap_ops *csr_cap_info,
                              cap_register_t src, target_ulong newval,
                              bool clen)
{
    cap_register_t csr = *get_cap_csr(env, csr_cap_info->reg_num);
    if (clen) // this will apply only to csrrw calls, all other writes are
              // xlen
    {
        if (csr_cap_info->invalid_address_conversion) {
            bool changed = validate_cap_address(env, &src, &newval);

            if (csr_cap_info->update_scadrr) {
                // xxvec, dpcc
                // write PC using scaddr
                src = cap_scaddr(newval, csr); // always update with scaddr
            } else { // only apply scadd if validate changes the address
                     // mepcc, sepcc., jvtc
                if (changed) {
                    src = cap_scaddr(newval, csr);
                } else {
                    // update the csr with direct write
                }
            }
            // else drop through and direct write src
            // dpcc, mepcc,
        } else {
            // xscratchx, xxidc
            // just drop through to do the write
            // direct write
        }
    } else { // xlen

        if (csr_cap_info->invalid_address_conversion) {
            // ignore changed as we always use scaddr
            validate_cap_address(env, &csr, &newval);
        }
        src = cap_scaddr(newval, csr);
    }
    // log the value and write it
    *get_cap_csr(env, csr_cap_info->reg_num) = src;
    cheri_log_instr_changed_capreg(env, csr_cap_info->name, &src,
                                   csr_cap_info->reg_num, LRI_CSR_ACCESS);
}

static void write_xtvecc(CPURISCVState *env, riscv_csr_cap_ops *csr_cap_info,
                         cap_register_t src, target_ulong new_tvec, bool clen)
{
    bool valid = true;
    cap_register_t *csr = get_cap_csr(env, csr_cap_info->reg_num);
    /* The low two bits encode the mode, but only 0 and 1 are valid. */
    if ((new_tvec & 3) > 1) {
        /* Invalid mode, keep the old one. */
        new_tvec &= ~(target_ulong)3;
        new_tvec |= cap_get_cursor(csr) & 3;
    }

    // the function needs to know if if it using the src capability or the csr's
    // existing capability in order to do the representable check.
    cap_register_t *auth;
    if (clen) { // use the source capability for checking the vector range
        auth = &src;
    } else { // use the csr register
        auth = csr;
    }

    if (!is_representable_cap_with_addr(auth, new_tvec + RISCV_HICAUSE * 4)) {
        error_report("Attempting to set vector register with unrepresentable "
                     "range (0x" TARGET_FMT_lx ") on %s: " PRINT_CAP_FMTSTR
                     "\r\n",
                     new_tvec, csr_cap_info->name, PRINT_CAP_ARGS(auth));
        qemu_log_instr_extra(
            env,
            "Attempting to set unrepresentable vector register with "
            "unrepresentable range (0x" TARGET_FMT_lx
            ") on %s: " PRINT_CAP_FMTSTR "\r\n",
            new_tvec, csr_cap_info->name, PRINT_CAP_ARGS(auth));
        valid = false;
    }
    if (!valid) {
        // caution this directly modifies the tareget csr register in integer
        // mode this should be ok, as it is invalidating the tag which is the
        // intended action
        cap_mark_unrepresentable(new_tvec, auth);
    }

    write_cap_csr_reg(env, csr_cap_info, src, new_tvec, clen);
}

static void write_xepcc(CPURISCVState *env, riscv_csr_cap_ops *csr_cap_info,
                        cap_register_t src, target_ulong new_xepcc, bool clen)
{
    new_xepcc &= (~0x1); // Zero bit zero
    write_cap_csr_reg(env, csr_cap_info, src, new_xepcc, clen);
}

// Common read function for the mepcc and sepcc registers
static cap_register_t read_xepcc(CPURISCVState *env,
                                 riscv_csr_cap_ops *csr_cap_info)
{
    cap_register_t retval = *get_cap_csr(env, csr_cap_info->reg_num);
    target_ulong val = cap_get_cursor(&retval);

    // RISC-V privileged spec 4.1.7 Supervisor Exception Program Counter
    // (sepc) "The low bit of sepc (sepc[0]) is always zero. [...] Whenever
    // IALIGN=32, sepc[1] is masked on reads so that it appears to be 0."
    val &= ~(target_ulong)(riscv_has_ext(env, RVC) ? 1 : 3);
    if (val != cap_get_cursor(&retval)) {
        warn_report("Clearing low bit(s) of MXPCC (contained an unaligned "
                    "capability): " PRINT_CAP_FMTSTR,
                    PRINT_CAP_ARGS(&retval));
        cap_set_cursor(&retval, val);
    }
    if (!cap_is_unsealed(&retval)) {
        warn_report("Invalidating sealed XEPCC (contained an unaligned "
                    "capability): " PRINT_CAP_FMTSTR,
                    PRINT_CAP_ARGS(&retval));
        retval.cr_tag = false;
    }

    cap_set_cursor(&retval, val);
    return retval;
}

static void write_dinfc(CPURISCVState *env, riscv_csr_cap_ops *csr_cap_info,
                        cap_register_t src, target_ulong newval, bool clen)
{
    /* Writing to dinf is allowed but ignored*/
    qemu_log_mask(CPU_LOG_INT, "Attempting to write dinfc is ignored\n");
}

static cap_register_t read_dinfc(CPURISCVState *env,
                                 riscv_csr_cap_ops *csr_cap_info)
{
    cap_register_t inf;
    assert(cheri_in_capmode(env) &&
           "Expect reads of dinfc only in debug/cap mode");
    set_max_perms_capability(env, &inf,0);
    return inf;
}

/*
Based on csr number and write mask determine if this register access requires
ASR architectural permissions.
Privilege mode indicated by bits 9:8 of csrno == 0 from 
RiscV Instuction Set Volume II, Section 2.1 CSR Address Mapping Conventions
*/
bool csr_needs_asr(int csrno, bool write)
{
    // based on CSR mapping conventions we can determine if the CSR is
    // privileged based on either bits 8-9 being set
    // however utidc is an exception to this instead and is treated as privilged
    // for asr checks.
    // in addition we also care about the write mask for the thread id regs
    switch (csrno) {
    case CSR_STIDC:
    case CSR_MTIDC:
    case CSR_UTIDC:
        return write; // the TID registers only require asr for writes
    default:
        return get_field(csrno, 0x300);
    }
}
#endif

/*
 * riscv_csrrw - read and/or update control and status register
 *
 * csrr   <->  riscv_csrrw(env, csrno, ret_value, 0, 0);
 * csrrw  <->  riscv_csrrw(env, csrno, ret_value, value, -1);
 * csrrs  <->  riscv_csrrw(env, csrno, ret_value, -1, value);
 * csrrc  <->  riscv_csrrw(env, csrno, ret_value, 0, value);
 */

RISCVException riscv_csrrw(CPURISCVState *env, int csrno,
                           target_ulong *ret_value,
                           target_ulong new_value, target_ulong write_mask,
                           uintptr_t retpc)
{
    RISCVException ret;
    target_ulong old_value;
    RISCVCPU *cpu = env_archcpu(env);

    /* check privileges and return -1 if check fails */
#if !defined(CONFIG_USER_ONLY)
    int effective_priv = env->priv;
    int read_only = get_field(csrno, 0xC00) == 3;

    if (riscv_has_ext(env, RVH) &&
        env->priv == PRV_S &&
        !riscv_cpu_virt_enabled(env)) {
        /*
         * We are in S mode without virtualisation, therefore we are in HS Mode.
         * Add 1 to the effective privledge level to allow us to access the
         * Hypervisor CSRs.
         */
        effective_priv++;
    }

    if ((write_mask && read_only) ||
        (!env->debugger && (effective_priv < get_field(csrno, 0x300)))) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
#endif

    /* ensure the CSR extension is enabled. */
    if (!cpu->cfg.ext_icsr) {
        return RISCV_EXCP_ILLEGAL_INST;
    }

    /* check predicate */
    if (!csr_ops[csrno].predicate) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    ret = csr_ops[csrno].predicate(env, csrno);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    // When CHERI is enabled, only certain CSRs can be accessed without the
    // Access_System_Registers permission in PCC.
#ifdef TARGET_CHERI
    if (!cheri_have_access_sysregs(env) &&
        csr_needs_asr(csrno, (bool)write_mask)) {
#if !defined(CONFIG_USER_ONLY)
        if (env->debugger) {
            return -1;
        }
        raise_cheri_exception_impl(env, CapEx_AccessSystemRegsViolation,
                                   CapExType_InstrAccess, /*regnum=*/0, 0, true,
                                   retpc);
#endif
    }
#endif // TARGET_CHERI

    /* execute combined read/write operation if it exists */
    if (csr_ops[csrno].op) {
        ret = csr_ops[csrno].op(env, csrno, ret_value, new_value, write_mask);
#ifdef CONFIG_TCG_LOG_INSTR
        if (ret >= 0 && csr_ops[csrno].log_update) {
            csr_ops[csrno].log_update(env, csrno, new_value);
        }
#endif
        return ret;
    }

    /* if no accessor exists then return failure */
    if (!csr_ops[csrno].read) {
        return RISCV_EXCP_ILLEGAL_INST;
    }
    /* read old value */
    ret = csr_ops[csrno].read(env, csrno, &old_value);
    if (ret != RISCV_EXCP_NONE) {
        return ret;
    }

    /* write value if writable and write mask set, otherwise drop writes */
    if (write_mask) {
        new_value = (old_value & ~write_mask) | (new_value & write_mask);
        if (csr_ops[csrno].write) {
            ret = csr_ops[csrno].write(env, csrno, new_value);
            if (ret != RISCV_EXCP_NONE) {
                return ret;
            }
#ifdef CONFIG_TCG_LOG_INSTR
            if (csr_ops[csrno].log_update){
                csr_ops[csrno].read(env, csrno, &new_value);
                csr_ops[csrno].log_update(env, csrno, new_value);
                if (csrno == CSR_FCSR){
                    // special case handling of the FCSR we also need to log mstatus
                    // as writes to FCSR can change the MSTATUS value
                    csr_ops[CSR_MSTATUS].read(env,CSR_MSTATUS,&new_value);
                    csr_ops[CSR_MSTATUS].log_update(env, CSR_MSTATUS, new_value);
                }

            }
#endif
        }
    }

    /* return old value */
    if (ret_value) {
        *ret_value = old_value;
    }

    return RISCV_EXCP_NONE;
}

/*
 * Debugger support.  If not in user mode, set env->debugger before the
 * riscv_csrrw call and clear it after the call.
 */
RISCVException riscv_csrrw_debug(CPURISCVState *env, int csrno,
                                 target_ulong *ret_value,
                                 target_ulong new_value,
                                 target_ulong write_mask)
{
    RISCVException ret;
#if !defined(CONFIG_USER_ONLY)
    env->debugger = true;
#endif
    ret = riscv_csrrw(env, csrno, ret_value, new_value, write_mask, 0);
#if !defined(CONFIG_USER_ONLY)
    env->debugger = false;
#endif
    return ret;
}


#ifdef CONFIG_TCG_LOG_INSTR
static void log_changed_csr_fn(CPURISCVState *env, int csrno,
                               target_ulong value)
{
    if (qemu_log_instr_enabled(env)) {
        qemu_log_instr_reg(env, csr_ops[csrno].csr_name, value, csrno,
                           LRI_CSR_ACCESS);
    }
}
#else
#define log_changed_csr_fn (NULL)
#endif

/* Define csr_ops entry for read-only CSR register */
#define CSR_OP_FN_R(pred, readfn, name)                            \
    {.predicate=pred, .read=readfn, .write=NULL, .op=NULL,         \
     .log_update=NULL, .csr_name=name}

/* Shorthand for functions following the read_<csr> pattern */
#define CSR_OP_R(pred, name)                                    \
    CSR_OP_FN_R(pred, glue(read_, name), stringify(name))

/* Internal - use CSR_OP_FN_RW, CSR_OP_RW and CSR_OP_NOLOG_RW */
#define _CSR_OP_FN_RW(pred, readfn, writefn, logfn, name)          \
    {.predicate=pred, .read=readfn, .write=writefn,                \
     .op=NULL, .log_update=logfn, .csr_name=name}

/* Define csr_ops entry for read-write CSR register */
#define CSR_OP_FN_RW(pred, readfn, writefn, name)                  \
    _CSR_OP_FN_RW(pred, readfn, writefn, log_changed_csr_fn, name)

/* Shorthand for functions following the read/write_<csr> pattern */
#define CSR_OP_RW(pred, name)                                      \
    CSR_OP_FN_RW(pred, glue(read_, name), glue(write_, name),      \
                 stringify(name))

/*
 * Shorthand for functions following the read/write_<csr> pattern,
 * with custom write logging.
 */
#define CSR_OP_NOLOG_RW(pred, name)                                \
    _CSR_OP_FN_RW(pred, glue(read_, name), glue(write_, name),     \
                  NULL, stringify(name))

/* Define csr_ops entry for read-modify-write CSR register */
#define CSR_OP_RMW(pred, name)                                     \
    {.predicate=pred, .read=NULL, .write=NULL,                     \
     .op=glue(rmw_, name), .log_update=log_changed_csr_fn,         \
     .csr_name=stringify(name)}

/* Control and Status Register function table */
riscv_csr_operations csr_ops[CSR_TABLE_SIZE] = {
    /* User Floating-Point CSRs */
    [CSR_FFLAGS] =              CSR_OP_RW(fs, fflags),
    [CSR_FRM] =                 CSR_OP_RW(fs, frm),
    [CSR_FCSR] =                CSR_OP_RW(fs, fcsr),

    /* Vector CSRs */
    [CSR_VSTART] =              CSR_OP_RW(vs, vstart),
    [CSR_VXSAT] =               CSR_OP_RW(vs, vxsat),
    [CSR_VXRM] =                CSR_OP_RW(vs, vxrm),
    [CSR_VL] =                  CSR_OP_R(vs, vl),
    [CSR_VTYPE] =               CSR_OP_R(vs, vtype),
    /* User Timers and Counters */
    [CSR_CYCLE] =               CSR_OP_FN_R(ctr, read_instret, "cycle"),
    [CSR_INSTRET] =             CSR_OP_FN_R(ctr, read_instret, "instret"),
    [CSR_CYCLEH] =              CSR_OP_FN_R(ctr32, read_instreth, "cycleh"),
    [CSR_INSTRETH] =            CSR_OP_FN_R(ctr32, read_instreth, "instreth"),

    /*
     * In privileged mode, the monitor will have to emulate TIME CSRs only if
     * rdtime callback is not provided by machine/platform emulation.
     */
    [CSR_TIME] =                CSR_OP_R(ctr, time),
    [CSR_TIMEH] =               CSR_OP_R(ctr32, timeh),

#if !defined(CONFIG_USER_ONLY)
    /* Machine Timers and Counters */
    [CSR_MCYCLE] =              CSR_OP_FN_R(any, read_instret, "mcycle"),
    [CSR_MINSTRET] =            CSR_OP_FN_R(any, read_instret, "minstret"),
    [CSR_MCYCLEH] =             CSR_OP_FN_R(any32, read_instreth, "mcycleh"),
    [CSR_MINSTRETH] =           CSR_OP_FN_R(any32, read_instreth, "minstreth"),

    /* Machine Information Registers */
    [CSR_MVENDORID] =           CSR_OP_FN_R(any, read_zero, "mvendorid"),
    [CSR_MARCHID] =             CSR_OP_FN_R(any, read_zero, "marchid"),
    [CSR_MIMPID] =              CSR_OP_FN_R(any, read_zero, "mimppid"),
    [CSR_MHARTID] =             CSR_OP_R(any, mhartid),

    /* Machine Trap Setup */
    [CSR_MSTATUS] =             CSR_OP_RW(any, mstatus),
    [CSR_MISA] =                CSR_OP_RW(any, misa),
    [CSR_MIDELEG] =             CSR_OP_RW(any, mideleg),
    [CSR_MEDELEG] =             CSR_OP_RW(any, medeleg),
    [CSR_MIE] =                 CSR_OP_RW(any, mie),
    [CSR_MCOUNTEREN] =          CSR_OP_RW(any, mcounteren),

    [CSR_MSTATUSH] =            CSR_OP_RW(any32, mstatush),

    [CSR_MENVCFG] =             CSR_OP_RW(any, menvcfg),
    [CSR_SENVCFG] =             CSR_OP_RW(any, senvcfg),

    /* Machine Trap Handling */
    [CSR_MCAUSE] =              CSR_OP_RW(any, mcause),
    [CSR_MTVAL] =            CSR_OP_RW(any, mtval),
    [CSR_MIP] =                 CSR_OP_RMW(any, mip),

    /* Supervisor Trap Setup */
    [CSR_SSTATUS] =             CSR_OP_RW(smode, sstatus),
    [CSR_SIE] =                 CSR_OP_RW(smode, sie),
    [CSR_SCOUNTEREN] =          CSR_OP_RW(smode, scounteren),

    /* Supervisor Trap Handling */
    [CSR_SCAUSE] =              CSR_OP_RW(smode, scause),
    [CSR_STVAL] =               CSR_OP_RW(any, stval),
    [CSR_SIP] =                 CSR_OP_RMW(smode, sip),

    /* Supervisor Protection and Translation */
    [CSR_SATP] =                CSR_OP_RW(smode, satp),

    [CSR_HSTATUS] =             CSR_OP_RW(hmode, hstatus),
    [CSR_HEDELEG] =             CSR_OP_RW(hmode, hedeleg),
    [CSR_HIDELEG] =             CSR_OP_RW(hmode, hideleg),
    [CSR_HVIP] =                CSR_OP_RMW(hmode, hvip),
    [CSR_HIP] =                 CSR_OP_RMW(hmode, hip),
    [CSR_HIE] =                 CSR_OP_RW(hmode, hie),
    [CSR_HCOUNTEREN] =          CSR_OP_RW(hmode, hcounteren),
    [CSR_HGEIE] =               CSR_OP_RW(hmode, hgeie),
    [CSR_HTVAL] =               CSR_OP_RW(hmode, htval),
    [CSR_HTINST] =              CSR_OP_RW(hmode, htinst),
    [CSR_HGEIP] =               CSR_OP_RW(hmode, hgeip),
    [CSR_HGATP] =               CSR_OP_RW(hmode, hgatp),
    [CSR_HTIMEDELTA] =          CSR_OP_RW(hmode, htimedelta),
    [CSR_HTIMEDELTAH] =         CSR_OP_RW(hmode32, htimedeltah),

    [CSR_VSSTATUS] =            CSR_OP_RW(hmode, vsstatus),
    [CSR_VSIP] =                CSR_OP_RMW(hmode, vsip),
    [CSR_VSIE] =                CSR_OP_RW(hmode, vsie),
    [CSR_VSSCRATCH] =           CSR_OP_RW(hmode, vsscratch),
    [CSR_VSCAUSE] =             CSR_OP_RW(hmode, vscause),
    [CSR_VSTVAL] =              CSR_OP_RW(hmode, vstval),
    [CSR_VSATP] =               CSR_OP_RW(hmode, vsatp),

    [CSR_MTVAL2] =              CSR_OP_RW(hmode, mtval2),
    [CSR_MTINST] =              CSR_OP_RW(hmode, mtinst),

    /* Physical Memory Protection */
    [CSR_MSECCFG]    = { "mseccfg",   any, read_mseccfg, write_mseccfg },
    [CSR_PMPCFG0]    = { "pmpcfg0",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG1]    = { "pmpcfg1",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG2]    = { "pmpcfg2",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPCFG3]    = { "pmpcfg3",   pmp, read_pmpcfg,  write_pmpcfg  },
    [CSR_PMPADDR0]   = { "pmpaddr0",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR1]   = { "pmpaddr1",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR2]   = { "pmpaddr2",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR3]   = { "pmpaddr3",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR4]   = { "pmpaddr4",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR5]   = { "pmpaddr5",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR6]   = { "pmpaddr6",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR7]   = { "pmpaddr7",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR8]   = { "pmpaddr8",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR9]   = { "pmpaddr9",  pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR10]  = { "pmpaddr10", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR11]  = { "pmpaddr11", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR12]  = { "pmpaddr12", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR13]  = { "pmpaddr13", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR14] =  { "pmpaddr14", pmp, read_pmpaddr, write_pmpaddr },
    [CSR_PMPADDR15] =  { "pmpaddr15", pmp, read_pmpaddr, write_pmpaddr },

    /* Performance Counters */
    [CSR_HPMCOUNTER3   ... CSR_HPMCOUNTER31] =    CSR_OP_FN_R(ctr, read_zero, "hpmcounterN"),
    [CSR_MHPMCOUNTER3  ... CSR_MHPMCOUNTER31] =   CSR_OP_FN_R(any, read_zero, "mhpmcounterN"),
    [CSR_MHPMEVENT3    ... CSR_MHPMEVENT31] =     CSR_OP_FN_R(any, read_zero, "mhpmeventN"),
    [CSR_HPMCOUNTER3H  ... CSR_HPMCOUNTER31H] =   CSR_OP_FN_R(ctr32, read_zero, "hpmcounterNh"),
    [CSR_MHPMCOUNTER3H ... CSR_MHPMCOUNTER31H] =  CSR_OP_FN_R(any32, read_zero, "mhpmcounterNh"),

#if !defined(TARGET_CHERI)
    [CSR_MSCRATCH] =            CSR_OP_RW(any, mscratch),
    [CSR_MTVEC] =               CSR_OP_RW(any, mtvec),
    [CSR_STVEC] =               CSR_OP_RW(smode, stvec),
    [CSR_MEPC] =                CSR_OP_RW(any, mepc),
    [CSR_SEPC] =                CSR_OP_RW(smode, sepc),
    [CSR_SSCRATCH] =            CSR_OP_RW(smode, sscratch),
    [CSR_VSEPC] =               CSR_OP_RW(hmode, vsepc),
    [CSR_VSTVEC] =              CSR_OP_RW(hmode, vstvec),
#endif /* !TARGET_CHERI */ 
#endif /* !CONFIG_USER_ONLY */
};

#ifdef TARGET_CHERI
// We don't have as many CSR CAP Ops, and haven't fully defined what we need in the table, so don't bother 
// with macros for this

riscv_csr_cap_ops csr_cap_ops[] = {
    { "mscratchc", CSR_MSCRATCHC, read_capcsr_reg, write_cap_csr_reg, false,
      false },
    { "mtvecc", CSR_MTVECC, read_capcsr_reg, write_xtvecc, false, true },
    { "stvecc", CSR_STVECC, read_capcsr_reg, write_xtvecc, false, true },
    { "mepcc", CSR_MEPCC, read_xepcc, write_xepcc, false, true },
    { "sepcc", CSR_SEPCC, read_xepcc, write_xepcc, false, true },
    { "sscratchc", CSR_SSCRATCHC, read_capcsr_reg, write_cap_csr_reg, false,
      false },
    { "dscratch0c", CSR_DSCRATCH0C, read_capcsr_reg, write_cap_csr_reg, false,
      false },
    { "dscratch1c", CSR_DSCRATCH1C, read_capcsr_reg, write_cap_csr_reg, false,
      false },
    { "dpcc", CSR_DPCC, read_capcsr_reg, write_cap_csr_reg, false, true },
    { "dddc", CSR_DDDC, read_capcsr_reg, write_cap_csr_reg, true, true },
    { "jvtc", CSR_JVTC, read_capcsr_reg, write_cap_csr_reg, false, true },
    { "dinf", CSR_DINFC, read_dinfc, write_dinfc, false, false },
    { "mtdc", CSR_MTDC, read_capcsr_reg, write_cap_csr_reg, true, false },
    { "stdc", CSR_STDC, read_capcsr_reg, write_cap_csr_reg, true, false },
    { "ddc", CSR_DDC, read_capcsr_reg, write_cap_csr_reg, true, true },
    { "mtidc", CSR_MTIDC, read_capcsr_reg, write_cap_csr_reg, false, false },
    { "stidc", CSR_STIDC, read_capcsr_reg, write_cap_csr_reg, false, false },
    { "utidc", CSR_UTIDC, read_capcsr_reg, write_cap_csr_reg, false, false },
};

riscv_csr_cap_ops* get_csr_cap_info(int csrnum){
    switch (csrnum){
        case CSR_MSCRATCHC: return &csr_cap_ops[0];
        case CSR_MTVECC: return &csr_cap_ops[1];
        case CSR_STVECC: return &csr_cap_ops[2];
        case CSR_MEPCC: return &csr_cap_ops[3];
        case CSR_SEPCC: return &csr_cap_ops[4];
        case CSR_SSCRATCHC: return &csr_cap_ops[5];
        case CSR_DSCRATCH0C: return &csr_cap_ops[6];
        case CSR_DSCRATCH1C: return &csr_cap_ops[7];
        case CSR_DPCC: return &csr_cap_ops[8];
        case CSR_DDDC: return &csr_cap_ops[9];
        case CSR_JVTC: return &csr_cap_ops[10];
        case CSR_DINFC: return &csr_cap_ops[11];
        case CSR_MTDC: return &csr_cap_ops[12];
        case CSR_STDC: return &csr_cap_ops[13];
        case CSR_DDC: return &csr_cap_ops[14];
        case CSR_MTIDC: return &csr_cap_ops[15];
        case CSR_STIDC: return &csr_cap_ops[16];
        case CSR_UTIDC: return &csr_cap_ops[17];
        default: return NULL;
    }
}
#endif


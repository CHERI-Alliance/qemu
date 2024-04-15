/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2020 Alex Richardson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory (Department of Computer Science and
 * Technology) under DARPA contract HR0011-18-C-0016 ("ECATS"), as part of the
 * DARPA SSITH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "exec/exec-all.h"
#include "exec/log.h"
#include "exec/helper-proto.h"
#include "cheri-helper-utils.h"
#include "cheri_tagmem.h"
#ifndef TARGET_CHERI
#error TARGET_CHERI must be set
#endif

enum SCRAccessMode {
    SCR_Invalid = 0,
    ASR_Flag = 1,
    U_Always = (PRV_U + 1) << 1,
    U_ASR = U_Always | ASR_Flag,
    S_Always = (PRV_S + 1) << 1,
    S_ASR = S_Always | ASR_Flag,
    H_Always = (PRV_H + 1) << 1,
    H_ASR = H_Always | ASR_Flag,
    M_Always = (PRV_M + 1) << 1,
    M_ASR = M_Always | ASR_Flag,
};

static inline int scr_min_priv(enum SCRAccessMode mode)
{
    return ((int)mode >> 1) - 1;
}
static inline int scr_needs_asr(enum SCRAccessMode mode)
{
    return (mode & ASR_Flag) == ASR_Flag;
}

struct SCRInfo {
    bool r;
    bool w;
    enum SCRAccessMode access; /* Default = Invalid */
    const char *name;
    //#define PRV_U 0
    //#define PRV_S 1
    //#define PRV_H 2 /* Reserved */
    //#define PRV_M 3
} scr_info[CheriSCR_MAX] = {
    [CheriSCR_PCC] = {.r = true, .w = false, .access = U_Always, .name = "PCC"},
    [CheriSCR_DDC] = {.r = true, .w = true, .access = U_Always, .name = "DDC"},

    [CheriSCR_STDC] = {.r = true, .w = true, .access = S_ASR, .name = "STDC"},
    
    [CheriSCR_MTDC] = {.r = true, .w = true, .access = M_ASR, .name = "MTDC"},

    [CheriSCR_BSTCC] = {.r = true, .w = true, .access = H_ASR, .name= "BSTCC"},
    [CheriSCR_BSTDC] = {.r = true, .w = true, .access = H_ASR, .name= "BSTCC"},
    [CheriSCR_BSScratchC] = {.r = true, .w = true, .access = H_ASR,
                             .name= "BSTCC"},
    [CheriSCR_BSEPCC] = {.r = true, .w = true, .access = H_ASR, .name= "BSTCC"},
};

static inline cap_register_t *get_scr(CPUArchState *env, uint32_t index)
{
    switch (index) {
    case CheriSCR_PCC: return &env->PCC;
    case CheriSCR_DDC: return &env->DDC;

    case CheriSCR_STDC: return &env->STDC_CAP;

    case CheriSCR_MTDC: return &env->MTDC;

    case CheriSCR_BSTCC: return &env->VSTCC;
    case CheriSCR_BSTDC: return &env->VSTDC;
    case CheriSCR_BSScratchC: return &env->VSScratchC;
    case CheriSCR_BSEPCC: return &env->VSEPCC;
    default: assert(false && "Should have raised an invalid inst trap!");
    }
}

#ifdef CONFIG_TCG_LOG_INSTR
void riscv_log_instr_scr_changed(CPURISCVState *env, int scrno)
{
    if (qemu_log_instr_enabled(env)) {
        qemu_log_instr_cap(env, scr_info[scrno].name, get_scr(env, scrno));
    }
}
#endif


int check_csr_cap_permissions(CPURISCVState *env, int csrno,int write_mask);
int check_csr_cap_permissions(CPURISCVState *env, int csrno,int write_mask)
{
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
        return -RISCV_EXCP_ILLEGAL_INST;
    }
#endif

    /* ensure the CSR extension is enabled. */
    if (!cpu->cfg.ext_icsr) {
        return -RISCV_EXCP_ILLEGAL_INST;
    }

    
    return 0;
}


void HELPER(csrrw_cap)(CPUArchState *env, uint32_t csr, uint32_t rd,
                        uint32_t rs1)
{
    cap_register_t rs_cap;
    int ret;
    riscv_csr_cap_ops *csr_cap_info = get_csr_cap_info(csr);
    cap_register_t csr_cap;

    assert(csr_cap_info);

    ret = check_csr_cap_permissions(env,csr,1);
    if (ret) {
        riscv_raise_exception(env, RISCV_EXCP_CHERI, GETPC());
    }

    rs_cap = *get_cap_in_gpregs(&env->gpcapregs,rs1);
    if (rd) {
        csr_cap = csr_cap_info->read(env);
        cap_register_t *rd_cap = get_cap_in_gpregs(&env->gpcapregs,rd);
        *rd_cap = csr_cap;
        cheri_log_instr_changed_gp_capreg(env, rd, rd_cap);
    }

    cheri_log_instr_changed_capreg(env, csr_cap_info->name, &rs_cap);
    csr_cap_info->write(env, &rs_cap);
}

void HELPER(csrrs_cap)(CPUArchState *env, uint32_t csr, uint32_t rd,
                        uint32_t rs1)
{
    cap_register_t rs_cap;
    int ret;
    riscv_csr_cap_ops *csr_cap_info = get_csr_cap_info(csr);
    cap_register_t csr_cap;


    assert(csr_cap_info);

    if (rs1) {
        ret = check_csr_cap_permissions(env,csr,1);
        if (ret) {
            riscv_raise_exception(env, RISCV_EXCP_CHERI, GETPC());
        }
        rs_cap = *get_cap_in_gpregs(&env->gpcapregs,rs1);
    }

    csr_cap = csr_cap_info->read(env);
    cap_register_t *dest = get_cap_in_gpregs(&env->gpcapregs,rd);
    *dest = csr_cap;
    cheri_log_instr_changed_gp_capreg(env, rd, &csr_cap);

    if (rs1){
        target_ulong new_val;
        new_val = cap_get_cursor(&csr_cap) | cap_get_cursor(&rs_cap);   
        update_special_register(env, &rs_cap, csr_cap_info->name, new_val);
        csr_cap_info->write(env, &rs_cap);
    }

}

void HELPER(csrrc_cap)(CPUArchState *env, uint32_t csr, uint32_t rd,
                        uint32_t rs1)
{
    cap_register_t rs_cap;
    int ret;
    riscv_csr_cap_ops *csr_cap_info = get_csr_cap_info(csr);
    cap_register_t csr_cap;

    assert(csr_cap_info);

    if(rs1) {
        ret = check_csr_cap_permissions(env,csr,1);
        if (ret) {
            riscv_raise_exception(env, RISCV_EXCP_CHERI, GETPC());
        }
        rs_cap = *get_cap_in_gpregs(&env->gpcapregs,rs1);
    }

    csr_cap = csr_cap_info->read(env);
    cap_register_t *dest = get_cap_in_gpregs(&env->gpcapregs,rd);
    *dest = csr_cap;
    cheri_log_instr_changed_gp_capreg(env, rd, &csr_cap);

    if (rs1) {
        target_ulong addr;
        addr = cap_get_cursor(&csr_cap) & ( ~cap_get_cursor(&rs_cap) );
        update_special_register(env, &rs_cap, csr_cap_info->name, addr);
        csr_cap_info->write(env, &rs_cap);
    }
}

void HELPER(csrrwi_cap)(CPUArchState *env, uint32_t csr, uint32_t rd,
                        uint32_t rs1)
{
    cap_register_t tmp_cap;
    int ret;
    riscv_csr_cap_ops *csr_cap_info = get_csr_cap_info(csr);
    cap_register_t csr_cap;

    assert(csr_cap_info);

    ret = check_csr_cap_permissions(env,csr,1);

    if (ret) {
        riscv_raise_exception(env, RISCV_EXCP_CHERI, GETPC());
    }

    csr_cap = csr_cap_info->read(env);
    if (rd) {
        cap_register_t *dest = get_cap_in_gpregs(&env->gpcapregs,rd);
        *dest = csr_cap;
        cheri_log_instr_changed_gp_capreg(env, rd, &csr_cap);
    }
    tmp_cap = csr_cap;
    update_special_register(env, &tmp_cap, csr_cap_info->name, rs1);
    csr_cap_info->write(env, &tmp_cap);
}

void HELPER(csrrsi_cap)(CPUArchState *env, uint32_t csr, uint32_t rd,
                        uint32_t rs1_val)
{
    int ret;
    riscv_csr_cap_ops *csr_cap_info = get_csr_cap_info(csr);
    cap_register_t csr_cap;

    assert(csr_cap_info);

    if (rs1_val) {
        ret = check_csr_cap_permissions(env,csr,1);
        if (ret) {
            riscv_raise_exception(env, RISCV_EXCP_CHERI, GETPC());
        }
    }

    csr_cap = csr_cap_info->read(env);
    if (rd) {
        cap_register_t *rd_cap = get_cap_in_gpregs(&env->gpcapregs,rd);
        *rd_cap = csr_cap;
        cheri_log_instr_changed_gp_capreg(env, rd, &csr_cap);
    }

    if (rs1_val) {
        target_ulong new_val;
        new_val = cap_get_cursor(&csr_cap ) | rs1_val;
        update_special_register(env, &csr_cap, csr_cap_info->name, new_val);
        csr_cap_info->write(env, &csr_cap);
    }
}

void HELPER(csrrci_cap)(CPUArchState *env, uint32_t csr, uint32_t rd,
                        uint32_t rs1_val)
{
    int ret;
    riscv_csr_cap_ops *csr_cap_info = get_csr_cap_info(csr);
    cap_register_t csr_cap;

    assert(csr_cap_info);

    if (rs1_val) {
        ret = check_csr_cap_permissions(env,csr,1);
        if(ret){
            riscv_raise_exception(env, RISCV_EXCP_CHERI, GETPC());
        }
    }

    csr_cap = csr_cap_info->read(env);
    if (rd) {
        cap_register_t *rd_cap = get_cap_in_gpregs(&env->gpcapregs,rd);
        *rd_cap=csr_cap;
        cheri_log_instr_changed_gp_capreg(env, rd, &csr_cap);
    }


    if (rs1_val) {
        target_ulong new_val;
        new_val = cap_get_cursor(&csr_cap) & (~rs1_val);
        update_special_register(env, &csr_cap, csr_cap_info->name, new_val);
        csr_cap_info->write(env, &csr_cap);
    }


}


void HELPER(cspecialrw)(CPUArchState *env, uint32_t cd, uint32_t cs,
                        uint32_t index)
{
    uintptr_t _host_return_address = GETPC();
    // Ensure that env->PCC.cursor is correct:
    cpu_restore_state(env_cpu(env), _host_return_address, false);

    assert(index <= 31 && "Bug in translator?");
    enum SCRAccessMode mode = scr_info[index].access;
    if (mode == SCR_Invalid || (cs != 0 && !scr_info[index].w)) {
        riscv_raise_exception(env, RISCV_EXCP_ILLEGAL_INST,
                              _host_return_address);
    }
    bool can_access_sysregs = cheri_have_access_sysregs(env);
    if (scr_needs_asr(mode) && !can_access_sysregs) {
        raise_cheri_exception(env, CapEx_AccessSystemRegsViolation, 32 + index);
    }
    if (scr_min_priv(mode) > env->priv) {
        raise_cheri_exception(env, CapEx_AccessSystemRegsViolation, 32 + index);
    }
    cap_register_t *scr = get_scr(env, index);
    // Make a copy of the write value in case cd == cs
    cap_register_t new_val = *get_readonly_capreg(env, cs);
    if (cd != 0) {
        assert(scr_info[index].r && "Bug? Should be readable");
        // For xEPCC we clear the low address bit(s) when reading to match xEPC.
        // See helper_sret/helper_mret for more context.
    }
    if (cs != 0) {
        assert(scr_info[index].w && "Bug? Should be writable");
#ifdef CONFIG_TCG_LOG_INSTR
        if (qemu_log_instr_enabled(env)) {
            qemu_log_instr_extra(env, "  %s <- " PRINT_CAP_FMTSTR "\n",
                scr_info[index].name, PRINT_CAP_ARGS(&new_val));
        }
#endif
        switch (index) {
        case CheriSCR_DDC:
            if (!new_val.cr_tag) {
                qemu_log_instr_or_mask_msg(
                    env, CPU_LOG_INT,
                    "Note: Installed untagged DDC at " TARGET_FMT_lx "\n",
                    cpu_get_recent_pc(env));
            }
            /* fallthrough */
        default:
            *scr = new_val;
            cheri_log_instr_changed_capreg(env, scr_info[index].name, scr);
        }
    }
}

#ifdef DO_CHERI_STATISTICS
static DEFINE_CHERI_STAT(auipcc);
#endif

void HELPER(auipcc)(CPUArchState *env, uint32_t cd, target_ulong new_cursor)
{
    derive_cap_from_pcc(env, cd, new_cursor, GETPC(), OOB_INFO(auipcc));
}

void HELPER(cjal)(CPUArchState *env, uint32_t cd, target_ulong target_addr,
                  target_ulong link_addr)
{
    cheri_jump_and_link_checked(env, cd, link_addr, CHERI_EXC_REGNUM_PCC,
                                cheri_get_recent_pcc(env), target_addr,
                                0, GETPC());
}

void HELPER(amoswap_cap)(CPUArchState *env, uint32_t dest_reg,
                         uint32_t addr_reg, uint32_t val_reg)
{
    uintptr_t _host_return_address = GETPC();
    assert(!qemu_tcg_mttcg_enabled() ||
           (cpu_in_exclusive_context(env_cpu(env)) &&
            "Should have raised EXCP_ATOMIC"));
    target_long addr = get_capreg_cursor(env, addr_reg);
    if (!cheri_in_capmode(env)) {
        addr = cheri_ddc_relative_addr(env, addr);
        addr_reg = CHERI_EXC_REGNUM_DDC;
    }
    const cap_register_t *cbp = get_load_store_base_cap(env, addr_reg);

    if (!cbp->cr_tag) {
        raise_cheri_exception(env, CapEx_TagViolation, addr_reg);
    } else if (!cap_is_unsealed(cbp)) {
        raise_cheri_exception(env, CapEx_SealViolation, addr_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_LOAD)) {
        raise_cheri_exception(env, CapEx_PermitLoadViolation, addr_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_STORE)) {
        raise_cheri_exception(env, CapEx_PermitStoreViolation, addr_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_STORE_CAP)) {
        raise_cheri_exception(env, CapEx_PermitStoreCapViolation, addr_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_STORE_LOCAL) &&
               get_capreg_tag(env, val_reg) &&
               !(get_capreg_hwperms(env, val_reg) & CAP_PERM_GLOBAL)) {
        raise_cheri_exception(env, CapEx_PermitStoreLocalCapViolation, val_reg);
    }

    if (!cap_is_in_bounds(cbp, addr, CHERI_CAP_SIZE)) {
        qemu_log_instr_or_mask_msg(
            env, CPU_LOG_INT,
            "Failed capability bounds check: addr=" TARGET_FMT_ld
            " base=" TARGET_FMT_lx " top=" TARGET_FMT_lx "\n",
            addr, cap_get_cursor(cbp), cap_get_top(cbp));
        raise_cheri_exception(env, CapEx_LengthViolation, addr_reg);
    } else if (!QEMU_IS_ALIGNED(addr, CHERI_CAP_SIZE)) {
        raise_unaligned_store_exception(env, addr, _host_return_address);
    }
    if (addr == env->load_res) {
        env->load_res = -1; // Invalidate LR/SC to the same address
    }
    // Load the value to store from the register file now in case the
    // load_cap_from_memory call overwrites that register
    target_ulong loaded_pesbt;
    target_ulong loaded_cursor;
    bool loaded_tag =
        load_cap_from_memory_raw(env, &loaded_pesbt, &loaded_cursor, addr_reg,
                                 cbp, addr, _host_return_address, NULL);
    // The store may still trap, so we must only update the dest register after
    // the store succeeded.
    store_cap_to_memory(env, val_reg, addr, _host_return_address);
    // Store succeeded -> we can update cd
    update_compressed_capreg(env, dest_reg, loaded_pesbt, loaded_tag,
                             loaded_cursor);
}

static void lr_c_impl(CPUArchState *env, uint32_t dest_reg, uint32_t auth_reg,
                      target_ulong addr, uintptr_t _host_return_address)
{
    assert(!qemu_tcg_mttcg_enabled() ||
           (cpu_in_exclusive_context(env_cpu(env)) &&
            "Should have raised EXCP_ATOMIC"));
    const cap_register_t *cbp = get_load_store_base_cap(env, auth_reg);
    if (!cbp->cr_tag) {
        raise_cheri_exception(env, CapEx_TagViolation, auth_reg);
    } else if (!cap_is_unsealed(cbp)) {
        raise_cheri_exception(env, CapEx_SealViolation, auth_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_LOAD)) {
        raise_cheri_exception(env, CapEx_PermitLoadViolation, auth_reg);
    }

    if (!cap_is_in_bounds(cbp, addr, CHERI_CAP_SIZE)) {
        qemu_log_instr_or_mask_msg(
            env, CPU_LOG_INT,
            "Failed capability bounds check: addr=" TARGET_FMT_ld
            " base=" TARGET_FMT_lx " top=" TARGET_FMT_lx "\n",
            addr, cap_get_cursor(cbp), cap_get_top(cbp));
        raise_cheri_exception(env, CapEx_LengthViolation, auth_reg);
    } else if (!QEMU_IS_ALIGNED(addr, CHERI_CAP_SIZE)) {
        raise_unaligned_store_exception(env, addr, _host_return_address);
    }
    target_ulong pesbt;
    target_ulong cursor;
    bool tag = load_cap_from_memory_raw(env, &pesbt, &cursor, auth_reg, cbp,
                                        addr, _host_return_address, NULL);
    // If this didn't trap, update the lr state:
    env->load_res = addr;
    env->load_val = cursor;
    env->load_pesbt = pesbt;
    env->load_tag = tag;
    log_changed_special_reg(env, "load_res", env->load_res);
    log_changed_special_reg(env, "load_val", env->load_val);
    log_changed_special_reg(env, "load_pesbt", env->load_pesbt);
    log_changed_special_reg(env, "load_tag", (target_ulong)env->load_tag);
    update_compressed_capreg(env, dest_reg, pesbt, tag, cursor);
}

void HELPER(lr_c_modedep)(CPUArchState *env, uint32_t dest_reg, uint32_t addr_reg)
{
    target_ulong addr = get_capreg_cursor(env, addr_reg);
    if (!cheri_in_capmode(env)) {
        addr = cheri_ddc_relative_addr(env, addr);
        addr_reg = CHERI_EXC_REGNUM_DDC;
    }
    lr_c_impl(env, dest_reg, addr_reg, addr, GETPC());
}

void HELPER(lr_c_ddc)(CPUArchState *env, uint32_t dest_reg, uint32_t addr_reg)
{
    target_ulong addr =
        cheri_ddc_relative_addr(env, get_capreg_cursor(env, addr_reg));
    lr_c_impl(env, dest_reg, CHERI_EXC_REGNUM_DDC, addr, GETPC());
}

void HELPER(lr_c_cap)(CPUArchState *env, uint32_t dest_reg, uint32_t addr_reg)
{
    target_ulong addr = get_capreg_cursor(env, addr_reg);
    lr_c_impl(env, dest_reg, addr_reg, addr, GETPC());
}

// SC returns zero on success, one on failure
static target_ulong sc_c_impl(CPUArchState *env, uint32_t addr_reg,
                              uint32_t val_reg, target_ulong addr,
                              uintptr_t _host_return_address)
{
    assert(!qemu_tcg_mttcg_enabled() ||
        (cpu_in_exclusive_context(env_cpu(env)) &&
            "Should have raised EXCP_ATOMIC"));
    const cap_register_t *cbp = get_load_store_base_cap(env, addr_reg);

    if (!cbp->cr_tag) {
        raise_cheri_exception(env, CapEx_TagViolation, addr_reg);
    } else if (!cap_is_unsealed(cbp)) {
        raise_cheri_exception(env, CapEx_SealViolation, addr_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_STORE)) {
        raise_cheri_exception(env, CapEx_PermitStoreViolation, addr_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_STORE_CAP)) {
        raise_cheri_exception(env, CapEx_PermitStoreCapViolation, addr_reg);
    } else if (!cap_has_perms(cbp, CAP_PERM_STORE_LOCAL) &&
               get_capreg_tag(env, val_reg) &&
               !(get_capreg_hwperms(env, val_reg) & CAP_PERM_GLOBAL)) {
        raise_cheri_exception(env, CapEx_PermitStoreLocalCapViolation, val_reg);
    }

    if (!cap_is_in_bounds(cbp, addr, CHERI_CAP_SIZE)) {
        qemu_log_instr_or_mask_msg(
            env, CPU_LOG_INT,
            "Failed capability bounds check: addr=" TARGET_FMT_ld
            " base=" TARGET_FMT_lx " top=" TARGET_FMT_lx "\n",
            addr, cap_get_cursor(cbp), cap_get_top(cbp));
        raise_cheri_exception(env, CapEx_LengthViolation, addr_reg);
    } else if (!QEMU_IS_ALIGNED(addr, CHERI_CAP_SIZE)) {
        raise_unaligned_store_exception(env, addr, _host_return_address);
    }

    // Save the expected addr
    const target_ulong expected_addr = env->load_res;
    // Clear the load reservation, since an SC must fail if there is
    // an SC to any address, in between an LR and SC pair.
    // We do this regardless of success/failure.
    env->load_res = -1;
    log_changed_special_reg(env, "load_res", env->load_res);
    if (addr != expected_addr) {
        goto sc_failed;
    }
    // Now perform the "cmpxchg" operation by checking if the current values
    // in memory are the same as the ones that the load-reserved observed.
    // FIXME: There is a bug here. If the MMU / Cap Permissions squash the tag,
    // we may think the location has changed when it has not.
    // Use load_cap_from_memory_128_raw_tag to get the real tag, and strip the
    // LOAD_CAP permission to ensure no MMU load faults occur
    // (this is not a real load).
    target_ulong current_pesbt;
    target_ulong current_cursor;
#ifdef CONFIG_RVFI_DII
    /* The read that is part of the cmpxchg should not be visible in traces. */
    uint32_t old_rmask = env->rvfi_dii_trace.MEM.rvfi_mem_rmask;
#endif
    bool current_tag =
        load_cap_from_memory_raw(env, &current_pesbt, &current_cursor, addr_reg,
                                 cbp, addr, _host_return_address, NULL);
#ifdef CONFIG_RVFI_DII
    /* The read that is part of the cmpxchg should not be visible in traces. */
    env->rvfi_dii_trace.MEM.rvfi_mem_rmask = old_rmask;
#endif
    if (current_cursor != env->load_val || current_pesbt != env->load_pesbt ||
        current_tag != env->load_tag) {
        goto sc_failed;
    }
    // This store may still trap, so we should update env->load_res before
    store_cap_to_memory(env, val_reg, addr, _host_return_address);
    tcg_debug_assert(env->load_res == -1);
    return 0; // success
sc_failed:
    tcg_debug_assert(env->load_res == -1);
    return 1; // failure
}

target_ulong HELPER(sc_c_modedep)(CPUArchState *env, uint32_t addr_reg,
                                  uint32_t val_reg)
{
    target_ulong addr = get_capreg_cursor(env, addr_reg);
    if (!cheri_in_capmode(env)) {
        addr = cheri_ddc_relative_addr(env, addr);
        addr_reg = CHERI_EXC_REGNUM_DDC;
    }
    return sc_c_impl(env, addr_reg, val_reg, addr, GETPC());
}

target_ulong HELPER(sc_c_ddc)(CPUArchState *env, uint32_t addr_reg,
                              uint32_t val_reg)
{
    target_ulong addr =
        cheri_ddc_relative_addr(env, get_capreg_cursor(env, addr_reg));
    return sc_c_impl(env, CHERI_EXC_REGNUM_DDC, val_reg, addr, GETPC());
}

target_ulong HELPER(sc_c_cap)(CPUArchState *env, uint32_t addr_reg,
                              uint32_t val_reg)
{
    target_ulong addr = get_capreg_cursor(env, addr_reg);
    return sc_c_impl(env, addr_reg, val_reg, addr, GETPC());
}

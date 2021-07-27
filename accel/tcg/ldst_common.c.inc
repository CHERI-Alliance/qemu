/*
 * Routines common to user and system emulation of load/store.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

uint32_t cpu_ldub_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                            int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);
    return cpu_ldb_mmu(env, addr, oi, ra);
}

int cpu_ldsb_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                       int mmu_idx, uintptr_t ra)
{
    return (int8_t)cpu_ldub_mmuidx_ra(env, addr, mmu_idx, ra);
}

uint32_t cpu_lduw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUW | MO_UNALN, mmu_idx);
    return cpu_ldw_be_mmu(env, addr, oi, ra);
}

int cpu_ldsw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_lduw_be_mmuidx_ra(env, addr, mmu_idx, ra);
}

uint32_t cpu_ldl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUL | MO_UNALN, mmu_idx);
    return cpu_ldl_be_mmu(env, addr, oi, ra);
}

uint64_t cpu_ldq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEQ | MO_UNALN, mmu_idx);
    return cpu_ldq_be_mmu(env, addr, oi, ra);
}

uint32_t cpu_lduw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                               int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUW | MO_UNALN, mmu_idx);
    return cpu_ldw_le_mmu(env, addr, oi, ra);
}

int cpu_ldsw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                          int mmu_idx, uintptr_t ra)
{
    return (int16_t)cpu_lduw_le_mmuidx_ra(env, addr, mmu_idx, ra);
}

uint32_t cpu_ldl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUL | MO_UNALN, mmu_idx);
    return cpu_ldl_le_mmu(env, addr, oi, ra);
}

uint64_t cpu_ldq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr,
                              int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEQ | MO_UNALN, mmu_idx);
    return cpu_ldq_le_mmu(env, addr, oi, ra);
}

void cpu_stb_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                       int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_UB, mmu_idx);
    cpu_stb_mmu(env, addr, val, oi, ra);
}

void cpu_stw_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUW | MO_UNALN, mmu_idx);
    cpu_stw_be_mmu(env, addr, val, oi, ra);
}

void cpu_stl_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEUL | MO_UNALN, mmu_idx);
    cpu_stl_be_mmu(env, addr, val, oi, ra);
}

void cpu_stq_be_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_BEQ | MO_UNALN, mmu_idx);
    cpu_stq_be_mmu(env, addr, val, oi, ra);
}

void cpu_stw_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUW | MO_UNALN, mmu_idx);
    cpu_stw_le_mmu(env, addr, val, oi, ra);
}

void cpu_stl_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint32_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEUL | MO_UNALN, mmu_idx);
    cpu_stl_le_mmu(env, addr, val, oi, ra);
}

void cpu_stq_le_mmuidx_ra(CPUArchState *env, abi_ptr addr, uint64_t val,
                          int mmu_idx, uintptr_t ra)
{
    MemOpIdx oi = make_memop_idx(MO_LEQ | MO_UNALN, mmu_idx);
    cpu_stq_le_mmu(env, addr, val, oi, ra);
}

/*--------------------------*/

uint32_t cpu_ldub_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldub_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

int cpu_ldsb_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int8_t)cpu_ldub_data_ra(env, addr, ra);
}

uint32_t cpu_lduw_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_lduw_be_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

int cpu_ldsw_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int16_t)cpu_lduw_be_data_ra(env, addr, ra);
}

uint32_t cpu_ldl_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldl_be_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

uint64_t cpu_ldq_be_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldq_be_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

uint32_t cpu_lduw_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_lduw_le_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

int cpu_ldsw_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return (int16_t)cpu_lduw_le_data_ra(env, addr, ra);
}

uint32_t cpu_ldl_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldl_le_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

uint64_t cpu_ldq_le_data_ra(CPUArchState *env, abi_ptr addr, uintptr_t ra)
{
    return cpu_ldq_le_mmuidx_ra(env, addr, cpu_mmu_index(env, false), ra);
}

#ifdef TARGET_CHERI
/*
 * TODO(am2419): Ugly hack to avoid logging memory accesses that load capability
 * components as normal memory accesses. The caller is responsible for logging.
 */
target_ulong cpu_ld_cap_word_ra(CPUArchState *env, target_ulong ptr,
                                uintptr_t retaddr)
{
#if TARGET_LONG_BITS == 32
#elif TARGET_LONG_BITS == 64
#else
#error "Unhandled target long width"
#endif
    return cpu_load_helper_no_log(
        env, ptr, make_memop_idx(MO_TEQ, cpu_mmu_index(env, false)), retaddr,
        MO_TE == MO_LE ? helper_le_ldq_mmu : helper_be_ldq_mmu);
}
#endif

void cpu_stb_data_ra(CPUArchState *env, abi_ptr addr,
                     uint32_t val, uintptr_t ra)
{
    cpu_stb_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stw_be_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stw_be_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stl_be_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stl_be_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stq_be_data_ra(CPUArchState *env, abi_ptr addr,
                        uint64_t val, uintptr_t ra)
{
    cpu_stq_be_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stw_le_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stw_le_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stl_le_data_ra(CPUArchState *env, abi_ptr addr,
                        uint32_t val, uintptr_t ra)
{
    cpu_stl_le_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

void cpu_stq_le_data_ra(CPUArchState *env, abi_ptr addr,
                        uint64_t val, uintptr_t ra)
{
    cpu_stq_le_mmuidx_ra(env, addr, val, cpu_mmu_index(env, false), ra);
}

#ifdef TARGET_CHERI
/*
 * TODO(am2419): Ugly hack to avoid logging memory accesses that store capability
 * components as normal memory accesses. The caller is responsible for logging.
 */
void cpu_st_cap_word_ra(CPUArchState *env, target_ulong ptr,
                        target_ulong val, uintptr_t retaddr)
{
    FullStoreHelper *fst;
    MemOp op;
#if TARGET_LONG_BITS == 32
    op = MO_TEUW;
    if (MO_TEUW == MO_LEUW)
         fst = full_le_stw_mmu;
    else
        fst = full_be_stw_mmu;
#elif TARGET_LONG_BITS == 64
    /*
     * For 64-bit values, there's no difference between helper functions (that
     * use the value's native size) and "full functions" (that operate on
     * 64-bit values).
     */
    op = MO_TEQ;
    if (MO_TEQ == MO_LEQ)
         fst = helper_le_stq_mmu;
    else
         fst = helper_be_stq_mmu;
#else
#error "Unhandled target long width"
#endif

    cpu_store_helper_no_log(env, ptr, val,
            make_memop_idx(op, cpu_mmu_index(env, false)), retaddr, fst);
}
#endif

/*--------------------------*/

uint32_t cpu_ldub_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldub_data_ra(env, addr, 0);
}

int cpu_ldsb_data(CPUArchState *env, abi_ptr addr)
{
    return (int8_t)cpu_ldub_data(env, addr);
}

uint32_t cpu_lduw_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_lduw_be_data_ra(env, addr, 0);
}

int cpu_ldsw_be_data(CPUArchState *env, abi_ptr addr)
{
    return (int16_t)cpu_lduw_be_data(env, addr);
}

uint32_t cpu_ldl_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldl_be_data_ra(env, addr, 0);
}

uint64_t cpu_ldq_be_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldq_be_data_ra(env, addr, 0);
}

uint32_t cpu_lduw_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_lduw_le_data_ra(env, addr, 0);
}

int cpu_ldsw_le_data(CPUArchState *env, abi_ptr addr)
{
    return (int16_t)cpu_lduw_le_data(env, addr);
}

uint32_t cpu_ldl_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldl_le_data_ra(env, addr, 0);
}

uint64_t cpu_ldq_le_data(CPUArchState *env, abi_ptr addr)
{
    return cpu_ldq_le_data_ra(env, addr, 0);
}

void cpu_stb_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stb_data_ra(env, addr, val, 0);
}

void cpu_stw_be_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stw_be_data_ra(env, addr, val, 0);
}

void cpu_stl_be_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stl_be_data_ra(env, addr, val, 0);
}

void cpu_stq_be_data(CPUArchState *env, abi_ptr addr, uint64_t val)
{
    cpu_stq_be_data_ra(env, addr, val, 0);
}

void cpu_stw_le_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stw_le_data_ra(env, addr, val, 0);
}

void cpu_stl_le_data(CPUArchState *env, abi_ptr addr, uint32_t val)
{
    cpu_stl_le_data_ra(env, addr, val, 0);
}

void cpu_stq_le_data(CPUArchState *env, abi_ptr addr, uint64_t val)
{
    cpu_stq_le_data_ra(env, addr, val, 0);
}

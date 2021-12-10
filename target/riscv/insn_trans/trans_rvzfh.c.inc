/*
 * RISC-V translation routines for the RV64Zfh Standard Extension.
 *
 * Copyright (c) 2020 Chih-Min Chao, chihmin.chao@sifive.com
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

#define REQUIRE_ZFH(ctx) do { \
    if (!ctx->ext_zfh) {      \
        return false;         \
    }                         \
} while (0)

static bool trans_flh(DisasContext *ctx, arg_flh *a)
{
    TCGv_i64 dest;
    TCGv_cap_checked_ptr addr;
    const MemOp mop = MO_TEUW;

    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);
    addr = get_capmode_dependent_load_addr(ctx, a->rs1, a->imm, mop);
    dest = cpu_fpr[a->rd];
    tcg_gen_qemu_ld_i64_with_checked_addr(dest, addr, ctx->mem_idx, mop);
    gen_nanbox_h(dest, dest);

    tcg_temp_free_cap_checked(addr);
    mark_fs_dirty(ctx);
    return true;
}

static bool trans_fsh(DisasContext *ctx, arg_fsh *a)
{
    TCGv_cap_checked_ptr addr;
    const MemOp mop = MO_TEUW;

    REQUIRE_FPU;
    REQUIRE_ZFH(ctx);
    addr = get_capmode_dependent_store_addr(ctx, a->rs1, a->imm, mop);
    tcg_gen_qemu_st_i64_with_checked_addr(cpu_fpr[a->rs2], addr, ctx->mem_idx,
                                          mop);

    tcg_temp_free_cap_checked(addr);

    return true;
}

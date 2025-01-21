/*
 * RISC-V translation routines for the RISC-V CBO Extension.
 *
 * Copyright (c) 2021 Philipp Tomsich, philipp.tomsich@vrull.eu
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

#define REQUIRE_ZICBOM(ctx) do {                   \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_icbom) {      \
        return false;                              \
    }                                              \
} while (0)

#define REQUIRE_ZICBOZ(ctx) do {                   \
    if (!RISCV_CPU(ctx->cs)->cfg.ext_icboz) {      \
        return false;                              \
    }                                              \
} while (0)

static bool trans_cbo_clean(DisasContext *ctx, arg_cbo_clean *a)
{
    REQUIRE_ZICBOM(ctx);
    gen_helper_cbo_clean_flush(cpu_env, get_gpr(ctx, a->rs1, EXT_NONE));
    return true;
}

static bool trans_cbo_flush(DisasContext *ctx, arg_cbo_flush *a)
{
    REQUIRE_ZICBOM(ctx);
    gen_helper_cbo_clean_flush(cpu_env, get_gpr(ctx, a->rs1, EXT_NONE));
    return true;
}

static bool trans_cbo_inval(DisasContext *ctx, arg_cbo_inval *a)
{
    REQUIRE_ZICBOM(ctx);
    gen_helper_cbo_inval(cpu_env, get_gpr(ctx, a->rs1, EXT_NONE));
    return true;
}

static bool trans_cbo_zero(DisasContext *ctx, arg_cbo_zero *a)
{
    REQUIRE_ZICBOZ(ctx);
    /*
     * The register value maps to a TCGv_tl parameter (== a target_ulong in
     * the helper function). We don't have to zero-expand it. This would be
     * required only if the parameter was always 64-bit, even for RV32.
     */
    gen_helper_cbo_zero(cpu_env, get_gpr(ctx, a->rs1, EXT_NONE));
    return true;
}

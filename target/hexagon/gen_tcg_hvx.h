/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_GEN_TCG_HVX_H
#define HEXAGON_GEN_TCG_HVX_H

/*
 * Histogram instructions
 *
 * Note that these instructions operate directly on the vector registers
 * and therefore happen after commit.
 *
 * The generate_<tag> function is called twice
 *     The first time is during the normal TCG generation
 *         ctx->pre_commit is true
 *         In the masked cases, we save the mask to the qtmp temporary
 *         Otherwise, there is nothing to do
 *     The second call is at the end of gen_commit_packet
 *         ctx->pre_commit is false
 *         Generate the call to the helper
 */

static inline void assert_vhist_tmp(DisasContext *ctx)
{
    /* vhist instructions require exactly one .tmp to be defined */
    g_assert(ctx->tmp_vregs_idx == 1);
}

#define fGEN_TCG_V6_vhist(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vhist(cpu_env); \
    }
#define fGEN_TCG_V6_vhistq(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vhistq(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist256(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist256(cpu_env); \
    }
#define fGEN_TCG_V6_vwhist256q(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist256q(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist256_sat(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist256_sat(cpu_env); \
    }
#define fGEN_TCG_V6_vwhist256q_sat(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist256q_sat(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist128(SHORTCODE) \
    if (!ctx->pre_commit) { \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist128(cpu_env); \
    }
#define fGEN_TCG_V6_vwhist128q(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist128q(cpu_env); \
        } \
    } while (0)
#define fGEN_TCG_V6_vwhist128m(SHORTCODE) \
    if (!ctx->pre_commit) { \
        TCGv tcgv_uiV = tcg_constant_tl(uiV); \
        assert_vhist_tmp(ctx); \
        gen_helper_vwhist128m(cpu_env, tcgv_uiV); \
    }
#define fGEN_TCG_V6_vwhist128qm(SHORTCODE) \
    do { \
        if (ctx->pre_commit) { \
            intptr_t dstoff = offsetof(CPUHexagonState, qtmp); \
            tcg_gen_gvec_mov(MO_64, dstoff, QvV_off, \
                             sizeof(MMVector), sizeof(MMVector)); \
        } else { \
            TCGv tcgv_uiV = tcg_constant_tl(uiV); \
            assert_vhist_tmp(ctx); \
            gen_helper_vwhist128qm(cpu_env, tcgv_uiV); \
        } \
    } while (0)


#define fGEN_TCG_V6_vassign(SHORTCODE) \
    tcg_gen_gvec_mov(MO_64, VdV_off, VuV_off, \
                     sizeof(MMVector), sizeof(MMVector))

/* Vector conditional move */
#define fGEN_TCG_VEC_CMOV(PRED) \
    do { \
        TCGv lsb = tcg_temp_new(); \
        TCGLabel *false_label = gen_new_label(); \
        TCGLabel *end_label = gen_new_label(); \
        tcg_gen_andi_tl(lsb, PsV, 1); \
        tcg_gen_brcondi_tl(TCG_COND_NE, lsb, PRED, false_label); \
        tcg_temp_free(lsb); \
        tcg_gen_gvec_mov(MO_64, VdV_off, VuV_off, \
                         sizeof(MMVector), sizeof(MMVector)); \
        tcg_gen_br(end_label); \
        gen_set_label(false_label); \
        tcg_gen_ori_tl(hex_slot_cancelled, hex_slot_cancelled, \
                       1 << insn->slot); \
        gen_set_label(end_label); \
    } while (0)


/* Vector conditional move (true) */
#define fGEN_TCG_V6_vcmov(SHORTCODE) \
    fGEN_TCG_VEC_CMOV(1)

/* Vector conditional move (false) */
#define fGEN_TCG_V6_vncmov(SHORTCODE) \
    fGEN_TCG_VEC_CMOV(0)

#endif

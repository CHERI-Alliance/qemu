/*
 * VR5432 extensions translation routines
 *
 * Reference: VR5432 Microprocessor User’s Manual
 *            (Document Number U13751EU5V0UM00)
 *
 *  Copyright (c) 2021 Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "tcg/tcg-op.h"
#include "exec/helper-gen.h"
#include "translate.h"
#include "internal.h"

/* Include the auto-generated decoder. */
#include "decode-vr54xx.c.inc"

/*
 * Integer Multiply-Accumulate Instructions
 *
 * MACC         Multiply, accumulate, and move LO
 * MACCHI       Multiply, accumulate, and move HI
 * MACCHIU      Unsigned multiply, accumulate, and move HI
 * MACCU        Unsigned multiply, accumulate, and move LO
 */

static bool trans_mult_acc(DisasContext *ctx, arg_r *a,
                           void (*gen_helper_mult_acc)(TCGv, TCGv_ptr, TCGv, TCGv))
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t0, a->rs);
    gen_load_gpr(t1, a->rt);

    gen_helper_mult_acc(t0, cpu_env, t0, t1);

    gen_store_gpr(t0, a->rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);

    return false;
}

TRANS(MACC,     trans_mult_acc, gen_helper_macc);
TRANS(MACCHI,   trans_mult_acc, gen_helper_macchi);
TRANS(MACCHIU,  trans_mult_acc, gen_helper_macchiu);
TRANS(MACCU,    trans_mult_acc, gen_helper_maccu);

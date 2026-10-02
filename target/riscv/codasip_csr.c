/*
 * Codasip-specific CSRs.
 *
 * Copyright (c) 2026 Codasip s.r.o.
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
#include "cpu.h"
#include "cpu_bits.h"

/*
 * The CSR table is shared by every CPU in the process, so registering these
 * makes the entries visible to all harts. The predicate is what restricts
 * access to CPUs that actually implement them.
 */
static RISCVException codasip_csr(CPURISCVState *env, int csrno)
{
    if (riscv_cpu_cfg(env)->ext_xcodasipcsr) {
        return RISCV_EXCP_NONE;
    }

    return RISCV_EXCP_ILLEGAL_INST;
}

static RISCVException read_zero(CPURISCVState *env, int csrno,
                                target_ulong *val)
{
    *val = 0;
    return RISCV_EXCP_NONE;
}

static RISCVException write_ignore(CPURISCVState *env, int csrno,
                                   target_ulong val)
{
    return RISCV_EXCP_NONE;
}

typedef struct {
    int csrno;
    riscv_csr_operations csr_ops;
} codasip_csr_def;

/* The Codasip custom CSRs are implemented as read-zero/write-ignore stubs */
#define CODASIP_STUB_CSR(csrno, name) \
    { csrno, { name, codasip_csr, read_zero, write_ignore } }

static codasip_csr_def codasip_csr_list[] = {
    CODASIP_STUB_CSR(CSR_MEXCAUSE,      "mexcause"),
    CODASIP_STUB_CSR(CSR_MFG_CTRL,      "mfg_ctrl"),
    CODASIP_STUB_CSR(CSR_MCACHESTATUS,  "mcachestatus"),
    CODASIP_STUB_CSR(CSR_MDCACHECTRL,   "mdcachectrl"),
    CODASIP_STUB_CSR(CSR_MICACHECTRL,   "micachectrl"),
    CODASIP_STUB_CSR(CSR_MLCACHECTRL,   "mlcachectrl"),
    CODASIP_STUB_CSR(CSR_MTLBCTRL,      "mtlbctrl"),
    CODASIP_STUB_CSR(CSR_MTCMCFG,       "mtcmcfg"),
    CODASIP_STUB_CSR(CSR_MITCMBASEADDR, "mitcmbaseaddr"),
    CODASIP_STUB_CSR(CSR_MDTCMBASEADDR, "mdtcmbaseaddr"),
    CODASIP_STUB_CSR(CSR_SBPREDCTRL,    "sbpredctrl"),
    CODASIP_STUB_CSR(CSR_SCBICSR,       "scbicsr"),
    CODASIP_STUB_CSR(CSR_SCBIGS,        "scbigs"),
    CODASIP_STUB_CSR(CSR_MCBICSR,       "mcbicsr"),
    CODASIP_STUB_CSR(CSR_MCBIGS,        "mcbigs"),
};

void codasip_register_custom_csrs(RISCVCPU *cpu)
{
    cpu->cfg.ext_xcodasipcsr = true;

    for (size_t i = 0; i < ARRAY_SIZE(codasip_csr_list); i++) {
        riscv_set_csr_ops(codasip_csr_list[i].csrno,
                          &codasip_csr_list[i].csr_ops);
    }
}

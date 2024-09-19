// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2024 Codasip s.r.o.

/* Implements a C wrapper around a C++ interface to the brick library.*/

#ifdef __cplusplus
#include <cstdint>
#endif


typedef enum{
    U_MODE=0,
    S_MODE,
    H_MODE,
    M_MODE,
    D_MODE
}CPU_PRIV_MODE;

typedef enum{
    INTEGER=0,
    CAPABILITY,
}CPU_CHERI_MODE;

typedef enum{
    REG_TYPE_CSR=4,
    REG_TYPE_GP=8,
}BRICK_REGISTER_TYPES;

typedef enum _brick_trap_type_
{
    FAULT,
    PROGRAM,
    INTERRUPT,
    NONE
}brick_trap_type;
typedef struct _brick_track_event_
{
    uint32_t insn;
    uint64_t pc;
    uint64_t count;
    int32_t  exception; // The riscv exception code,
    brick_trap_type kind;

}brick_track_event;

typedef struct _brick_track_reg_
{
    // const char* regname;
    uint64_t regindex;
    BRICK_REGISTER_TYPES regtype;
    uint64_t offset;
    uint64_t pesbt;
    bool tag_valid;
    bool is_cap;
}brick_track_reg;

typedef struct _brick_track_cpu_state_{
    CPU_PRIV_MODE privilege;
    bool cheri_mode;
    CPU_CHERI_MODE isamode;
}brick_track_cpu_state;

typedef struct _brick_track_mem_trnsn_{
    uint8_t flags;
    uint64_t addr;
    uint8_t size;
    uint64_t val;
    uint64_t pesbt;
    bool tag_valid;
}brick_track_mem_trnsn;

#define MEM_LD 1
#define MEM_ST 2
#define MEM_CAP 4

#ifndef __cplusplus
void init_eventstream(int logfd);
int output_track_event(brick_track_event *ev);
void track_reg_write(brick_track_reg *reg_event);
void track_cpu_state(brick_track_cpu_state *state);
void track_mem_transaction(brick_track_mem_trnsn *trnsn);
void close_eventstream(void);
#endif 

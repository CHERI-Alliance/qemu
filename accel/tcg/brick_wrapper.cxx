// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: 2024 Codasip s.r.o.

/* Implements a C wrapper around a C++ interface to the brick library.
We instantiate a global instance of the wrapper class for logging which our
C wrapper functions call to handle the calls
*/

#include <core/stream.h>
#include <isa/isa.sb.h>
#include <fb/value.h>

#include "core/engine.h"
#include "core/event.h"
#include "core/io.h"
#include "core/plugin.h"
#include "core/stream.h"
#include "fb/cli.h"
#include "fb/json.h"
#include "fb/value.h"
#include <fcntl.h>
#include <unistd.h>
#include "qemu/log_instr.h"

// Define some defines that rvfi expects
#if defined(_WIN32) && (defined(__x86_64__) || defined(__i386__))
#define QEMU_PACKED __attribute__((gcc_struct, packed))
#else
#define QEMU_PACKED __attribute__((packed))
#endif
#define _Static_assert(X, Y) static_assert(X, Y)

#include "qemu/brick_wrapper.h"
#ifdef TARGET_RISCV
#define TARGET_CHERI
#include "cpu_bits.h"
static brick::isa::Exception::Type fromRISCV_exception_cause(int32_t cause)
{

    switch (cause) {
    case EXCP_NONE:
        return brick::isa::Exception::NONE; /* sentinel value */
    case RISCV_EXCP_INST_ADDR_MIS:
        return brick::isa::Exception::INSTRUCTION_ADDRESS_MISALIGNED;
    case RISCV_EXCP_INST_ACCESS_FAULT:
        return brick::isa::Exception::INSTRUCTION_ACCESS_FAULT;
    case RISCV_EXCP_ILLEGAL_INST:
        return brick::isa::Exception::ILLEGAL_INSTRUCTION;
    case RISCV_EXCP_BREAKPOINT:
        return brick::isa::Exception::BREAKPOINT;
    case RISCV_EXCP_LOAD_ADDR_MIS:
        return brick::isa::Exception::LOAD_ADDRESS_MISALIGNED;
    case RISCV_EXCP_LOAD_ACCESS_FAULT:
        return brick::isa::Exception::LOAD_ACCESS_FAULT;
    case RISCV_EXCP_STORE_AMO_ADDR_MIS:
        return brick::isa::Exception::STORE_AMO_ADDRESS_MISALIGNED;
    case RISCV_EXCP_STORE_AMO_ACCESS_FAULT:
        return brick::isa::Exception::STORE_AMO_ACCESS_FAULT;
    case RISCV_EXCP_U_ECALL:
        return brick::isa::Exception::ECALL_FROM_U;
    case RISCV_EXCP_S_ECALL:
        return brick::isa::Exception::ECALL_FROM_S;
    case RISCV_EXCP_VS_ECALL:
        return brick::isa::Exception::ECALL_FROM_VS;
    case RISCV_EXCP_M_ECALL:
        return brick::isa::Exception::ECALL_FROM_M;
    case RISCV_EXCP_INST_PAGE_FAULT:
        return brick::isa::Exception::INSTRUCTION_PAGE_FAULT; /* since: priv-1.10.0 */
    case RISCV_EXCP_LOAD_PAGE_FAULT:
        return brick::isa::Exception::LOAD_PAGE_FAULT; /* since: priv-1.10.0 */
    case RISCV_EXCP_STORE_PAGE_FAULT:
        return brick::isa::Exception::STORE_AMO_PAGE_FAULT; /* since: priv-1.10.0 */
    // case RISCV_EXCP_SEMIHOST:
    //     return 0x10;
    case RISCV_EXCP_INST_GUEST_PAGE_FAULT:
        return brick::isa::Exception::INSTRUCTION_GUEST_PAGE_FAULT;
    case RISCV_EXCP_LOAD_GUEST_ACCESS_FAULT:
        return brick::isa::Exception::LOAD_GUEST_PAGE_FAULT;
    case RISCV_EXCP_VIRT_INSTRUCTION_FAULT:
        return brick::isa::Exception::VIRTUAL_INSTRUCTION;
    case RISCV_EXCP_STORE_GUEST_AMO_ACCESS_FAULT:
        return brick::isa::Exception::STORE_AMO_GUEST_PAGE_FAULT;
    case RISCV_EXCP_LOAD_CAP_PAGE_FAULT:
        return brick::isa::Exception::PERMIT_LOAD_CAPABILITY_VIOLATION;
    case RISCV_EXCP_STORE_AMO_CAP_PAGE_FAULT:
        return brick::isa::Exception::PERMIT_STORE_CAPABILITY_VIOLATION;
    case RISCV_EXCP_CHERI:
        return brick::isa::Exception::CHERI;
    default:
        return brick::isa::Exception::NONE;
    }
}
#endif

class BrickWrapper
{
    std::unique_ptr<brick::core::EventStreamWriter> writer;
    std::unique_ptr<brick::isa::ExecutionStep::Value> event;
    std::unique_ptr<brick::fb::Value> props;
    int fd = 0;
    bool in_reset=true;

  public:
    /* initialise an evenstream with the supplied path unless a file has
    already been opened */
    void init_eventstream(int log_fd)
    {
        // if (fd <= 0) {

        //     fd = open(filename, O_CREAT | O_RDWR, 0666);
        //     writer = std::make_unique<brick::core::EventStreamWriter>(fd,
        //     true);
        // }
        //
        writer = std::make_unique<brick::core::EventStreamWriter>(log_fd, true);
        props = std::make_unique<brick::fb::Value>();
        event = std::make_unique<brick::isa::ExecutionStep::Value>();
        fd = log_fd;
    }

    // send an output event with the data
    void output_track_event(brick_track_event *ev)
    {
        if(ev->pc == 0x80000000){
            in_reset = false;
        }
        if(in_reset){ 
            // if we have not seen the start adddess then drop the stored data

            props = std::make_unique<brick::fb::Value>();
            event = std::make_unique<brick::isa::ExecutionStep::Value>();
            event->clear();
            return;
        }
        if (fd > 0) {

            brick::core::EventBuilder evb;
            brick::core::PropertiesBuilder prb;

            event->pc = ev->pc;
            if (ev->exception!=-1)
            {
                #ifdef TARGET_RISCV
                event->exception = fromRISCV_exception_cause(ev->exception);
                #else
                event->exception = brick::isa::Exception::NONE; 
                #endif
                switch (ev->kind)
                {
                case PROGRAM:
                event->kind = brick::isa::Kind::PROGRAM;
                    break;
                case INTERRUPT:
                event->kind = brick::isa::Kind::ASYNC;
                    break;
                case FAULT:
                event->kind = brick::isa::Kind::FAULT;
                    break;
                
                default:
                    break;
                }
            }
            event->opcode = ev->insn;
            (*props)["ts"]["value"] = ev->count;
            (*props)["ts"]["unit"] = "clk";

            writer->write(evb.compile<brick::isa::ExecutionStep>(*event),
                          prb.compile(*props));
            // drop and assign new values here will clear up for the next
            // instruction.
            props = std::make_unique<brick::fb::Value>();
            event = std::make_unique<brick::isa::ExecutionStep::Value>();
            event->clear();
        }
    }

// define macros for indexing into the brick register names headers
#define CSR(n, i)                                                              \
    case i:                                                                    \
        name = #n;                                                             \
        break;
#define GPR(n, i)                                                              \
    case i:                                                                    \
        name = #n;                                                             \
        break;


    void track_reg_write(brick_track_reg *reg_event)
    {
        std::string name;
        int64_t index = reg_event->regindex;

        if (reg_event->regtype == REG_TYPE_CSR) {
            switch (index) {
#include <isa/csr.inc>
            default:
                name = "?";
                break;
            }
        } else if (reg_event->regtype == REG_TYPE_GP) {
            switch (index) {
#include <isa/gpr.inc>
            default:
                name = "?";
                break;
            }
        }
#undef CSR
#undef GPR

        brick::isa::Reg::Value reg;
        reg.name = name;
        reg.value.set<uint64_t>(reg_event->offset);
        event->regs.push_back(reg);

        if (reg_event->is_cap) {
            brick::isa::Cap::Value cap;
            cap.name = name;
            cap.value.set<uint64_t> (reg_event->pesbt);
            cap.tag = reg_event->tag_valid;

            event->caps.push_back(cap);
        }
    }
    void track_cpu_state(brick_track_cpu_state *state)
    {
        switch (state->privilege) {
        case QEMU_LOG_INSTR_CPU_TARGET1:
            event->privilege = brick::isa::Privilege::MACHINE;
            break;
        case QEMU_LOG_INSTR_CPU_SUPERVISOR:
        case QEMU_LOG_INSTR_CPU_HYPERVISOR:

            event->privilege = brick::isa::Privilege::SUPERVISOR;
            break;
        case QEMU_LOG_INSTR_CPU_USER:
            event->privilege = brick::isa::Privilege::USER;
            break;
        case QEMU_LOG_INSTR_CPU_DEBUG:
            event->privilege = brick::isa::Privilege::DEBUG;
            break;
        case QEMU_LOG_INSTR_CPU_TARGET2:
        case QEMU_LOG_INSTR_CPU_TARGET3:
        case QEMU_LOG_INSTR_CPU_TARGET4:
        case QEMU_LOG_INSTR_CPU_MODE_MAX:
            std::cerr
                << "Warning unexpected CPU privilege mode treating as MACHINE"
                << std::endl;
            event->privilege = brick::isa::Privilege::MACHINE;
        }
        event->cheri = state->cheri_mode;
        switch (state->isamode) {
        case INTEGER:
            event->mode = brick::isa::Mode::INTEGER;
            break;
            case CAPABILITY:
                event->mode = brick::isa::Mode::CAPABILITY;
            }
    }
    void track_mem_transaction(brick_track_mem_trnsn *trnsn)
    {
        brick::isa::Memory::Value mem;
        auto val = trnsn->val;
        auto pesbt = trnsn->pesbt;

        mem.addr = trnsn->addr;
        mem.pa = trnsn->addr;
        // needs to handle capabilities
        // if there is a tag we should handle it
        // also need to extend pesbt into val
        for (size_t i = 0; i < trnsn->size; i++) {
            mem.data.push_back(val& 0xff);
            val = val >> 8 | (pesbt & 0xff) << 56;
            pesbt = pesbt >> 8;
        }
        if (trnsn->flags & MEM_CAP) {
            mem.tag = trnsn->tag_valid ? 1 : 0;
        }
        if (trnsn->flags & MEM_LD) {
            event->loads.push_back(mem);
        } else {
            event->stores.push_back(mem);
        }
    }
    void close_eventstream()
    {
        writer = nullptr;
    }
};

// instantiate our wrapper instance
BrickWrapper wrapper_instance;
extern "C" {

int output_track_event(brick_track_event *ev)
{
    wrapper_instance.output_track_event(ev);
    return 0;
}

void init_eventstream(int logfd)
{
    return wrapper_instance.init_eventstream(logfd);
}

void track_reg_write(brick_track_reg *reg_event)
{
    return wrapper_instance.track_reg_write(reg_event);
}

void track_cpu_state(brick_track_cpu_state *state)
{
    return wrapper_instance.track_cpu_state(state);
}

void track_mem_transaction(brick_track_mem_trnsn *trnsn)
{
    return wrapper_instance.track_mem_transaction(trnsn);
}

void close_eventstream()
{
    return wrapper_instance.close_eventstream();
}

}// extern C

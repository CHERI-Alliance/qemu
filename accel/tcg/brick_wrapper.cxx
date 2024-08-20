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
// Define some defines that rvfi expects
#if defined(_WIN32) && (defined(__x86_64__) || defined(__i386__))
#define QEMU_PACKED __attribute__((gcc_struct, packed))
#else
#define QEMU_PACKED __attribute__((packed))
#endif
#define _Static_assert(X, Y) static_assert(X, Y)

#include "qemu/brick_wrapper.h"

class BrickWrapper
{
    std::unique_ptr<brick::core::EventStreamWriter> writer;
    std::unique_ptr<brick::isa::ExecutionStep::Value> event;
    std::unique_ptr<brick::fb::Value> props;
    int fd = 0;

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
        if (fd > 0) {

            brick::core::EventBuilder evb;
            brick::core::PropertiesBuilder prb;

            event->pc = ev->pc;

            event->opcode = ev->insn;
            (*props)["ts"]["value"] = ev->count;

            writer->write(evb.compile<brick::isa::ExecutionStep>(*event),
                          prb.compile(*props));
            // drop and assign new values here will clear up for the next
            // instruction.
            props = std::make_unique<brick::fb::Value>();
            event = std::make_unique<brick::isa::ExecutionStep::Value>();
            event->clear();
        }
    }
    void track_reg_write(brick_track_reg *reg_event)
    {
        if (reg_event->is_cap) {
            brick::isa::Cap::Value cap;
            cap.name = reg_event->regname;

            __uint128_t capval =
                reg_event->offset | ((__uint128_t)reg_event->pesbt) << 64;
            cap.value.set<__uint128_t> (capval);
            cap.tag = reg_event->tag_valid;

            event->caps.push_back(cap);
        } else {
            brick::isa::Reg::Value reg;
            reg.name = reg_event->regname;
            reg.value.set<uint64_t>(reg_event->offset);
            event->regs.push_back(reg);
        }
    }
    void track_cpu_state(brick_track_cpu_state *state)
    {
        switch (state->privilege) {
        case M_MODE:
            event->privilege = brick::isa::Privilege::MACHINE;
            break;
            case S_MODE:
            case H_MODE:

            event->privilege = brick::isa::Privilege::SUPERVISOR;
            break;
            case U_MODE:
            event->privilege = brick::isa::Privilege::USER;
            break;
            case D_MODE:
            event->privilege = brick::isa::Privilege::DEBUG;
            break;
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
        mem.addr = trnsn->addr;
        for (size_t i = 0; i < trnsn->size; i++) {
            mem.data.push_back(val& 0xff);
            val >>=8;
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

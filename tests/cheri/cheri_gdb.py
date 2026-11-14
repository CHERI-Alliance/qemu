# SPDX-License-Identifier: BSD-2-Clause
# Note: This helper module was AI-generated for the tests/cheri gdb scripts.
"""Shared GDB Python helpers for deterministic MTTCG race tests."""

import os
import re
import struct
import gdb

SKIP = 77


def elf_symbols(elf_path=None):
    if elf_path is None:
        elf_path = os.environ.get("CHERI_TEST_ELF")
    if not elf_path:
        args = gdb.execute("show args", to_string=True)
        m = re.search(r"-kernel\s+(\S+)", args)
        if not m:
            raise RuntimeError(f"cannot find -kernel in args: {args}")
        elf_path = m.group(1).strip("'\"")
    with open(elf_path, "rb") as f:
        data = f.read()
    (e_shoff,) = struct.unpack_from("<Q", data, 40)
    e_shentsize, e_shnum = struct.unpack_from("<HH", data, 58)
    sections = [
        struct.unpack_from("<IIQQQQIIQQ", data, e_shoff + i * e_shentsize)
        for i in range(e_shnum)
    ]
    syms = {}
    for s in sections:
        _, sh_type, _, _, sh_off, sh_size, sh_link, _, _, sh_entsize = s
        if sh_type != 2:  # SHT_SYMTAB
            continue
        str_off, str_size = sections[sh_link][4], sections[sh_link][5]
        strtab = data[str_off:str_off + str_size]
        for off in range(sh_off, sh_off + sh_size, sh_entsize):
            st_name, _, _, _, st_value, _ = struct.unpack_from(
                "<IBBHQQ", data, off
            )
            end = strtab.find(b"\0", st_name)
            syms[strtab[st_name:end].decode()] = st_value
    return syms


def ram_host_addr(guest_paddr, ram_base):
    host = int(gdb.parse_and_eval(
        "(uintptr_t)current_machine->ram->ram_block->host"
    ))
    return host + (guest_paddr - ram_base)


def find_vcpu(cpu_index):
    target = f"CPU {cpu_index}/TCG"
    for t in gdb.selected_inferior().threads():
        if t.name == target:
            return t
    names = [t.name for t in gdb.selected_inferior().threads()]
    print(f"SKIP: {target} not active (threads: {names})")
    gdb.execute(f"quit {SKIP}")


def read_u32(host_addr):
    return int(gdb.parse_and_eval(f"*(volatile uint32_t *)0x{host_addr:x}"))


def write_u32(host_addr, val):
    gdb.execute(f"set var *(volatile uint32_t *)0x{host_addr:x} = {val}")


def read_u64(host_addr):
    return int(gdb.parse_and_eval(f"*(volatile uint64_t *)0x{host_addr:x}"))

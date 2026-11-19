# SPDX-License-Identifier: BSD-2-Clause
# Note: This helper module was AI-generated for the tests/cheri gdb scripts.
"""Shared GDB Python helpers for deterministic MTTCG race tests."""

import os
import re
import struct
import gdb

def fail(reason):
    print(f"FAIL: {reason}")
    gdb.execute("quit 1")


def require_symbol(*candidates):
    for name in candidates:
        if (gdb.lookup_global_symbol(name) is not None or
                gdb.lookup_static_symbol(name) is not None):
            return name
    fail(f"none of {candidates} found in QEMU binary")


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
    fail(f"{target} not active (threads: {names})")


def get_tagblock(page_idx):
    try:
        expr = (f"((CheriTagBlock **)"
                f"current_machine->ram->ram_block->cheri_tags)[{page_idx}]")
        ptr = gdb.parse_and_eval(expr)
    except gdb.error as exc:
        fail(f"CheriTagBlock layout unavailable ({exc})")
    addr = int(ptr)
    return (addr, ptr.dereference()) if addr != 0 else (0, None)


def read_u32(host_addr):
    return int(gdb.parse_and_eval(f"*(volatile uint32_t *)0x{host_addr:x}"))


def write_u32(host_addr, val):
    gdb.execute(f"set var *(volatile uint32_t *)0x{host_addr:x} = {val}")


def read_u64(host_addr):
    return int(gdb.parse_and_eval(f"*(volatile uint64_t *)0x{host_addr:x}"))


class ThreadBreakpoint(gdb.Breakpoint):
    def __init__(self, spec, thread_name=None, cond_fn=None):
        super().__init__(spec, internal=True)
        self.thread_name = thread_name
        self.cond_fn = cond_fn

    def stop(self):
        t = gdb.selected_thread()
        if self.thread_name and (t is None or t.name != self.thread_name):
            return False
        return True if self.cond_fn is None else bool(self.cond_fn())


class HostWatchpoint(gdb.Breakpoint):
    def __init__(self, host_addr, ctype="uint64_t", wp_class=gdb.WP_WRITE,
                 thread_name=None):
        expr = f"*(volatile {ctype} *)0x{host_addr:x}"
        super().__init__(expr, gdb.BP_WATCHPOINT, wp_class, internal=True)
        self.thread_name = thread_name

    def stop(self):
        t = gdb.selected_thread()
        if self.thread_name and (t is None or t.name != self.thread_name):
            return False
        return True


_RET_CACHE = {}


def _func_ret_addrs(func_name):
    if func_name not in _RET_CACHE:
        sym = (gdb.lookup_global_symbol(func_name) or
               gdb.lookup_static_symbol(func_name))
        blk = gdb.block_for_pc(int(sym.value().address))
        while blk.function is None:
            blk = blk.superblock
        arch = gdb.selected_inferior().architecture()
        insns = arch.disassemble(blk.start, blk.end - 1)
        _RET_CACHE[func_name] = [
            i["addr"] for i in insns if i["asm"].split()[0].startswith("ret")
        ]
    return _RET_CACHE[func_name]


def finish_until_caller(func_name, vcpu):
    """Run vcpu until func_name's 'ret' instruction in .text.

    TCG helper_* functions return into split-WX MAP_SHARED code_gen_buffer,
    where GDB's built-in 'finish' misses the frame-ID check. Breaking on the
    helper's own 'ret' instruction in .text stays in MAP_PRIVATE code with
    all side effects complete and %rax holding the return value.
    """
    vcpu.switch()
    bps = [ThreadBreakpoint(f"*0x{addr:x}", thread_name=vcpu.name)
           for addr in _func_ret_addrs(func_name)]
    gdb.execute("continue")
    for bp in bps:
        bp.delete()
    return int(gdb.parse_and_eval("(unsigned long)$rax"))


def finish_top_frame(vcpu):
    vcpu.switch()
    return finish_until_caller(gdb.selected_frame().name(), vcpu)


def run_helper_call(vcpu, helper_sym, cond_fn=None):
    vcpu.switch()
    bp = ThreadBreakpoint(helper_sym, thread_name=vcpu.name, cond_fn=cond_fn)
    gdb.execute("continue")
    bp.delete()
    return finish_until_caller(helper_sym, vcpu)

# Note: This test is fully AI-generated (prompted to generate adversarial
# regression tests exposing MTTCG capability tag memory bugs).
#
# Deterministically forces the RISC-V hardware PTW A/D update race against a
# concurrent capability store (target/riscv/cpu_helper.c):
# - Stops CPU 0/TCG at qemu_map_ram_ptr() inside get_physical_address() for
#   VA_DATA (0x40000000), after Hart 0 has read the PTE (PTE.A == 0) and before
#   it acquires any tag lock or runs qatomic_cmpxchg().
# - With scheduler-locking on, lets CPU 1/TCG run until round_done is updated
#   (confirming Hart 1's SY of CAP_MARKER to l0_pt[0] has completed and
#   released its tag lock).
# - Releases both harts so Hart 0 executes the PTW tag invalidation + cmpxchg.
#   Because *pte_pa is now CAP_MARKER, cmpxchg fails; if QEMU cleared the tag
#   before cmpxchg succeeded, Hart 0 observes CAP_MARKER with tag == 0 and
#   exits with status 1.

python
import cheri_gdb
import gdb

RAM_BASE = 0x80000000
VA_DATA = 0x40000000
CAP_MARKER = 0xdeadbeef00000000

cheri_gdb.require_symbol("get_physical_address")
map_sym = cheri_gdb.require_symbol("qemu_map_ram_ptr")

syms = cheri_gdb.elf_symbols()
if "l0_pt" not in syms or "round_done" not in syms:
    cheri_gdb.fail("l0_pt or round_done missing from guest ELF")

def in_ptw_ad_update():
    frame = gdb.selected_frame().older()
    if frame is None or frame.name() != "get_physical_address":
        return False
    try:
        return int(frame.read_var("addr")) == VA_DATA
    except gdb.error:
        return False

ptw_bp = cheri_gdb.ThreadBreakpoint(map_sym, thread_name="CPU 0/TCG",
                                    cond_fn=in_ptw_ad_update)
end

run

python
ptw_bp.delete()
hart0 = cheri_gdb.find_vcpu(0)
hart1 = cheri_gdb.find_vcpu(1)

l0_pt_host = cheri_gdb.ram_host_addr(syms["l0_pt"], RAM_BASE)
round_done_host = cheri_gdb.ram_host_addr(syms["round_done"], RAM_BASE)

gdb.execute("set scheduler-locking on")

if cheri_gdb.read_u64(l0_pt_host) != CAP_MARKER:
    hart1.switch()
    wp = cheri_gdb.HostWatchpoint(round_done_host, ctype="uint32_t",
                                  wp_class=gdb.WP_WRITE,
                                  thread_name="CPU 1/TCG")
    gdb.execute("continue")
    wp.delete()

if cheri_gdb.read_u64(l0_pt_host) != CAP_MARKER:
    cheri_gdb.fail(
        f"expected l0_pt[0] == 0x{CAP_MARKER:x} after Hart 1 SY, "
        f"got 0x{cheri_gdb.read_u64(l0_pt_host):x}"
    )

hart0.switch()
gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

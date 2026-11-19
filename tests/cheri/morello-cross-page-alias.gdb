# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Deterministically forces an AB-BA cross-page tag-lock collision on Morello
# using hardware watchpoints on page_a and page_b's CheriTagBlock seqlocks
# (without depending on internal cheri_tagmem.c helper names):
# - Runs whichever CPU maps [higher_blk, lower_blk] first until it acquires
#   its first page lock. If it already pre-sorted into canonical (lower_blk,
#   higher_blk) order, no AB-BA deadlock is possible.
# - Otherwise it holds higher_blk while lower_blk is still free; steps the
#   other CPU ([lower_blk, higher_blk]) until it acquires lower_blk, then
#   releases both CPUs simultaneously so unfixed lock acquisition deadlocks
#   immediately while ordered lock recovery completes cleanly.

python
import cheri_gdb
import gdb

inv_sym = cheri_gdb.require_symbol(
    "helper_cheri_invalidate_lock_tags_start_or_dummy"
)
syms = cheri_gdb.elf_symbols()
ram_base = syms["_start"]
idx_a = (syms["page_a"] - ram_base) // 4096
idx_b = (syms["page_b"] - ram_base) // 4096


def ready_and_unlocked():
    smp_go_host = cheri_gdb.ram_host_addr(syms["smp_go"], ram_base)
    if cheri_gdb.read_u32(smp_go_host) != 1:
        return False
    ptr_a, blk_a = cheri_gdb.get_tagblock(idx_a)
    ptr_b, blk_b = cheri_gdb.get_tagblock(idx_b)
    if not ptr_a or not ptr_b:
        return False
    return (int(blk_a["seqlock"]) & 1) == 0 and (int(blk_b["seqlock"]) & 1) == 0


start_bp = cheri_gdb.ThreadBreakpoint(inv_sym, cond_fn=ready_and_unlocked)
end

run

python
start_bp.delete()
cpu0 = cheri_gdb.find_vcpu(0)
cpu1 = cheri_gdb.find_vcpu(1)
ptr_a, blk_a = cheri_gdb.get_tagblock(idx_a)
ptr_b, blk_b = cheri_gdb.get_tagblock(idx_b)

# CPU 0 maps VA_AB ([page_a, page_b]); CPU 1 maps VA_BA ([page_b, page_a]).
if ptr_a > ptr_b:
    reverse_cpu, forward_cpu = cpu0, cpu1
    higher_blk, lower_blk = blk_a, blk_b
else:
    reverse_cpu, forward_cpu = cpu1, cpu0
    higher_blk, lower_blk = blk_b, blk_a

higher_seq = int(higher_blk["seqlock"].address)
lower_seq = int(lower_blk["seqlock"].address)

gdb.execute("set scheduler-locking on")
reverse_cpu.switch()
wp_hi = cheri_gdb.HostWatchpoint(higher_seq, ctype="unsigned long",
                                 thread_name=reverse_cpu.name)
wp_lo = cheri_gdb.HostWatchpoint(lower_seq, ctype="unsigned long",
                                 thread_name=reverse_cpu.name)
gdb.execute("continue")
wp_hi.delete()
wp_lo.delete()

hi_locked = (int(higher_blk["seqlock"]) & 1) != 0
lo_locked = (int(lower_blk["seqlock"]) & 1) != 0
if hi_locked and not lo_locked:
    forward_cpu.switch()
    wp_fwd = cheri_gdb.HostWatchpoint(lower_seq, ctype="unsigned long",
                                      thread_name=forward_cpu.name)
    gdb.execute("continue")
    wp_fwd.delete()
    print(f"cross-page-alias: forced AB-BA lock collision "
          f"(0x{int(higher_blk.address):x} <-> 0x{int(lower_blk.address):x}), "
          f"releasing both CPUs")
else:
    print("cross-page-alias: reverse mapping locked lower block first")

gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

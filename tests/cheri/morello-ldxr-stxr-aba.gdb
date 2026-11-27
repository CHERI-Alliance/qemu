# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Deterministically tests Morello LDXR/STXR reservation non-interference using
# only top-level TCG instruction helpers (helper_load_exclusive_cap_via_cap /
# helper_store_exclusive_cap_via_cap):
# - Phase 0a (Treiber stack LDXR/STXR ABA) and Phase 0b (LDXP/STXP pair ABA)
#   run natively in guest C.
# - Once both CPUs reach Phase 1 (phase1_go == 1), steps:
#   * CPU 1 takes an LDXR reservation at cursor C.
#   * CPU 0 runs LDXR + STXR (advancing cursor to C + 1 so CPU 1 is stale).
#   * CPU 0 takes a fresh LDXR reservation at cursor C + 1.
#   * CPU 1 runs its stale STXR (must fail without bumping seqlock).
#   * CPU 0 runs its active STXR (must succeed and advance cursor to C + 2).

python
import cheri_gdb
import gdb

ldxr_sym = cheri_gdb.require_symbol("helper_load_exclusive_cap_via_cap")
stxr_sym = cheri_gdb.require_symbol("helper_store_exclusive_cap_via_cap")
syms = cheri_gdb.elf_symbols()
ram_base = syms["_start"]
excl_idx = (syms["excl_page"] - ram_base) // 4096
# sync_page layout: aba_state (16B) + 7 cap_slots (112B) + cpu1_ready (4B)
phase1_go_va = syms["sync_page"] + 132


def phase1_ready():
    go_host = cheri_gdb.ram_host_addr(phase1_go_va, ram_base)
    if cheri_gdb.read_u32(go_host) != 1:
        return False
    ptr, blk = cheri_gdb.get_tagblock(excl_idx)
    return ptr != 0 and (int(blk["seqlock"]) & 1) == 0


phase_bp = cheri_gdb.ThreadBreakpoint(ldxr_sym, thread_name="CPU 0/TCG",
                                      cond_fn=phase1_ready)
end

run

python
phase_bp.delete()
cpu0 = cheri_gdb.find_vcpu(0)
cpu1 = cheri_gdb.find_vcpu(1)
stack_top_host = cheri_gdb.ram_host_addr(syms["excl_page"], ram_base)

gdb.execute("set scheduler-locking on")

# CPU 1 takes an LDXR reservation (draining any prior in-flight STXR first).
cheri_gdb.run_helper_call(cpu1, ldxr_sym)
c0 = cheri_gdb.read_u64(stack_top_host)

# CPU 0 finishes its paused LDXR and runs STXR -> cursor becomes c0 + 1.
cheri_gdb.finish_top_frame(cpu0)
cheri_gdb.run_helper_call(cpu0, stxr_sym)
if cheri_gdb.read_u64(stack_top_host) != c0 + 1:
    print("FAIL: expected CPU 0 uncontended STXR to advance cursor")
    gdb.execute("quit 1")

# CPU 0 takes a fresh LDXR reservation at cursor c0 + 1.
cheri_gdb.run_helper_call(cpu0, ldxr_sym)

# CPU 1 runs its stale STXR (must fail without bumping seqlock).
cheri_gdb.run_helper_call(cpu1, stxr_sym)
if cheri_gdb.read_u64(stack_top_host) != c0 + 1:
    print("FAIL: expected CPU 1 stale STXR to fail")
    gdb.execute("quit 1")

# CPU 0 runs its active STXR (must still succeed -> cursor becomes c0 + 2).
cheri_gdb.run_helper_call(cpu0, stxr_sym)
if cheri_gdb.read_u64(stack_top_host) != c0 + 2:
    print("FAIL: stale STXR on CPU 1 clobbered CPU 0's active reservation")
    gdb.execute("quit 1")

print("morello-aba: CPU 0 STXR succeeded after CPU 1 stale STXR")
gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

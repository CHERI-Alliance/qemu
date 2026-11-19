# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Deterministically tests RISC-V LR.Y/SC.Y reservation non-interference using
# only top-level TCG instruction helpers (helper_lr_c_modedep /
# helper_sc_c_modedep):
# - Phase 0 (Treiber stack A -> B -> A race) runs natively in guest C.
# - Once all 4 harts reach Phase A (bar_a1 == 4), steps:
#   * Hart 1 takes an LR.Y reservation (seq = S).
#   * Hart 2 runs LR.Y + SC.Y (succeeds, advancing seqlock to S + 2 so Hart 1's
#     reservation is now stale).
#   * Hart 0 takes a fresh LR.Y reservation (seq = S + 2).
#   * Hart 1 runs its stale SC.Y (must fail without bumping seqlock).
#   * Hart 0 runs its active SC.Y (must still succeed).

python
import cheri_gdb
import gdb

lr_sym = cheri_gdb.require_symbol("helper_lr_c_modedep")
sc_sym = cheri_gdb.require_symbol("helper_sc_c_modedep")
syms = cheri_gdb.elf_symbols()
ram_base = syms["_start"]
lrsc_idx = (syms["lrsc_page"] - ram_base) // 4096
# sync_page layout: aba_state (4B + 12B pad) + 9 cap_slots (144B) = offset 160
bar_a1_va = syms["sync_page"] + 160


def phase_a_ready():
    bar_host = cheri_gdb.ram_host_addr(bar_a1_va, ram_base)
    if cheri_gdb.read_u32(bar_host) != 4:
        return False
    ptr, blk = cheri_gdb.get_tagblock(lrsc_idx)
    return ptr != 0 and (int(blk["seqlock"]) & 1) == 0


phase_bp = cheri_gdb.ThreadBreakpoint(lr_sym, thread_name="CPU 0/TCG",
                                      cond_fn=phase_a_ready)
end

run

python
phase_bp.delete()
hart0 = cheri_gdb.find_vcpu(0)
hart1 = cheri_gdb.find_vcpu(1)
hart2 = cheri_gdb.find_vcpu(2)

gdb.execute("set scheduler-locking on")

# Hart 1 takes an LR.Y reservation (seq = S).
cheri_gdb.run_helper_call(hart1, lr_sym)

# Hart 2 runs LR.Y + SC.Y, advancing seqlock so Hart 1's reservation is stale.
cheri_gdb.run_helper_call(hart2, lr_sym)
sc2_failed = int(cheri_gdb.run_helper_call(hart2, sc_sym))
if sc2_failed != 0:
    print("FAIL: expected Hart 2 uncontended SC.Y to succeed")
    gdb.execute("quit 1")

# Hart 0 finishes the LR.Y it paused on entry to (fresh reservation S + 2).
cheri_gdb.finish_top_frame(hart0)

# Hart 1 executes its stale SC.Y (must fail without bumping seqlock).
sc1_failed = int(cheri_gdb.run_helper_call(hart1, sc_sym))
if sc1_failed == 0:
    print("FAIL: expected Hart 1 stale SC.Y to fail")
    gdb.execute("quit 1")

# Hart 0 executes its active SC.Y (must still succeed).
sc0_failed = int(cheri_gdb.run_helper_call(hart0, sc_sym))
if sc0_failed != 0:
    print("FAIL: stale SC.Y on Hart 1 clobbered Hart 0's active reservation")
    gdb.execute("quit 1")

print("lrsc-aba: Hart 0 SC.Y succeeded after Hart 1 stale SC.Y")
gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

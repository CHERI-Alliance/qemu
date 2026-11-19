# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Deterministically forces three capability/scalar tearing windows on RV64Y
# using only top-level TCG helpers (helper_cbo_zero, helper_store_cap_via_ddc)
# and host hardware watchpoints on shared_slot:
# - CBO.ZERO vs SY atomicity: pauses Hart 2 at memset() inside helper_cbo_zero
#   and verifies the page tag write lock is held across memset().
# - Capability-vs-capability tearing: pauses Hart 0 via a hardware access
#   watchpoint right after reading the first 8-byte word of shared_slot during
#   LY, runs Hart 1's SY of the alternate capability, and lets the guest verify
#   no torn capability with tag == 1 is observed.
# - Capability-vs-scalar forgery: pauses Hart 0 at the same mid-LY point while
#   shared_slot holds a tagged capability, runs Hart 2's CBO.ZERO to overwrite
#   the bytes and clear the tag, and lets the guest verify no forged capability
#   with tag == 1 is observed.

python
import cheri_gdb
import gdb

cbo_sym = cheri_gdb.require_symbol("helper_cbo_zero_cap", "helper_cbo_zero")
sy_sym = cheri_gdb.require_symbol("helper_store_cap_via_ddc")
syms = cheri_gdb.elf_symbols()
ram_base = syms["_start"]
shared_va = syms["test_granules"]
page_idx = (shared_va - ram_base) // 4096


def caller_has(func_name):
    f = gdb.selected_frame()
    while f is not None:
        if f.name() == func_name:
            return True
        f = f.older()
    return False


# Catch the very first CBO.ZERO on Hart 2 so reader harts have not finished.
cbo_bp = cheri_gdb.ThreadBreakpoint(cbo_sym, thread_name="CPU 2/TCG")
end

run

python
cbo_bp.delete()
hart0 = cheri_gdb.find_vcpu(0)
hart1 = cheri_gdb.find_vcpu(1)
hart2 = cheri_gdb.find_vcpu(2)
shared_host = cheri_gdb.ram_host_addr(shared_va, ram_base)

gdb.execute("set scheduler-locking on")

# Ensure shared_slot's CheriTagBlock is allocated and unlocked by completing
# one SY on Hart 1 while Hart 2 remains paused on entry to helper_cbo_zero.
ptr, blk = cheri_gdb.get_tagblock(page_idx)
if ptr == 0 or (int(blk["seqlock"]) & 1) != 0:
    cheri_gdb.run_helper_call(hart1, sy_sym)

# Sub-test 0: verify helper_cbo_zero holds the tag write lock across memset().
hart2.switch()
memset_bp = cheri_gdb.ThreadBreakpoint(
    "memset", thread_name="CPU 2/TCG",
    cond_fn=lambda: caller_has(cbo_sym),
)
gdb.execute("continue")
memset_bp.delete()

_, blk = cheri_gdb.get_tagblock(page_idx)
if (int(blk["seqlock"]) & 1) == 0:
    print("seqlock-tearing: helper_cbo_zero reached memset() without tag lock!")
    cheri_gdb.run_helper_call(hart1, sy_sym)
    cheri_gdb.finish_until_caller(cbo_sym, hart2)
    gdb.execute("set scheduler-locking off")
    gdb.execute("continue")
    gdb.execute("quit $_exitcode")

cheri_gdb.finish_until_caller(cbo_sym, hart2)
print("seqlock-tearing: helper_cbo_zero held tag lock across memset()")

# Sub-test A: tear CAP_A against CAP_B during Hart 0's LY via watchpoint.
cheri_gdb.run_helper_call(hart1, sy_sym)
old_pair = (cheri_gdb.read_u64(shared_host),
            cheri_gdb.read_u64(shared_host + 8))

hart0.switch()
wp_a = cheri_gdb.HostWatchpoint(shared_host, wp_class=gdb.WP_ACCESS,
                                thread_name="CPU 0/TCG")
gdb.execute("continue")
wp_a.delete()

for _ in range(4):
    cheri_gdb.run_helper_call(hart1, sy_sym)
    new_pair = (cheri_gdb.read_u64(shared_host),
                cheri_gdb.read_u64(shared_host + 8))
    if new_pair != old_pair:
        break
print("seqlock-tearing: forced mid-LY capability overwrite")

# Sub-test B: finish Sub-test A's LY on Hart 0, catch the next mid-LY read of
# the tagged capability, and overwrite with CBO.ZERO on Hart 2.
hart0.switch()
wp_b = cheri_gdb.HostWatchpoint(shared_host, wp_class=gdb.WP_ACCESS,
                                thread_name="CPU 0/TCG")
gdb.execute("continue")
wp_b.delete()

cheri_gdb.run_helper_call(hart2, cbo_sym)
print("seqlock-tearing: forced mid-LY CBO.ZERO scalar overwrite")

gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

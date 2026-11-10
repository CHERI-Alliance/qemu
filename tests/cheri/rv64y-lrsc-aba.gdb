# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Forces two RISC-V LR.Y/SC.Y race windows on a lock-free Treiber stack and
# contended counter:
# - Post-lock ABA inside sc_c_impl(): holds Hart 1 while Hart 0 runs LR.Y + LY
#   on stack_top, passes sc_c_impl()'s lockless seqlock check, and pauses at
#   lock_for_cap_rmw(); runs Hart 1 through pop(A), pop(B) + poison, push(A);
#   then releases Hart 0 so the post-lock seqlock != expected_seq + 1 check
#   must reject SC.Y.
# - Stale SC.Y reservation non-interference: gives Hart 1 an LR.Y reservation,
#   invalidates it via Hart 2's LR.Y + SC.Y, gives Hart 0 a fresh LR.Y
#   reservation on the same page, then runs Hart 1's stale SC.Y before Hart 0's
#   SC.Y. A failed stale SC.Y must not acquire the write lock and clobber
#   Hart 0's active reservation.

python
import cheri_gdb
import gdb


class VcpuAddrBreak(gdb.Breakpoint):
    def __init__(self, func, thread_name, var_name, expected_addr):
        super().__init__(func)
        self.thread_name = thread_name
        self.var_name = var_name
        self.expected_addr = expected_addr

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != self.thread_name:
            return False
        val = int(gdb.selected_frame().read_var(self.var_name))
        return val == self.expected_addr


class VcpuCountBreak(gdb.Breakpoint):
    def __init__(self, func, thread_name, target_hits):
        super().__init__(func)
        self.thread_name = thread_name
        self.target_hits = target_hits
        self.hits = 0

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != self.thread_name:
            return False
        self.hits += 1
        return self.hits >= self.target_hits


def run_call_on_vcpu(vcpu, func, addr_va):
    vcpu.switch()
    bp = VcpuAddrBreak(func, vcpu.name, "addr", addr_va)
    gdb.execute("continue")
    bp.delete()
    gdb.execute("finish")
    return gdb.history(0)


syms = cheri_gdb.elf_symbols()
ram_base = syms["_start"]
stack_top_va = syms["lrsc_page"]
sync_page_va = syms["sync_page"]

lr_bp = VcpuAddrBreak("lr_c_impl", "CPU 0/TCG", "addr", stack_top_va)
end

run

python
lr_bp.delete()
hart0 = cheri_gdb.find_vcpu(0)
hart1 = cheri_gdb.find_vcpu(1)
hart2 = cheri_gdb.find_vcpu(2)
aba_host = cheri_gdb.ram_host_addr(sync_page_va, ram_base)

# Sub-test A: post-lock ABA check inside sc_c_impl().
gdb.execute("set scheduler-locking on")
cheri_gdb.write_u32(aba_host, 4)
lock_bp = VcpuAddrBreak("lock_for_cap_rmw", "CPU 0/TCG", "addr", stack_top_va)
end

continue

python
lock_bp.delete()
print("lrsc-aba: CPU 0/TCG paused at lock_for_cap_rmw after pre-lock check")

cheri_gdb.write_u32(aba_host, 3)
hart1.switch()
sc_bp = VcpuCountBreak("sc_c_impl", "CPU 1/TCG", 3)
end

continue

python
sc_bp.delete()
gdb.execute("finish")
print("lrsc-aba: CPU 1/TCG completed A -> B -> A, releasing CPU 0/TCG")
hart0.switch()
gdb.execute("set scheduler-locking off")

# Wait until Phase A contention starts (Hart 2 only runs LR.Y in Phase A).
phase_a_bp = VcpuAddrBreak("lr_c_impl", "CPU 2/TCG", "addr", stack_top_va)
end

continue

python
phase_a_bp.delete()
if gdb.selected_inferior().pid == 0:
    gdb.execute("quit $_exitcode")

# Sub-test B: stale SC.Y on Hart 1 must not clobber Hart 0's fresh LR.Y.
gdb.execute("set scheduler-locking on")

# Hart 1 takes an LR.Y reservation (seq = S).
run_call_on_vcpu(hart1, "lr_c_impl", stack_top_va)

# Hart 2 finishes its LR.Y + SC.Y, advancing seqlock to S + 2 so Hart 1's
# reservation is now stale.
hart2.switch()
gdb.execute("finish")
sc2_failed = int(run_call_on_vcpu(hart2, "sc_c_impl", stack_top_va))
if sc2_failed != 0:
    print("FAIL: expected Hart 2 uncontended SC.Y to succeed")
    gdb.execute("quit 1")

# Hart 0 takes a fresh LR.Y reservation (seq = S + 2).
run_call_on_vcpu(hart0, "lr_c_impl", stack_top_va)

# Hart 1 executes its stale SC.Y (must fail without bumping seqlock).
sc1_failed = int(run_call_on_vcpu(hart1, "sc_c_impl", stack_top_va))
if sc1_failed == 0:
    print("FAIL: expected Hart 1 stale SC.Y to fail")
    gdb.execute("quit 1")

# Hart 0 executes its valid SC.Y (must still succeed).
sc0_failed = int(run_call_on_vcpu(hart0, "sc_c_impl", stack_top_va))
if sc0_failed != 0:
    print("FAIL: stale SC.Y on Hart 1 clobbered Hart 0's active reservation")
    gdb.execute("quit 1")
print("lrsc-aba: Hart 0 SC.Y succeeded after Hart 1 stale SC.Y")

gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

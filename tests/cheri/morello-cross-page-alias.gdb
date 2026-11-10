# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Deterministically forces an AB-BA cross-page tag-lock collision in
# tagmem_clear_tag() on Morello:
# - CPU 0 executes an unaligned store straddling VA_AB ([page_a, page_b]),
#   locks page_a's CheriTagBlock, and pauses on entry to the second
#   tagmem_clear_tag() call at 0x40203000 before locking page_b.
# - With CPU 0 held, CPU 1 executes an unaligned store straddling VA_BA
#   ([page_b, page_a]), locks page_b's CheriTagBlock, and pauses on entry to
#   its second tagmem_clear_tag() call at 0x40205000 before locking page_a.
# - Releases both CPUs simultaneously so unfixed lock acquisition deadlocks
#   immediately while ordered lock acquisition completes cleanly.

python
import cheri_gdb
import gdb

VA_AB_PAGE2 = 0x40203000
VA_BA_PAGE2 = 0x40205000


class CrossPageBreak(gdb.Breakpoint):
    def __init__(self, thread_name, second_page_va):
        super().__init__("tagmem_clear_tag")
        self.thread_name = thread_name
        self.second_page_va = second_page_va

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != self.thread_name:
            return False
        f = gdb.selected_frame()
        return (int(f.read_var("vaddr")) == self.second_page_va and
                int(f.read_var("done")) != 0)


bp0 = CrossPageBreak("CPU 0/TCG", VA_AB_PAGE2)
end

run

python
bp0.delete()
cpu0 = cheri_gdb.find_vcpu(0)
cpu1 = cheri_gdb.find_vcpu(1)
f0 = gdb.selected_frame()
cpu0_done = int(f0.read_var("done"))
cpu0_want = int(f0.read_var("tagmem"))

gdb.execute("set scheduler-locking on")
cpu1.switch()
bp1 = CrossPageBreak("CPU 1/TCG", VA_BA_PAGE2)
end

continue

python
bp1.delete()
f1 = gdb.selected_frame()
cpu1_done = int(f1.read_var("done"))
cpu1_want = int(f1.read_var("tagmem"))

if cpu0_done != cpu1_want or cpu1_done != cpu0_want:
    print(f"FAIL: expected inverted locks, got CPU0 ({cpu0_done:#x} -> "
          f"{cpu0_want:#x}) vs CPU1 ({cpu1_done:#x} -> {cpu1_want:#x})")
    gdb.execute("quit 6")

print(f"cross-page-alias: forced AB-BA lock collision "
      f"(0x{cpu0_done:x} <-> 0x{cpu1_done:x}), releasing both CPUs")
gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

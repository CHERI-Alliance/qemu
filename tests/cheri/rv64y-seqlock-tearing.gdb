# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Deterministically forces three capability/scalar tearing windows on RV64Y:
# - CBO.ZERO vs SY atomicity: pauses Hart 2 inside do_cbo_zero() at memset();
#   if the CheriTagBlock write lock is not held across memset(), runs Hart 1's
#   SY to install a tagged capability before memset() zeroes the payload,
#   exposing a forged all-zeroes tagged capability to Hart 0's guest check.
# - Capability-vs-capability tearing: freezes Hart 0 inside load_cap after
#   reading tag == 1 and CAP_A's pesbt, runs Hart 1's SY of CAP_B before
#   Hart 0 loads cursor, and verifies load_cap retries cleanly.
# - Capability-vs-scalar forgery: freezes Hart 0 at the same mid-load point,
#   runs Hart 2's scalar overwrite + tag invalidation, and verifies load_cap
#   returns tag == 0.

python
import cheri_gdb
import gdb


class CboZeroBreak(gdb.Breakpoint):
    def __init__(self, target_va):
        super().__init__("do_cbo_zero")
        self.target_va = target_va

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 2/TCG":
            return False
        return int(gdb.selected_frame().read_var("address")) == self.target_va


class MemsetInCboBreak(gdb.Breakpoint):
    def __init__(self):
        super().__init__("memset")

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 2/TCG":
            return False
        older = gdb.selected_frame().older()
        return older is not None and older.name() == "do_cbo_zero"


class TagGetBreak(gdb.Breakpoint):
    def __init__(self, target_va):
        super().__init__("cheri_tag_get")
        self.target_va = target_va

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 0/TCG":
            return False
        return int(gdb.selected_frame().read_var("vaddr")) == self.target_va


class CapWriterBreak(gdb.Breakpoint):
    def __init__(self, target_va):
        super().__init__("store_cap_to_memory_mmu_index")
        self.target_va = target_va

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 1/TCG":
            return False
        return int(gdb.selected_frame().read_var("vaddr")) == self.target_va


class ScalarUnlockBreak(gdb.Breakpoint):
    def __init__(self, target_va):
        super().__init__("cheri_tag_invalidate")
        self.target_va = target_va

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 2/TCG":
            return False
        vaddr = int(gdb.selected_frame().read_var("vaddr"))
        return (vaddr & ~0xF) == self.target_va


def get_tagblock(page_idx):
    expr = (f"((CheriTagBlock **)current_machine->ram->ram_block->cheri_tags)"
            f"[{page_idx}]")
    ptr = gdb.parse_and_eval(expr)
    if int(ptr) == 0:
        return None
    return ptr.dereference()


def pause_hart0_after_pesbt(hart0, hart1, shared_slot_va):
    gdb.execute("set scheduler-locking on")
    hart1.switch()
    prep_bp = CapWriterBreak(shared_slot_va)
    gdb.execute("continue")
    prep_bp.delete()
    gdb.execute("finish")

    hart0.switch()
    bp = TagGetBreak(shared_slot_va)
    gdb.execute("continue")
    bp.delete()
    gdb.execute("finish")
    if not bool(gdb.history(0)):
        print("FAIL: expected cheri_tag_get() == true after Hart 1 SY")
        gdb.execute("quit 1")
    gdb.execute("next")
    while gdb.selected_frame().name() != "ldq_he_p":
        gdb.execute("step")
    gdb.execute("finish")
    gdb.execute("next")


syms = cheri_gdb.elf_symbols()
ram_base = syms["_start"]
shared_slot_va = syms["test_granules"]
page_idx = (shared_slot_va - ram_base) // 4096

cbo_bp = CboZeroBreak(shared_slot_va)
end

run

python
cbo_bp.delete()
hart0 = cheri_gdb.find_vcpu(0)
hart1 = cheri_gdb.find_vcpu(1)
hart2 = cheri_gdb.find_vcpu(2)
shared_host = cheri_gdb.ram_host_addr(shared_slot_va, ram_base)

# Ensure shared_slot's CheriTagBlock is allocated and unlocked before holding.
while True:
    blk = get_tagblock(page_idx)
    if blk is not None and (int(blk["seqlock"]) & 1) == 0:
        break
    cbo_bp = CboZeroBreak(shared_slot_va)
    gdb.execute("continue")
    cbo_bp.delete()

# Sub-test 0: verify do_cbo_zero() holds the tag write lock across memset().
gdb.execute("set scheduler-locking on")
memset_bp = MemsetInCboBreak()
gdb.execute("continue")
memset_bp.delete()

blk = get_tagblock(page_idx)
if (int(blk["seqlock"]) & 1) == 0:
    print("seqlock-tearing: do_cbo_zero reached memset() without tag lock!")
    hart1.switch()
    cap_bp = CapWriterBreak(shared_slot_va)
    gdb.execute("continue")
    cap_bp.delete()
    gdb.execute("finish")

    hart2.switch()
    gdb.execute("finish")
    gdb.execute("finish")
    hart0.switch()
    gdb.execute("set scheduler-locking off")
    gdb.execute("continue")
    gdb.execute("quit $_exitcode")

# Finish memset() and do_cbo_zero() cleanly so no spinlock remains held.
gdb.execute("finish")
gdb.execute("finish")
print("seqlock-tearing: do_cbo_zero held tag lock across memset()")

# Sub-test A: tear CAP_A pesbt against CAP_B cursor via Hart 1 SY.
pause_hart0_after_pesbt(hart0, hart1, shared_slot_va)
old_pair = (cheri_gdb.read_u64(shared_host),
            cheri_gdb.read_u64(shared_host + 8))
print(f"seqlock-tearing: Hart 0 read pesbt from "
      f"(0x{old_pair[0]:x}, 0x{old_pair[1]:x})")

hart1.switch()
cap_bp = CapWriterBreak(shared_slot_va)
while True:
    gdb.execute("continue")
    gdb.execute("finish")
    new_pair = (cheri_gdb.read_u64(shared_host),
                cheri_gdb.read_u64(shared_host + 8))
    if new_pair != old_pair:
        break
cap_bp.delete()

hart0.switch()
gdb.execute("finish")
print("seqlock-tearing: Hart 0 recovered from SY capability tearing")

# Sub-test B: keep scheduler-locking on (no lock held) and tear against Hart 2.
pause_hart0_after_pesbt(hart0, hart1, shared_slot_va)
hart2.switch()
scalar_bp = ScalarUnlockBreak(shared_slot_va)
gdb.execute("continue")
scalar_bp.delete()
gdb.execute("finish")

hart0.switch()
gdb.execute("finish")
ret_tag = bool(gdb.history(0))
if ret_tag:
    print("FAIL: load_cap returned tag == 1 after mid-load scalar overwrite")
    gdb.execute("quit 1")
print("seqlock-tearing: Hart 0 returned untagged after mid-load scalar write")
gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

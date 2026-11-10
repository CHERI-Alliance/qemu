# Note: This test script is AI-generated (prompted to generate deterministic
# MTTCG capability tag race reproducers).
#
# Forces the post-lock ABA window inside Morello's
# helper_store_exclusive_cap_via_cap() for both single-capability LDXR/STXR
# (Treiber stack pop) and capability-pair LDXP/STXP:
# - Holds CPU 1 while CPU 0 runs LDXR/LDXP, enters STXR/STXP, passes the
#   lockless seqlock fast-check, and stops at cheri_lock_for_tag_set() before
#   acquiring low_lock.
# - Runs CPU 1 through the intervening A -> B -> A updates so memory holds
#   the original capability value(s) and tag(s) with an advanced seqlock.
# - Releases CPU 0 so STXR/STXP must detect the ABA race via the post-lock
#   seqlock != expected_seq + 1 check.

python
import cheri_gdb
import gdb

REG_NONE = 0x77


class LoadExclBreak(gdb.Breakpoint):
    def __init__(self, want_pair, expected_addr=None):
        super().__init__("helper_load_exclusive_cap_via_cap")
        self.want_pair = want_pair
        self.expected_addr = expected_addr

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 0/TCG":
            return False
        f = gdb.selected_frame()
        is_pair = int(f.read_var("cd2")) != REG_NONE
        if is_pair != self.want_pair:
            return False
        if self.expected_addr is not None:
            return int(f.read_var("addr")) == self.expected_addr
        return True


class TagSetBreak(gdb.Breakpoint):
    def __init__(self, expected_vaddr):
        super().__init__("cheri_lock_for_tag_set")
        self.expected_vaddr = expected_vaddr

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 0/TCG":
            return False
        vaddr = int(gdb.selected_frame().read_var("vaddr"))
        return vaddr == self.expected_vaddr


class Cpu1CountBreak(gdb.Breakpoint):
    def __init__(self, func, target_hits, addr_var=None, expected_addr=None):
        super().__init__(func)
        self.target_hits = target_hits
        self.addr_var = addr_var
        self.expected_addr = expected_addr
        self.hits = 0

    def stop(self):
        t = gdb.selected_thread()
        if t is None or t.name != "CPU 1/TCG":
            return False
        f = gdb.selected_frame()
        if self.addr_var == "caller":
            older = f.older()
            if older is None or older.name() != self.expected_addr:
                return False
        elif self.addr_var is not None:
            val = int(f.read_var(self.addr_var))
            if val != self.expected_addr:
                return False
        self.hits += 1
        return self.hits >= self.target_hits


syms = cheri_gdb.elf_symbols()
ram_base = syms["_start"]
stack_top_va = syms["excl_page"]
sync_page_va = syms["sync_page"]

ldxr_bp = LoadExclBreak(want_pair=False, expected_addr=stack_top_va)
end

run

python
ldxr_bp.delete()
cpu0 = cheri_gdb.find_vcpu(0)
cpu1 = cheri_gdb.find_vcpu(1)
aba_host = cheri_gdb.ram_host_addr(sync_page_va, ram_base)

# Phase 0a: advance CPU 0 into STXR past the pre-lock seqlock check.
gdb.execute("set scheduler-locking on")
cheri_gdb.write_u32(aba_host, 2)
stxr_lock_bp = TagSetBreak(stack_top_va)
end

continue

python
stxr_lock_bp.delete()
print("morello-aba: CPU 0/TCG paused at cheri_lock_for_tag_set in STXR")

cheri_gdb.write_u32(aba_host, 1)
cpu1.switch()
excl_helper = "helper_store_exclusive_cap_via_cap"
cpu1_stxr_bp = Cpu1CountBreak("cheri_tag_writer_release", 3,
                              addr_var="caller", expected_addr=excl_helper)
end

continue

python
cpu1_stxr_bp.delete()
gdb.execute("finish")
print("morello-aba: CPU 1/TCG completed Treiber A -> B -> A")

cpu0.switch()
gdb.execute("set scheduler-locking off")
ldxp_bp = LoadExclBreak(want_pair=True)
end

continue

python
ldxp_bp.delete()
if gdb.selected_inferior().pid == 0:
    gdb.execute("quit $_exitcode")
pair_va = int(gdb.selected_frame().read_var("addr"))

# Phase 0b: advance CPU 0 into STXP past the pre-lock seqlock check.
gdb.execute("set scheduler-locking on")
cheri_gdb.write_u32(aba_host, 4)
stxp_lock_bp = TagSetBreak(pair_va)
end

continue

python
stxp_lock_bp.delete()
print("morello-aba: CPU 0/TCG paused at cheri_lock_for_tag_set in STXP")

cheri_gdb.write_u32(aba_host, 3)
cpu1.switch()
cpu1_stp_bp = Cpu1CountBreak("store_cap_to_memory_mmu_index", 2,
                             addr_var="vaddr", expected_addr=pair_va + 16)
end

continue

python
cpu1_stp_bp.delete()
gdb.execute("finish")
print("morello-aba: CPU 1/TCG completed pair A,B -> C,C -> A,B")

cpu0.switch()
gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

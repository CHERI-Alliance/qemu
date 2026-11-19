# Note: This test is fully AI-generated (prompted to generate adversarial
# regression tests exposing MTTCG capability tag memory bugs).
#
# Uses a hardware write watchpoint on DMA_TARGET inside fw_cfg_dma_transfer()
# (without depending on internal physmem.c dirty-bitmap helpers):
# - Stops CPU 1/TCG at the exact host store that writes the fw_cfg DMA UUID
#   into DMA_TARGET.
# - If the DMA write is not protected by the page's CheriTagBlock write lock
#   or dma_bitmap, holds CPU 1/TCG there and runs only CPU 0/TCG so its LY
#   immediately catches the new DMA bytes paired with the stale tag.
# - Otherwise finishes fw_cfg_dma_transfer() to release the lock cleanly, opens
#   GDB_GATE, and lets the guest verify the DMA data and cleared tag.

python
import cheri_gdb
import gdb

DMA_OFF = 0x10000   # DMA_TARGET - RAM base
GATE_OFF = 0x10020  # GDB_GATE - RAM base

dma_sym = cheri_gdb.require_symbol("fw_cfg_dma_transfer")
csrw_sym = cheri_gdb.require_symbol("helper_csrw")
dma_bp = cheri_gdb.ThreadBreakpoint(dma_sym, thread_name="CPU 1/TCG")
end

run

python
dma_bp.delete()
hart0 = cheri_gdb.find_vcpu(0)
hart1 = cheri_gdb.find_vcpu(1)
ram_host = int(gdb.parse_and_eval(
    "(uintptr_t)current_machine->ram->ram_block->host"
))
dma_host = ram_host + DMA_OFF
gate_host = ram_host + GATE_OFF

gdb.execute("set scheduler-locking on")
wp = cheri_gdb.HostWatchpoint(dma_host, wp_class=gdb.WP_WRITE,
                              thread_name="CPU 1/TCG")
gdb.execute("continue")
wp.delete()

_, blk = cheri_gdb.get_tagblock(DMA_OFF // 4096)
protected = False
if blk is not None:
    seqlock_held = (int(blk["seqlock"]) & 1) != 0
    try:
        dma_masked = (int(blk["dma_bitmap"][0]) & 1) != 0
    except gdb.error:
        dma_masked = False
    protected = seqlock_held or dma_masked

if not protected:
    print("dma-tag-order: DMA payload written without tag lock or dma_bitmap!")
    cheri_gdb.write_u32(gate_host, 1)
    hart0.switch()
    csr_bp = cheri_gdb.ThreadBreakpoint(
        csrw_sym, thread_name="CPU 0/TCG",
        cond_fn=lambda: int(gdb.selected_frame().read_var("csr")) == 0x320,
    )
    gdb.execute("continue")
    csr_bp.delete()
else:
    print("dma-tag-order: DMA payload write protected under lock/dma_bitmap")
    cheri_gdb.finish_until_caller(dma_sym, hart1)
    cheri_gdb.write_u32(gate_host, 1)

gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

# Note: This test is fully AI-generated (prompted to generate adversarial
# regression tests exposing MTTCG capability tag memory bugs).
#
# Stop the vCPU thread doing the fw_cfg DMA in rv64y-dma-tag-order.S once
# the data is in RAM (cpu_physical_memory_set_dirty_range() runs after the
# copy), then run only hart 0 until it has loaded the granule.
python
RAM_OFF = 0x10000   # DMA_TARGET - RAM base
GATE_OFF = 0x10020  # GDB_GATE - RAM base
RAM = "current_machine->ram->ram_block"


class DmaWrite(gdb.Breakpoint):
    def stop(self):
        frame = gdb.selected_frame()
        if int(frame.read_var("length")) != 16:
            return False
        base = int(gdb.parse_and_eval(RAM + "->offset"))
        return int(frame.read_var("start")) == base + RAM_OFF


dma_write = DmaWrite("cpu_physical_memory_set_dirty_range")
end

run

python
threads = [t.name for t in gdb.selected_inferior().threads()]
if "CPU 1/TCG" not in threads:
    print("SKIP: MTTCG not active (threads: %s)" % threads)
    gdb.execute("quit 77")
held = gdb.selected_thread()
if held is None or held.name != "CPU 1/TCG":
    print("dma-tag-order: DMA write not caught (%s)" % held)
    gdb.execute("quit 4")
print("dma-tag-order: CPU 1/TCG stopped after the DMA copy")
dma_write.delete()
gdb.execute("set var *(volatile int *)(%s->host + %d) = 1" % (RAM, GATE_OFF))
hart0 = [t for t in gdb.selected_inferior().threads()
         if t.name == "CPU 0/TCG"][0]
hart0.switch()
gdb.execute("set scheduler-locking on")
gdb.execute("break helper_csrw thread %d if csr == 0x320" % hart0.num)
end

continue

python
print("dma-tag-order: CPU 0/TCG loaded the granule, releasing CPU 1/TCG")
gdb.execute("delete")
gdb.execute("set scheduler-locking off")
end

continue
quit $_exitcode

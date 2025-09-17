# Generate devicetrees with mainline and CHERI QEMU for comparison

# Basic devicetree for the virt device
VIRT_FILES = qemu-system-riscv64-virt

# Single-Core and quadcore devicetrees (has an impact on phandle allocation)
CHERI_FILES_SMP1 = qemu-system-riscv64cheri-v2-vcu118-smp1
CHERI_FILES_SMP4 = qemu-system-riscv64cheri-v2-vcu118-smp4

# These reference devicetrees are built with the yocto from gitlab-public for
# hobgoblin2-vcu118 target. 
# The latest commit at the time of development was 2d8264c16a1bd0c942bc147558c68e40d353796a. 
REFERENCE_A730 = a730-mp4-hobgoblin2-vcu118-qemu
REFERENCE_X730 = x730-mp1-hobgoblin2-vcu118-qemu

all: build-qemu $(VIRT_FILES).dts $(CHERI_FILES_SMP1).dts $(CHERI_FILES_SMP4).dts compare

build-qemu:
	$(MAKE) -C ../../build

$(VIRT_FILES).dtb:
	qemu-system-riscv64 -machine virt,dumpdtb=$@

$(CHERI_FILES_SMP1).dtb:
	qemu-system-riscv64cheri -smp 1 -machine hobgoblin-v2-vcu118,dumpdtb=$@

$(CHERI_FILES_SMP4).dtb:
	qemu-system-riscv64cheri -smp 4 -machine hobgoblin-v2-vcu118,dumpdtb=$@

%.dts: %.dtb
	dtc -I dtb -O dts -s -o $@ $<

compare:
	diff --side-by-side --expand-tabs --suppress-common-lines $(REFERENCE_A730).dts $(CHERI_FILES_SMP4).dts > diff_a730.txt || true
	diff --side-by-side --expand-tabs --suppress-common-lines $(REFERENCE_X730).dts $(CHERI_FILES_SMP1).dts > diff_x730.txt || true

clean:
	rm -f $(VIRT_FILES).dtb $(CHERI_FILES_SMP1).dtb $(CHERI_FILES_SMP4).dtb $(VIRT_FILES).dts $(CHERI_FILES_SMP1).dts $(CHERI_FILES_SMP4).dts

.PHONY: all clean $(VIRT_FILES).dtb $(CHERI_FILES_SMP1).dtb $(CHERI_FILES_SMP4).dtb

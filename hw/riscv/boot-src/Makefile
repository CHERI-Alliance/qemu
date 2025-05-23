# target_arch?=riscv64
# $(info    target_arch is $(target_arch))
#
OBJDUMP = llvm-objdump
CC=clang

BUILD_DIR = ./build

UART_ADDR ?= 0x10000000

RV32_LDFLAGS= -nostdlib -fuse-ld=lld -static -T default.lds -Wl,-defsym=UART_BASE=$(UART_ADDR)  
RV32_CFLAGS= --target=riscv32-unknown-qemu -march=rv32imac  -nostdlib 
RV64_LDFLAGS= -nostdlib -fuse-ld=lld -static -T default.lds -Wl,-defsym=UART_BASE=$(UART_ADDR)  
RV64_CFLAGS= --target=riscv64-unknown-qemu -march=rv64imafd  -mno-relax -static 

RV32C_LDFLAGS= -nostdlib -fuse-ld=lld -static -T default.lds -Wl,-defsym=UART_BASE=$(UART_ADDR)  
RV32C_CFLAGS= --target=riscv32-unknown-qemu -march=rv32imac_zcherihybrid -mabi=il32pc64 -nostdlib 
RV64C_LDFLAGS= -nostdlib -fuse-ld=lld -static -T default.lds -Wl,-defsym=UART_BASE=$(UART_ADDR)  
RV64C_CFLAGS= --target=riscv64-unknown-qemu -march=rv64imafd_zcherihybrid -mabi=l64pc128d -mno-relax -static 


ELF_FILES = $(BUILD_DIR)/rv32_purecap.elf $(BUILD_DIR)/rv32_hybrid.elf $(BUILD_DIR)/rv64_purecap.elf $(BUILD_DIR)/rv64_hybrid.elf
DUMP_FILES = $(ELF_FILES:.elf=.dump)
HEX_FILES = $(ELF_FILES:.elf=.hex)
HEXDUMP_FILES = $(ELF_FILES:.elf=.hexdump)



# LDFLAGS= -m elf64lriscv 

.PHONY: all clean

all: $(ELF_FILES) $(DUMP_FILES) $(HEX_FILES) $(HEXDUMP_FILES)

$(BUILD_DIR)/rv64_hybrid.elf: rv64_hybrid.s
	$(CC) $(RV64_CFLAGS) $(RV64_LDFLAGS) -o $@ $^ 

$(BUILD_DIR)/rv64_purecap.elf: rv64_purecap.s
	$(CC) $(RV64C_CFLAGS) $(RV64C_LDFLAGS) -o $@ $^ 

$(BUILD_DIR)/rv32_hybrid.elf: rv32_hybrid.s
	$(CC) $(RV32_CFLAGS) $(RV32_LDFLAGS) -o $@ $^ 

$(BUILD_DIR)/rv32_purecap.elf: rv32_purecap.s
	$(CC) $(RV32C_CFLAGS) $(RV32C_LDFLAGS) -o $@ $^ 

%.dump: %.elf
	$(OBJDUMP) -dSh $< >$@

%.hex: %.elf
	llvm-objcopy -O binary $^ $@

%.hexdump: %.hex
	xxd -p -e -c4 $^ $@

clean: 
	rm -rf $(BUILD_DIR)/*.elf $(BUILD_DIR)/*.dump

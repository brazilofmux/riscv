# Forth kernel for RV32IM — runs on rv32-run DBT
RISCV_AS = riscv64-unknown-elf-as
RISCV_LD = riscv64-unknown-elf-ld

ASFLAGS = -march=rv32im -mabi=ilp32
LDFLAGS = -T ../runtime/link.ld -m elf32lriscv

.PHONY: all clean run

all: kernel.elf

kernel.o: kernel.s
	$(RISCV_AS) $(ASFLAGS) -o $@ $<

kernel.elf: kernel.o
	$(RISCV_LD) $(LDFLAGS) -o $@ $<

run: kernel.elf
	cat prelude.fth - | ../dbt/rv32-run kernel.elf

clean:
	rm -f kernel.o kernel.elf

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

# Cross-compiler for guest programs
RISCV_CC = riscv64-unknown-elf-gcc
RISCV_CFLAGS = -march=rv32im -mabi=ilp32 -nostdlib -O2

.PHONY: all clean dbt runtime

all: dbt

dbt:
	$(MAKE) -C dbt

runtime:
	$(MAKE) -C runtime

clean:
	$(MAKE) -C dbt clean
	$(MAKE) -C runtime clean

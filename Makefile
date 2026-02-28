CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

# Cross-compiler for guest programs
RISCV_CC = riscv64-unknown-elf-gcc
RISCV_CFLAGS = -march=rv32imfd -mabi=ilp32d -nostdlib -O2

.PHONY: all clean dbt runtime test

all: dbt

dbt:
	$(MAKE) -C dbt

runtime:
	$(MAKE) -C runtime

test: dbt runtime
	$(MAKE) -C tests
	bash tests/run-tests.sh

clean:
	$(MAKE) -C dbt clean
	$(MAKE) -C runtime clean
	$(MAKE) -C tests clean

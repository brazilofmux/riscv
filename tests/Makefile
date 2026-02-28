CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv32imfd -mabi=ilp32d -O2 -Wall -Wextra -ffreestanding -nostdlib -I../runtime/include
LDFLAGS = -T ../runtime/link.ld -nostdlib

RUNTIME = ../runtime/crt0.o ../runtime/libc.a

TESTS = $(wildcard test-*.c)
ELFS = $(TESTS:.c=.elf)

.PHONY: all clean

all: $(ELFS)

%.elf: %.c $(RUNTIME)
	$(CC) $(CFLAGS) $(LDFLAGS) ../runtime/crt0.o $< ../runtime/libc.a -lgcc -o $@

clean:
	rm -f $(ELFS)

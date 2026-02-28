CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv32im -mabi=ilp32 -Os -ffreestanding -nostdlib \
         -I../runtime/include -Isrc -fno-builtin -D__slow32__
LDFLAGS = -T ../runtime/link.ld -nostdlib -march=rv32im -mabi=ilp32

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:.c=.o)

RUNTIME_CRT0 = ../runtime/crt0.o
RUNTIME_LIBC = ../runtime/libc.a

all: dbase.elf

dbase.elf: $(OBJS) $(RUNTIME_CRT0) $(RUNTIME_LIBC)
	$(CC) $(LDFLAGS) $(RUNTIME_CRT0) $(OBJS) $(RUNTIME_LIBC) -lgcc -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) dbase.elf

.PHONY: all clean

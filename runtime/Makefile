CC = riscv64-unknown-elf-gcc
AR = riscv64-unknown-elf-ar
CFLAGS = -march=rv32im -mabi=ilp32 -O2 -Wall -Wextra -ffreestanding -nostdlib -Iinclude
ASFLAGS = -march=rv32im -mabi=ilp32

SRCS = src/string.c src/string_extra.c src/ctype.c src/errno.c \
       src/malloc.c src/stdlib_utils.c src/stdlib_extra.c \
       src/convert.c src/convert_extra.c \
       src/printf.c src/dtoa.c src/stdio.c src/math_soft.c \
       src/time.c

OBJS = $(SRCS:.c=.o)
CRT0 = crt0.o
SYSCALL = src/syscall.o
SETJMP = src/setjmp.o
TIME_SYSCALL = src/time_syscall.o

.PHONY: all clean

all: $(CRT0) libc.a

$(CRT0): crt0.s
	$(CC) $(ASFLAGS) -c $< -o $@

$(SYSCALL): src/syscall.s
	$(CC) $(ASFLAGS) -c $< -o $@

$(SETJMP): src/setjmp.s
	$(CC) $(ASFLAGS) -c $< -o $@

$(TIME_SYSCALL): src/time_syscall.s
	$(CC) $(ASFLAGS) -c $< -o $@

src/dtoa.o: src/dtoa.c
	$(CC) $(CFLAGS) -DIEEE_8087 -Wno-sign-compare -Wno-implicit-fallthrough -Wno-maybe-uninitialized -c $< -o $@

src/math_soft.o: src/math_soft.c
	$(CC) $(CFLAGS) -fno-builtin -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

libc.a: $(OBJS) $(SYSCALL) $(SETJMP) $(TIME_SYSCALL)
	$(AR) rcs $@ $^

clean:
	rm -f $(CRT0) $(OBJS) $(SYSCALL) $(SETJMP) $(TIME_SYSCALL) libc.a

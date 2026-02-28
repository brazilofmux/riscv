CC = riscv64-unknown-elf-gcc
CFLAGS = -march=rv32imfd -mabi=ilp32d -O2 -ffreestanding -nostdlib \
         -I../runtime/include -Isrc -fno-builtin \
         -Wno-implicit-fallthrough
LDFLAGS = -T ../runtime/link.ld -nostdlib -march=rv32imfd -mabi=ilp32d

# All Lua .c files except lua.c and luac.c (standard entry points we don't use)
LUA_SRCS = $(filter-out src/lua.c src/luac.c, $(wildcard src/*.c))
LUA_OBJS = $(LUA_SRCS:.c=.o)

RUNTIME_CRT0 = ../runtime/crt0.o
RUNTIME_LIBC = ../runtime/libc.a

all: lua.elf

lua.elf: $(LUA_OBJS) $(RUNTIME_CRT0) $(RUNTIME_LIBC)
	$(CC) $(LDFLAGS) $(RUNTIME_CRT0) $(LUA_OBJS) $(RUNTIME_LIBC) -lgcc -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(LUA_OBJS) lua.elf

.PHONY: all clean

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
TARGET = rv32-run

# Architecture detection: pick the JIT backend.
# macOS reports `arm64`, Linux reports `aarch64` — normalize.
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),arm64)
    UNAME_M := aarch64
endif
ifeq ($(UNAME_M),x86_64)
    ARCH_SRC = dbt_x64.c
    ARCH_HDR = emit_x64.h
else ifeq ($(UNAME_M),aarch64)
    ARCH_SRC = dbt_a64.c
    ARCH_HDR = emit_a64.h
else
    $(error Unsupported host architecture: $(UNAME_M))
endif

SRCS = main.c elf_loader.c interp.c dbt_common.c shadow.c $(ARCH_SRC)
OBJS = $(SRCS:.c=.o)

all: $(TARGET)
	@echo "Built for $(UNAME_M)"

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGET)

# Dependencies
main.o:        main.c elf_loader.h interp.h dbt.h
elf_loader.o:  elf_loader.c elf_loader.h
interp.o:      interp.c interp.h decoder.h elf_loader.h
dbt_common.o:  dbt_common.c dbt.h decoder.h elf_loader.h
dbt_x64.o:     dbt_x64.c dbt.h decoder.h emit_x64.h elf_loader.h
dbt_a64.o:     dbt_a64.c dbt.h decoder.h emit_a64.h elf_loader.h
shadow.o:      shadow.c shadow.h dbt.h decoder.h elf_loader.h

# Stage 00 bootstrap interpreter for RV32IMFD.
#
# Mirrors slow-32/selfhost/stage00: when the fast DBT exists, the
# rv32-emu binary becomes a symlink to it; otherwise we compile a
# pure-interpreter binary from rv32-emu.c. Either way `./rv32-emu`
# at the stage00 path runs the same workloads.

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11 -pedantic
TARGET   = rv32-emu
DBT      = ../../dbt/rv32-run
SRC      = rv32-emu.c
DEPS     = $(SRC) ../../dbt/elf_loader.c ../../dbt/elf_loader.h \
           ../../dbt/interp.c ../../dbt/interp.h ../../dbt/decoder.h

all: $(TARGET)

$(TARGET): $(DEPS)
	@if [ -x $(DBT) ]; then \
		echo "Found fast DBT at $(DBT), symlinking..."; \
		ln -sf $(DBT) $(TARGET); \
	else \
		echo "Building bootstrap interpreter from source..."; \
		$(CC) $(CFLAGS) -lm -o $@ $(SRC); \
	fi

clean:
	rm -f $(TARGET)

# Smoke test: run a known-good ELF through whatever rv32-emu currently is
# (interpreter or DBT symlink) and check it exits cleanly.
test: $(TARGET) ../../runtime/libc.a
	@echo "=== Stage 00 bootstrap interpreter smoke test ==="
	@if [ ! -f ../../tests/test-string.elf ]; then \
		$(MAKE) -C ../../tests >/dev/null; \
	fi
	@./$(TARGET) ../../tests/test-string.elf >/tmp/stage00-out 2>&1 \
		&& grep -q "0 failed" /tmp/stage00-out \
		&& echo "PASS: test-string.elf ran clean" \
		|| { echo "FAIL: stage00 smoke test"; cat /tmp/stage00-out; exit 1; }

../../runtime/libc.a:
	$(MAKE) -C ../../runtime

.PHONY: all clean test

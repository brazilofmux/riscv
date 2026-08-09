CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =

# Cross-compiler for guest programs
RISCV_CC = riscv64-unknown-elf-gcc
RISCV_CFLAGS = -march=rv32imfd -mabi=ilp32d -nostdlib -O2

.PHONY: all clean dbt runtime test test-all clean-all

PORTS = lua lisp sbasic prolog zork nano dbase forth

all: dbt

dbt:
	$(MAKE) -C dbt

runtime:
	$(MAKE) -C runtime

test: dbt runtime
	$(MAKE) -C tests
	bash tests/run-tests.sh

# Full sweep across port suites that ship a run-tests.sh.
# Builds dbt/runtime first; each port is responsible for its own ELF.
test-all: test
	@for p in $(PORTS); do \
	  if [ -x $$p/tests/run-tests.sh ]; then \
	    echo "==== $$p ===="; \
	    (cd $$p && bash tests/run-tests.sh) || exit 1; \
	  fi; \
	done

clean:
	$(MAKE) -C dbt clean
	$(MAKE) -C runtime clean
	$(MAKE) -C tests clean

clean-all: clean
	@for p in $(PORTS) forth; do \
	  if [ -f $$p/Makefile ]; then $(MAKE) -C $$p clean 2>/dev/null || true; fi; \
	done

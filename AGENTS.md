# Repository Guidelines

## Project Structure & Module Organization
Lightweight **RV32IMFD** execution stack: guest binaries on a freestanding
libc, host runner with interpreter + dynamic binary translation.

- `dbt/`: host tool (`rv32-run`) — ELF load, interpreter, JIT (x64/a64),
  shadow verifier (`-V`), shared ECALL layer.
- `runtime/`: guest linker script, crt0, headers (`runtime/include/`),
  libc sources (`runtime/src/`).
- `examples/`: bare-metal and libc bring-up programs (`hello.c`,
  `benchmark_core.c`, `test_fp.c`, …).
- `tests/`: core runtime regression suite (8 tests).
- Ports with their own suites: `lua/`, `lisp/`, `sbasic/`, `prolog/`,
  `zork/`, `nano/`, `dbase/`, `forth/`.
- `docs/`: design/write-ups. `CLAUDE.md` is the detailed architecture status.

Keep generated artifacts (`*.o`, `*.elf`, `*.a`, `rv32-run`) out of version
control (see `.gitignore`). Rebuild ported ELFs after runtime/ECALL changes.

## Build, Test, and Development Commands
- `make` or `make -C dbt`: build `dbt/rv32-run` (arch picked via `uname -m`).
- `make -C runtime`: build guest `crt0.o` + `libc.a`.
- `make test`: core runtime suite (`tests/run-tests.sh`).
- Port suites: `bash <port>/tests/run-tests.sh` after building that port.
- Example guest compile (with libc):
  ```
  riscv64-unknown-elf-gcc -march=rv32imfd -mabi=ilp32d -O2 \
      -ffreestanding -nostdlib -Iruntime/include \
      -T runtime/link.ld runtime/crt0.o examples/hello.c \
      runtime/libc.a -lgcc -o examples/hello.elf
  ```
- Bare-metal benchmark (own `_start`, no crt0):
  ```
  riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib -O2 \
      -T runtime/link.ld examples/benchmark_core.c \
      -o examples/benchmark_core.elf
  ```
- Run: `./dbt/rv32-run examples/hello.elf`
  - `-i` interpreter, `-s` stats, `-V` lockstep shadow verify.

## Coding Style & Naming Conventions
C11-style, warning-clean with existing flags (`-Wall -Wextra -O2 -g`).

- Indentation: 4 spaces, no tabs.
- Naming: `snake_case` for functions/variables; `UPPER_CASE` for macros.
- Pair new modules with matching headers; keep decode/emit helpers small.

## Testing Guidelines
1. `make -C dbt && make -C runtime && make test`
2. Smoke: `./dbt/rv32-run examples/benchmark_core.elf` (and `-i` for ECALL parity).
3. For JIT correctness sweeps: `./dbt/rv32-run -V <elf>` (forces unchained blocks).
4. Performance: wall time + interpreter `-s` icount for BIPS; quote machine/date.

## Commit & Pull Request Guidelines
Short, imperative, technically specific (e.g. `dbt: fix jalr cache miss handling`).

- One logical change per commit.
- PRs: purpose, key notes, commands run, output deltas; call out RV32IMFD /
  no-RVC / ECALL assumptions when relevant.

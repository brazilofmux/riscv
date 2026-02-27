# Repository Guidelines

## Project Structure & Module Organization
This repository contains a lightweight RV32IM execution stack centered on `dbt/`.

- `dbt/`: host-side runner (`rv32-run`) with ELF loading, interpreter fallback, and DBT pipeline.
- `runtime/`: guest linker script plus runtime headers/sources under `runtime/include/` and `runtime/src/`.
- `examples/`: guest programs used for bring-up and benchmarking (`hello.c`, `benchmark_core.c`, `test_calls.c`).
- `forth/`: minimal RV32IM Forth kernel (`kernel.s`, `prelude.fth`) and its own build/run flow.
- `docs/` and `scripts/`: reserved for documentation and helper tooling.

Keep generated artifacts (`*.o`, `*.elf`, `rv32-run`) out of version control.

## Build, Test, and Development Commands
- `make` or `make -C dbt`: build the host binary translator/interpreter (`dbt/rv32-run`).
- `make -C dbt clean`: remove DBT objects and binary.
- `make -C forth`: assemble/link the Forth kernel ELF.
- `make -C forth run`: pipe `forth/prelude.fth` into the kernel via `../dbt/rv32-run`.
- Example guest compile:
  `riscv32-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib -T runtime/link.ld examples/hello.c -o examples/hello.elf`
- Example execution:
  `./dbt/rv32-run examples/hello.elf` (add `-i` for interpreter mode, `-s` for stats).

## Coding Style & Naming Conventions
Use C11-style, warning-clean code with the existing flags (`-Wall -Wextra -O2 -g`).

- Indentation: 4 spaces, no tabs.
- Naming: `snake_case` for functions/variables; `UPPER_CASE` for macros/constants.
- Keep headers focused (`*.h`) and pair new modules with matching source files.
- Prefer small, explicit helpers for decode/execute logic and bounds checks.

## Testing Guidelines
There is no formal test framework yet. Validate changes with runnable guest binaries:

1. Build: `make -C dbt`
2. Run smoke tests: `./dbt/rv32-run examples/hello.elf`
3. Cross-check behavior in interpreter mode: `./dbt/rv32-run -i examples/hello.elf`
4. For performance-sensitive changes, compare `-s` stats before/after.

Add new regression inputs under `examples/` with descriptive names (for example, `test_branch_edges.c`).

## Commit & Pull Request Guidelines
Follow the existing commit tone: short, imperative, technically specific (example: `dbt: fix jalr cache miss handling`).

- Keep commits scoped to one logical change.
- PRs should include: purpose, key implementation notes, validation commands run, and observed output deltas.
- Link related issues and call out ISA/ABI assumptions (RV32IM, no RVC) when relevant.

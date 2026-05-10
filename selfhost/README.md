# Selfhost Workspace (RV32IMFD)

Inspired by `~/slow-32/selfhost`. Right now this tree contains only
the trust-root interpreter (`stage00`); higher stages will arrive as
the corresponding pieces of slow-32 are adapted to RV32IMFD.

| Stage    | Role                                                               | Status |
|----------|--------------------------------------------------------------------|--------|
| stage00  | Pure-C reference interpreter, the trust root.                      | done   |
| stageNN  | (future) self-hosting C compiler, runtime additions, bootstrap chain. | n/a |

## Why mirror slow-32's layout

`slow-32` builds a complete cleanroom toolchain on top of its
trust-root emulator (assembler, archiver, linker, C compiler, libc),
verified at every stage. We're not committed to that whole climb here
— GCC/clang already produce RV32IMFD ELFs perfectly well — but the
shape is useful for two reasons:

1. **Trust root.** Every higher-stage tool we eventually add can be
   re-verified by running it under `stage00/rv32-emu`, which depends
   only on `dbt/interp.c` + `dbt/elf_loader.c` + a stock C compiler.
2. **DBT exercise.** Anything that runs as a guest binary under
   `rv32-run` is a heavier and more diverse test workload than our
   targeted regression suite.

See `stage00/README.md` for the per-stage details.

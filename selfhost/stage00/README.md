# Stage 00: Minimal Bootstrap Interpreter

The trust root for any future bootstrap work that runs RV32IMFD code
without trusting the JIT. Mirrors `~/slow-32/selfhost/stage00` in
spirit:

- `rv32-emu.c` is a single translation unit that pulls
  `dbt/elf_loader.c` and `dbt/interp.c` in via relative `#include`.
  No JIT, no shadow, no MAP_JIT plumbing — just the C reference
  interpreter plus a minimal `setup_guest_args` and `main`.
- The Makefile checks for `dbt/rv32-run` and substitutes a symlink
  when the fast path exists. Same trick `slow-32/selfhost/stage00`
  uses; means `./rv32-emu` always reaches the best available
  emulator without changing any callers.

## Build

```
make -C selfhost/stage00
```

If `dbt/rv32-run` was already built, this just makes `rv32-emu` a
symlink. Otherwise it compiles a slow-but-auditable interpreter
from one C file.

## Test

```
make -C selfhost/stage00 test
```

Runs `tests/test-string.elf` through whatever `rv32-emu` resolves
to. Builds the runtime first if needed.

## Why have it at all

When we eventually want a bootstrap chain — slow-32-style stages
01..N — the verification story depends on a trust-root emulator
that does not depend on the JIT. This is that.

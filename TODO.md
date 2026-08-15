# TODO — RV32IMFD DBT Future Work

Status snapshot: RV32IMFD JIT (x64 + a64), interpreter, shadow verify (`-V`),
21 ECALLs, freestanding libc, 8 ported programs. See `CLAUDE.md` for measured
performance and the full checklist of what is already done.

## Performance

### AArch64 refinements (speculative)
Chained-exit dispatch on Apple Silicon is already ~9 host insns; RAS, diamond
merge, and LUI+JALR/LOAD/STORE fusion all regressed when ported. Still open:

- **FP self-loop warm-entry**: pre-load FP regs at warm_entry so the back-edge
  can skip the FP flush. Current benches use more than 8 FP regs, so the
  eligibility check never fires — needs a real workload.
- **Hardware RAS** via guest call/ret → host BLR/RET through per-function
  stubs (much larger change than the software RAS that was reverted).

### x86-64

- **Register cache expansion**: 8-slot LRU is the main bottleneck on
  register-heavy loops; no free GPRs left without XMM tricks.
- **Peephole pass** (never implemented): redundant mov elimination, lea fold,
  strength-reduce imul-by-power-of-2, dead guest-reg stores before block exit.

### Profiling

- Optional per-PC block counters for hot-loop reporting / PGO.
- Optional guest instruction counter in DBT mode (today only the interpreter
  prints icount via `-s`; BIPS numerators should quote `-i -s`).

## ISA Extensions

| Extension | Status |
|-----------|--------|
| M (mul/div) | Done |
| F / D (float) | Done (interp + both JITs; fflags not set by arithmetic) |
| A (atomics) | Not needed for single-threaded micro profile |
| C (compressed) | Not supported — decoder is fixed 4-byte; ELF rejects RVC |

FCLASS and FCVT.W{,U}.{S,D} use shared soft helpers in both JITs (see
`rv32_fclass_*` / `rv32_fcvt_*` in `dbt_common.c`). Soft FP edge cases that
still diverge host-native vs interp: FMIN/FMAX NaN/-0.

## Runtime / ECALL

Done recently:

- Shared `rv32_handle_ecall` for JIT + interpreter (open flags, AT_FDCWD,
  dirops, nanosleep, fstat).
- Real `fstat` (ECALL 80) + richer `struct stat` marshal (ino/mode/size/times).
- `sleep` / `usleep` via nanosleep (no longer no-ops).

Still stubbed / incomplete (microcontroller profile unless a port needs them):

| API | Notes |
|-----|--------|
| `rename` | always -1; dbase falls back to copy+delete |
| `rmdir` / `chdir` / `access` | always -1 |
| `getcwd` | always returns `"/"` |
| `rewinddir` | no-op |
| `dirfd` | returns DIR table slot, not host fd |
| `brk` | returns 0 (malloc is self-managed) |
| guest `errno` | never set from ECALLs |
| `freopen` | does not rebind the existing `FILE*` |
| path ECALLs | `unlinkat`/`mkdirat` ignore dirfd; always host CWD |

## Tooling

- Hybrid DBT+trace mode (per-block PC stream without full `-i` cost).
- Root `make test-all` over port suites; portable `timeout` for macOS.
- Optional `-i` / `-V` matrix in test harnesses.
- Header rebuild deps for port Makefiles (stale `.o` after runtime header change).

## Ports (ideas)

- SQLite, MicroPython, a small C compiler, Dhrystone/CoreMark for public
  comparisons. Existing ports: Lua, SBASIC, Lisp, Prolog, MojoZork, nano,
  dBASE III, Forth.

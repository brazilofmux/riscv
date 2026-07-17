# CLAUDE.md - RV32IMFD Microcontroller DBT

## What This Is

A lightweight, high-performance dynamic binary translator for **RV32IMFD** (RISC-V 32-bit, Integer + Multiply/Divide + Single/Double Float) targeting microcontroller-class binaries. Think of it as a fast, portable execution environment: compile your C/C++ code for RV32IMFD, run it at near-native speed on any host (x86-64, AArch64). On unsupported hosts, the binary falls back to the interpreter.

This project is a spiritual successor to the SLOW-32 project at `~/slow-32`. All the techniques and lessons from that project apply here, but we use the standard RISC-V toolchain instead of a custom one.

## Why This Exists

- **No custom toolchain needed** — uses upstream GCC/clang with `-march=rv32imfd -mabi=ilp32d`
- **No QEMU needed** — purpose-built DBT is ~3-6× faster than `qemu-riscv32` user-mode on sustained CPU workloads, and ~3× faster to start (measured on Apple Silicon, see "Benchmark vs QEMU" below)
- **Tiny footprint** — the DBT + runtime is all you ship. No build tree, no dependencies beyond a host C compiler
- **Portable binaries** — one RV32IMFD ELF runs on every platform the DBT supports
- **ECALL service model** — clean host interface via Linux-style syscall numbers

## Architecture

### Memory Model
- Single flat address space (no MMU, no privilege modes)
- Code: 0x10000, up to 1 MB. Data: 0x80000, up to 16 MB
- Stack grows down from top of data region (0x0FFFFFF0)
- W^X: code segment is read-execute, data is read-write

### Host Services via ECALL
Guest programs invoke host services through the RISC-V `ecall` instruction using Linux-style syscall numbers in register a7. Supported ECALLs:

| ECALL | Name | Arguments |
|-------|------|-----------|
| 34 | mkdirat | a1=path, a2=mode |
| 35 | unlinkat | a1=path |
| 46 | ftruncate | a0=fd, a1=length |
| 56 | openat | a0=dirfd, a1=path, a2=flags (guest Linux O_* translated to host), a3=mode |
| 57 | close | a0=fd |
| 62 | lseek | a0=fd, a1=offset, a2=whence |
| 63 | read | a0=fd, a1=buf, a2=len |
| 64 | write | a0=fd, a1=buf, a2=len |
| 79 | fstatat | a1=path, a2=statbuf, a3=flags |
| 80 | fstat | stub, returns -1 |
| 90 | opendir | a0=path → directory handle (>=0) or -1 |
| 91 | readdir | a0=handle, a1=struct dirent buf → 0 / -1 |
| 92 | closedir | a0=handle |
| 93 | exit | a0=exit_code |
| 101 | nanosleep | a0=req timespec, a1=rem timespec or 0 |
| 214 | brk | stub, returns 0 (malloc is self-managed) |
| 403 | clock_gettime | a0=clockid, a1=tp_addr |
| 404 | get_cpu_clock | returns host clock() |
| 500 | term_setraw | a0=mode (1=raw, 0=cooked) |
| 501 | term_getsize | a0=buf_addr (writes rows, cols as 2x uint32) |
| 502 | term_kbhit | returns 1 if key available, 0 otherwise |

### Binary Validation
The DBT accepts standard RV32IMFD ELF binaries but validates:
- Must be ELF32, little-endian, machine EM_RISCV
- Must be RV32 (ELF flags)
- No privileged instructions (CSR used only for fcsr/fflags/frm, ECALL/EBREAK for service calls)
- No atomics (A extension) unless we choose to support them later

### DBT Pipeline
1. **ELF Loader** (`elf_loader.c`) — parse ELF, map segments, validate RV32IM, extract symbol table
2. **Decoder** (`decoder.h`) — inline RV32IM instruction decoder
3. **Translator** (`dbt.c`) — guest-to-host code generation with:
   - 8-slot LRU integer register cache (RSI, RDI, R8-R11, R14, R15)
   - FP translation via XMM0/XMM1 scratch (NaN-boxed single, native double)
   - Instruction fusion (LUI+ADDI, AUIPC+ADDI, AUIPC+JALR, AUIPC+load/store, SLT+branch)
   - Self-loop optimization with warm entry (register pressure guarded)
   - Diamond merge for short forward branches (up to 16 bytes)
   - Superblocks with side-exit snapshots (up to 8 side exits)
   - Block chaining via inline cache probes (R13 = cache base)
   - Return address stack (RAS) prediction for JALR returns
   - Intrinsic stubs for memcpy, memmove, memset, strlen (calls host native)
4. **Interpreter** (`interp.c`) — reference implementation, used with `-i` flag
5. **Block Cache** — direct-mapped hash table (64K entries x 16 bytes)

### Host Register Convention
**x86-64 backend (`dbt_x64.c`, full optimizations):**
- RBX = pointer to `rv32_ctx_t` (guest register file)
- R12 = guest memory base pointer
- R13 = block cache base pointer
- RAX, RCX, RDX = scratch (used by mul/div, cache probes)
- RSI, RDI, R8-R11, R14, R15 = register cache slots

**AArch64 backend (`dbt_a64.c`, baseline + chaining + intrinsics + LUI/AUIPC fusion + register cache):**
- X19 = pointer to `rv32_ctx_t`
- X20 = guest memory base pointer
- X21 = block cache base pointer
- X22-X28 = 7 LRU register cache slots (callee-saved, preserved by trampoline)
- X15 = 8th LRU register cache slot (caller-saved — safe within a block;
  intrinsic stubs that BLR don't share cache state with regular blocks)
- X9-X12 = scratch (computation, address calculation, cache probes)
- X14 = JALR target staging
- D0-D2 = FP scratch
- (No FP register cache yet — every FP access goes through ctx memory.)

The two backends share `dbt_common.c` (block cache, ECALL handler, run loop,
init/cleanup). The Makefile picks the right per-arch translator via
`uname -m`.

## RV32IMFD Quick Reference

### Integer Registers (32 x 32-bit)
- x0 = zero (hardwired)
- x1 = ra (return address)
- x2 = sp (stack pointer)
- x3 = gp (global pointer)
- x4 = tp (thread pointer — unused in microcontroller mode)
- x5-x7 = t0-t2 (temporaries)
- x8 = s0/fp (frame pointer)
- x9 = s1 (callee-saved)
- x10-x11 = a0-a1 (args/return values)
- x12-x17 = a2-a7 (args)
- x18-x27 = s2-s11 (callee-saved)
- x28-x31 = t3-t6 (temporaries)

### FP Registers (32 x 64-bit, NaN-boxed for single-precision)
- f0-f7 = ft0-ft7 (FP temporaries)
- f8-f9 = fs0-fs1 (FP callee-saved)
- f10-f11 = fa0-fa1 (FP args/return values)
- f12-f17 = fa2-fa7 (FP args)
- f18-f27 = fs2-fs11 (FP callee-saved)
- f28-f31 = ft8-ft11 (FP temporaries)

### Instruction Formats
- R-type: register-register (ADD, SUB, MUL, DIV, etc.)
- I-type: register-immediate (ADDI, LW, JALR, etc.)
- S-type: store (SW, SH, SB)
- B-type: branch (BEQ, BNE, BLT, BGE, BLTU, BGEU)
- U-type: upper immediate (LUI, AUIPC)
- J-type: jump (JAL)

### Key Differences from SLOW-32
- 6 branch types (vs SLOW-32's BEQ/BNE only) — comparisons are in the branch itself
- No separate comparison instructions (no SEQ/SNE/SGT etc.)
- AUIPC (add upper immediate to PC) — enables PC-relative addressing
- ECALL/EBREAK for system interface
- Standard ELF format (no custom .s32x)
- MUL/MULH/MULHU/MULHSU for full multiply coverage (SLOW-32 only had MUL+MULH)
- DIV/DIVU/REM/REMU are real instructions (SLOW-32 used libcalls for 64-bit)

## Project Structure

```
riscv/
├── CLAUDE.md              # This file
├── TODO.md                # Future work and potential enhancements
├── Makefile               # Top-level build
├── dbt/                   # Dynamic binary translator (host tool)
│   ├── main.c             # Entry point, CLI, -i flag
│   ├── elf_loader.c/.h    # ELF parser, validator, symbol table
│   ├── decoder.h          # RV32IM instruction decoder (inline)
│   ├── emit_x64.h         # x86-64 code emitter primitives
│   ├── dbt.c/.h           # JIT translator, block cache, dispatch
│   └── interp.c/.h        # Reference interpreter
├── runtime/               # Guest-side runtime (RV32IM)
│   ├── crt0.s             # Startup code (argc/argv, BSS clear)
│   ├── link.ld            # Linker script (code@0x10000, data@0x80000)
│   ├── include/           # C headers (26 files: stdio.h, stdlib.h, dirent.h, etc.)
│   └── src/               # libc implementation (17 C modules + 4 asm)
├── examples/              # Test and benchmark programs
├── tests/                 # Core runtime regression tests (8 suites)
├── lua/                   # Lua 5.4 interpreter port (11 tests)
├── sbasic/                # SBASIC interpreter port (43 tests)
├── lisp/                  # Scheme interpreter port (17 tests)
├── prolog/                # Prolog interpreter port (16 tests)
├── zork/                  # MojoZork Z-Machine port (3 tests)
├── nano/                  # nano text editor port (43 tests)
├── dbase/                 # dBASE III clone port (102 tests)
├── forth/                 # Forth interpreter port (22 tests)
├── bench-vs-qemu.sh       # rv32-run vs qemu-riscv32 benchmark (podman)
└── docs/                  # Design documents
```

## Build & Run

```bash
# Build the DBT (host tool)
make -C dbt

# Build the runtime (guest libc)
make -C runtime

# Compile a guest program
riscv64-unknown-elf-gcc -march=rv32imfd -mabi=ilp32d -O2 -ffreestanding -nostdlib \
    -Iruntime/include examples/hello.c -T runtime/link.ld \
    runtime/crt0.o runtime/libc.a -lgcc -o hello.elf

# Run it (JIT mode)
./dbt/rv32-run hello.elf

# Run it (interpreter mode, for debugging)
./dbt/rv32-run -i hello.elf
```

## Runtime Library

The runtime provides a comprehensive freestanding libc:

- **stdio**: FILE buffering, fopen/fclose/fread/fwrite, printf family (full format support), fgets/fputs, fseek/ftell, sscanf
- **stdlib**: malloc/free/calloc/realloc, qsort/bsearch, atoi/strtol/strtod, exit/abort
- **string**: strlen, strcpy, strcmp, memcpy, memmove, memset, memcmp, strtok, strstr, strdup
- **math**: sin/cos/tan/asin/acos/atan/atan2, exp/log/pow/sqrt, sinh/cosh/tanh, ceil/floor/round (all software-implemented)
- **ctype**: isalpha, isdigit, isspace, toupper, tolower, etc.
- **time**: time(), clock(), gmtime(), localtime(), mktime(), strftime()
- **setjmp**: setjmp/longjmp (assembly)
- **term**: terminal raw mode, cursor positioning, keyboard polling (ANSI escape sequences)
- **dirent**: real opendir/readdir/closedir via ECALLs 90-92
- **unistd**: nanosleep/usleep/sleep (ECALL 101), unlink, ftruncate, mkdir, stat
- **dtoa**: David Gay's algorithm for precise float-to-string

Intentional stubs (acceptable for microcontroller profile): fstat-on-fd, brk, signal, locale.

Guest binaries statically link this libc, so **checked-out ELFs go stale
when the runtime or its headers change** (e.g. the dirent layout) —
rebuild the ported programs (`make -C lua`, `make -C dbase`, ...) after
touching runtime/ or the ECALL layer. The .elf files are gitignored.

## Key Design Decisions

1. **No system emulation** — user-mode execution with host services, not a virtual machine
2. **ECALL for all host I/O** — Linux-style syscall numbers, direct host delegation
3. **Validate on load** — reject binaries that use unsupported extensions
4. **Intrinsic interception** — memcpy/memset/memmove/strlen replaced with native host calls
5. **Reuse SLOW-32 techniques** — the translation patterns are proven, adapted for RISC-V

## Ported Programs

| Program | Tests | Optimization | Description |
|---------|-------|-------------|-------------|
| Lua 5.4 | 11 | -O2 | Full Lua interpreter with coroutines |
| SBASIC | 43 | -O2 | BASIC interpreter with file I/O |
| Lisp | 17 | -O2 | Scheme interpreter |
| Prolog | 16 | -O2 | Prolog interpreter |
| MojoZork | 3 | -O2 | Z-Machine interpreter (Infocom games) |
| nano | 43 | -O2 | Terminal text editor |
| dBASE III | 102 | -O2 | Database system with indexing |
| Forth | 22 | -O2 | Forth interpreter |

## Reference Material

- RISC-V ISA spec: https://riscv.org/specifications/
- SLOW-32 DBT source: `~/slow-32/tools/dbt/`
- SLOW-32 MMIO design: `~/slow-32/docs/SERVICE_NEGOTIATION.md`
- SLOW-32 runtime: `~/slow-32/runtime/`

## Current Status

- [x] Project scaffolding
- [x] ELF loader with RV32IM validation
- [x] RV32IM decoder
- [x] Interpreter (reference implementation)
- [x] Basic DBT (block-at-a-time translation)
- [x] Block chaining
- [x] Register caching (8-slot LRU)
- [x] Superblocks (side exits with register snapshots)
- [x] Instruction fusion (LUI/AUIPC pairs, SLT+branch)
- [x] Return address stack prediction
- [x] Intrinsic function interception (memcpy, memset, memmove, strlen)
- [x] Diamond merge for short forward branches
- [x] ECALL service layer (21 syscalls)
- [x] Runtime libc (21 modules, 26 headers)
- [x] RV32F/D floating-point extensions (interpreter + JIT)
- [x] FP test suite (50 tests)
- [x] 8 ported programs; full sweep 2026-07-16: core 8/8, lua 11/11,
      lisp 17/17, sbasic 42/43 (1 skip), prolog 16/16, zork 3/3,
      nano 43/43, dbase 102/102, forth 22/22 — 265 tests
- [x] AArch64 host backend (`dbt_a64.c`): trampoline, integer + FP
      translator, block chaining, intrinsic stubs, LUI/AUIPC fusion,
      SLT+branch fusion, 8-slot LRU integer register cache (X22-X28 +
      X15), superblocks with per-side-exit cache snapshots, self-loop
      back-edge optimization (warm-entry skips cold loads every
      iteration), 8-slot LRU FP register cache (D8-D15) for doubles,
      and native-libm intrinsic stubs for 20 transcendental functions
      (sin/cos/tan/exp/log/sqrt/pow/atan2/...). All suites pass
      (265 tests, 2026-07-16). benchmark_core: **~9.1-9.3 BIPS on
      a64** (2.494 G instructions in 0.27 s; 400M-iter run confirms at
      ~9.975 G in 1.06 s) — ~17x over the interpreter (~530 MIPS).

      vs slow32-dbt, measured like-for-like 2026-07-16 (identical
      kernels from examples/benchmark_core.c, BENCH_ITERS=100M, same
      Apple M5 Max, same hour):

          rv32-run     2,493,751,040 instr   0.274 s   9.10 BIPS
          slow32-dbt   2,850,025,393 instr   0.380 s   7.50 BIPS

      RV32 needs 12.5% fewer instructions for the same work, runs them
      21% faster, and finishes 39% sooner in wall time.

      DO NOT read that as an ISA result — it is not attributed, and at
      least three variables are uncontrolled:
        1. Compilers differ. RV32 here is gcc; slow-32 is its own
           in-tree LLVM backend. The instruction-count gap may be
           mostly codegen quality, not encoding density.
        2. The programs differ slightly. Same kernels (byte-identical
           C), but slow-32's build links a libc for printf (97 blocks
           translated) while ours is bare-metal ecall (36).
        3. The register-cache designs differ — and an earlier revision
           of this note wrongly called slow-32's a64 translator
           "incomplete" for lacking translate.c's loop pre-warm.
           RETRACTED: slow-32's translate_a64.c is a static prescan
           allocator (never evicts mid-block) that loads all 8 slots
           once in the block prologue and takes back-edges as a bare
           b.cond — no cold loads in the loop, nothing for a pre-warm
           to skip. Our dbt_a64.c is a lazy LRU and needs its
           warm_entry for the same reason slow-32's x64 needs its
           pre-warm. Different designs, neither owing the other's
           debts. Slow-32's ~9.5 (x86-64) vs ~6-7.5 (Apple) spread is
           cross-host AND cross-translator — it attributes nothing.
      What is real and same-host: our a64 DBT runs identical kernels
      21% faster per guest instruction than slow-32's a64 DBT
      (9.10 vs 7.50 BIPS, M5 Max, 2026-07-16). Cause unknown and
      unprofiled — and per-guest-instruction BIPS is itself awkward
      across guest ISAs (fused BEQ does more per instruction). Before
      anyone claims a reason: disassemble both emitted hot loops and
      count host instructions per iteration. An x86-64 run of both
      adds a second datapoint. Neither done.

      DO NOT compare BIPS against slow32-dbt's "9.2/9.5" figure: that
      is its **x86-64** number. On Apple Silicon slow32-dbt is 7.50,
      and slow-32's docs/EMULATORS.md "~6 BIPS (Apple Silicon)" is
      itself stale. Nor compare the two repos' checked-in
      benchmark_core binaries: same source file, but the defaults are
      10x apart (slow-32 BENCH_ITERS=10000000u, here 100000000u), and
      at 10M the slow-32 build spends 21% of its runtime in JIT
      warm-up (slow32-dbt -p: translate 0.010 s of a 0.047 s run).
      Rebuild both at the same BENCH_ITERS on the same host, or the
      comparison is meaningless. An earlier revision of this line
      claimed "parity with slow32-dbt's 9.2 BIPS" — it was wrong for
      all three reasons above.
      Other datapoints: lisp 17-stress 11× over interp,
      transcendental-heavy microbench ~8× over the no-stubs baseline,
      FP loops ~30% faster with the doubles cache.
      Tried and reverted (regressed on this
      host — chained-exit is already too cheap for the optimizations
      to pay off): RAS, diamond merge, LUI+JALR/LOAD/STORE fusion.
- [x] Lockstep shadow-interpreter verifier (`-V`, `shadow.c`/`shadow.h`):
      runs a pure-C interpreter on every block from a pre-block snapshot,
      captures stores in a shadow buffer, then compares regs/PC/FP/stores
      against the JIT's actual effects and aborts with diagnostics on
      first divergence. Forces unchained per-block dispatch (chaining,
      superblocks, and self-loop warm-entry are gated off when verify is
      on), so it cannot detect bugs that only manifest with self-loop
      enabled, but it *does* catch register-cache, fusion, and
      ALU/FP-translation bugs. ~54× slower than the unverified JIT;
      intended for development sweeps, not production runs. Verify
      sweep: core 8/8, lua 11/11, lisp 17/17.
- [x] Benchmark vs QEMU. `bench-vs-qemu.sh` runs both `rv32-run` and
      `qemu-riscv32` (qemu-user-mode 10.2.1) inside a podman container so
      VM overhead applies equally; both targets the host AArch64. Apple
      Silicon M-series, qemu-user installed via Ubuntu apt:
      | Workload                            | rv32-run | qemu     | speedup |
      | ----------------------------------- | -------- | -------- | ------- |
      | lisp 17-stress (CPU interp loop)    | 0.05 s   | 0.23 s   | 4.6×    |
      | lua full sweep (11 tests, mixed)    | 0.02 s   | 0.09 s   | 4.5×    |
      | empty.lua × 100 (startup)           | 0.15 s   | 0.50 s   | 3.3×    |
      | sbasic suite (43 tests, I/O+ALU)    | 0.18 s   | 0.18 s   | 1.0×    |
      | dbase 102 tests (I/O-heavy)         | 1.28 s   | 1.38 s   | 1.08×   |
      Compute-bound interpreter loops are 4-6× faster; I/O-bound suites
      are dominated by host-side syscall costs and converge.

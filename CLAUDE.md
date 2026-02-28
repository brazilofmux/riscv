# CLAUDE.md - RV32IM Microcontroller DBT

## What This Is

A lightweight, high-performance dynamic binary translator for **RV32IM** (RISC-V 32-bit, Integer + Multiply/Divide) targeting microcontroller-class binaries. Think of it as a fast, portable execution environment: compile your C/C++ code for RV32IM, run it at near-native speed on any host (x86-64, ARM64, RISC-V64).

This project is a spiritual successor to the SLOW-32 project at `~/slow-32`. All the techniques and lessons from that project apply here, but we use the standard RISC-V toolchain instead of a custom one.

## Why This Exists

- **No custom toolchain needed** — uses upstream GCC/clang with `-march=rv32im -mabi=ilp32`
- **No QEMU needed** — purpose-built DBT is ~1.5-2x faster on sustained workloads and much faster to start
- **Tiny footprint** — the DBT + runtime is all you ship. No build tree, no dependencies beyond a host C compiler
- **Portable binaries** — one RV32IM ELF runs on every platform the DBT supports
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
| 35 | unlinkat | a1=path |
| 46 | ftruncate | a0=fd, a1=length |
| 56 | openat | a0=dirfd, a1=path, a2=flags, a3=mode |
| 57 | close | a0=fd |
| 62 | lseek | a0=fd, a1=offset, a2=whence |
| 63 | read | a0=fd, a1=buf, a2=len |
| 64 | write | a0=fd, a1=buf, a2=len |
| 80 | fstat | stub, returns -1 |
| 93 | exit | a0=exit_code |
| 214 | brk | stub, returns 0 (malloc is self-managed) |
| 403 | clock_gettime | a0=clockid, a1=tp_addr |
| 404 | get_cpu_clock | returns host clock() |
| 500 | term_setraw | a0=mode (1=raw, 0=cooked) |
| 501 | term_getsize | a0=buf_addr (writes rows, cols as 2x uint32) |
| 502 | term_kbhit | returns 1 if key available, 0 otherwise |

### Binary Validation
The DBT accepts standard RV32IM ELF binaries but validates:
- Must be ELF32, little-endian, machine EM_RISCV
- Must be RV32 (ELF flags)
- No privileged instructions (CSR, ECALL/EBREAK used only for service calls)
- No atomics (A extension) unless we choose to support them later
- F/D extensions: optional, can be added incrementally

### DBT Pipeline
1. **ELF Loader** (`elf_loader.c`) — parse ELF, map segments, validate RV32IM, extract symbol table
2. **Decoder** (`decoder.h`) — inline RV32IM instruction decoder
3. **Translator** (`dbt.c`) — guest-to-host code generation with:
   - 8-slot LRU register cache (RSI, RDI, R8-R11, R14, R15)
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
- RBX = pointer to `rv32_ctx_t` (guest register file)
- R12 = guest memory base pointer
- R13 = block cache base pointer
- RAX, RCX, RDX = scratch (used by mul/div, cache probes)
- RSI, RDI, R8-R11, R14, R15 = register cache slots

## RV32IM Quick Reference

### Registers (32 x 32-bit)
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
│   ├── include/           # C headers (21 files: stdio.h, stdlib.h, etc.)
│   └── src/               # libc implementation (20 modules)
├── examples/              # Test and benchmark programs
├── tests/                 # Core runtime regression tests (8 suites)
├── lua/                   # Lua 5.4 interpreter port (11 tests)
├── sbasic/                # SBASIC interpreter port (43 tests)
├── lisp/                  # Scheme interpreter port (17 tests)
├── prolog/                # Prolog interpreter port (16 tests)
├── zork/                  # MojoZork Z-Machine port (3 tests)
├── nano/                  # nano text editor port (33 tests)
├── dbase/                 # dBASE III clone port (102 tests)
├── forth/                 # Forth interpreter port
├── scripts/               # Helper scripts
└── docs/                  # Design documents
```

## Build & Run

```bash
# Build the DBT (host tool)
make -C dbt

# Build the runtime (guest libc)
make -C runtime

# Compile a guest program
riscv64-unknown-elf-gcc -march=rv32im -mabi=ilp32 -O2 -ffreestanding -nostdlib \
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
- **dtoa**: David Gay's algorithm for precise float-to-string

Intentional stubs (acceptable for microcontroller profile): directory ops, sleep, signal, locale.

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
| nano | 33 | -O2 | Terminal text editor |
| dBASE III | 102 | -O2 | Database system with indexing |
| Forth | — | -O2 | Forth interpreter |

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
- [x] ECALL service layer (15 syscalls)
- [x] Runtime libc (20 modules, 21 headers)
- [x] 7 ported programs, 225+ tests passing
- [ ] Benchmark vs QEMU

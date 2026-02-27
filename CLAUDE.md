# CLAUDE.md - RV32IM Microcontroller DBT

## What This Is

A lightweight, high-performance dynamic binary translator for **RV32IM** (RISC-V 32-bit, Integer + Multiply/Divide) targeting microcontroller-class binaries. Think of it as a fast, portable execution environment: compile your C/C++ code for RV32IM, run it at near-native speed on any host (x86-64, ARM64, RISC-V64).

This project is a spiritual successor to the SLOW-32 project at `~/slow-32`. All the techniques and lessons from that project apply here, but we use the standard RISC-V toolchain instead of a custom one.

## Why This Exists

- **No custom toolchain needed** — uses upstream GCC/clang with `-march=rv32im -mabi=ilp32`
- **No QEMU needed** — purpose-built DBT is ~1.5-2x faster on sustained workloads and much faster to start
- **Tiny footprint** — the DBT + runtime is all you ship. No build tree, no dependencies beyond a host C compiler
- **Portable binaries** — one RV32IM ELF runs on every platform the DBT supports
- **MMIO service model** — clean host interface via memory-mapped ring buffers, not emulated hardware

## Architecture

### Memory Model
- Single flat address space (no MMU, no privilege modes)
- W^X protection: code segment is execute-only, data is read-write
- Stack grows down from top of data region
- MMIO window at a configurable base address for host services

### Host Services via MMIO
- Ring buffer protocol inherited from SLOW-32 (see `~/slow-32/docs/SERVICE_NEGOTIATION.md`)
- Services: filesystem, console I/O, time, environment, terminal
- Service negotiation: guest discovers and requests services at runtime
- Legacy gate: basic I/O always available regardless of policy

### Binary Validation
The DBT accepts standard RV32IM ELF binaries but validates:
- Must be ELF32, little-endian, machine EM_RISCV
- Must be RV32 (ELF flags)
- No privileged instructions (CSR, ECALL/EBREAK used only for service calls)
- No atomics (A extension) unless we choose to support them later
- F/D extensions: optional, can be added incrementally

### DBT Pipeline (from SLOW-32 experience)
1. **ELF Loader** — parse ELF, map segments, validate RV32IM profile
2. **Block Cache** — hash table mapping guest PC → translated native code
3. **Decoder** — RV32IM instruction decoder (much simpler than x86)
4. **Translator** — guest → host code generation with register allocation
5. **Block Chaining** — patch direct jumps between translated blocks
6. **Superblocks** — extend translation across branches for hot paths
7. **Peephole Optimizer** — clean up redundant instructions post-translation

## RV32IM Quick Reference

### Registers (32 × 32-bit)
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
- ECALL/EBREAK for system interface (we repurpose ECALL for MMIO or use memory-mapped I/O)
- Standard ELF format (no custom .s32x)
- MUL/MULH/MULHU/MULHSU for full multiply coverage (SLOW-32 only had MUL+MULH)
- DIV/DIVU/REM/REMU are real instructions (SLOW-32 used libcalls for 64-bit)

## Project Structure

```
riscv/
├── CLAUDE.md          # This file
├── Makefile           # Top-level build
├── dbt/               # Dynamic binary translator
│   ├── main.c         # Entry point, CLI
│   ├── elf_loader.c   # ELF parser and validator
│   ├── elf_loader.h
│   ├── decoder.c      # RV32IM instruction decoder
│   ├── decoder.h
│   ├── translate.c    # Guest → host translation
│   ├── translate.h
│   ├── block_cache.c  # Translated block management
│   ├── block_cache.h
│   ├── dbt.c          # Core dispatch loop
│   └── dbt.h
├── runtime/           # Guest-side runtime libraries
│   ├── include/       # C headers (stdio.h, stdlib.h, etc.)
│   ├── src/           # libc implementation
│   └── crt0.s         # RV32IM startup code
├── scripts/           # Helper scripts
│   ├── compile.sh     # Compile C → RV32IM ELF
│   └── run.sh         # Compile and run
├── examples/          # Test programs
│   └── hello.c
└── docs/              # Design documents
```

## Build & Run

```bash
# Build the DBT (host tool)
make

# Compile a guest program (uses system riscv32 cross-compiler)
riscv32-unknown-elf-gcc -march=rv32im -mabi=ilp32 -nostdlib -T runtime/link.ld \
    runtime/crt0.o examples/hello.c runtime/libc.a -o hello.elf

# Run it
./rv32-run hello.elf

# Or use the helper script
./scripts/compile.sh examples/hello.c hello.elf
./rv32-run hello.elf
```

## Key Design Decisions

1. **No system emulation** — this is user-mode execution with host services, not a virtual machine
2. **MMIO for all host I/O** — no syscall emulation, no signal handling, just memory-mapped ring buffers
3. **Validate on load** — reject binaries that use unsupported extensions or privileged instructions
4. **Start simple** — get RV32IM working first, add F/D extensions later
5. **Reuse SLOW-32 DBT techniques** — the translation patterns are proven, just adapt the decoder

## Reference Material

- RISC-V ISA spec: https://riscv.org/specifications/
- SLOW-32 DBT source: `~/slow-32/tools/dbt/`
- SLOW-32 MMIO design: `~/slow-32/docs/SERVICE_NEGOTIATION.md`
- SLOW-32 runtime: `~/slow-32/runtime/`
- SLOW-32 benchmarks: `~/slow-32/docs/benchmarks.md`

## Current Status

- [ ] Project scaffolding
- [ ] ELF loader with RV32IM validation
- [ ] RV32IM decoder
- [ ] Interpreter (reference implementation)
- [ ] Basic DBT (block-at-a-time translation)
- [ ] Block chaining
- [ ] Register caching
- [ ] Superblocks
- [ ] MMIO service layer
- [ ] Runtime libc
- [ ] Benchmark vs QEMU

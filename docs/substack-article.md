# RISC-V Is Already the Best Portable Bytecode. We Just Haven't Noticed.

I didn't set out to make this argument. I set out to build a toy CPU and got mugged by reality.

## The accidental discovery

A year ago, I started a project called SLOW-32 — a custom 32-bit RISC ISA with the entire stack built from scratch. I mean *everything*:

- A native LLVM backend so clang could target my ISA directly
- A custom assembler and linker with relocations, symbol tables, archive libraries
- A selfhosting bootstrap chain — Ken Thompson style, where a Forth kernel builds the assembler that builds the tools that compile C
- A QEMU system emulator target (full TCG backend, not a user-mode hack)
- Five emulators ranging from a 50 MIPS interpreter to a 6 BIPS JIT
- Docker containers for the toolchain and the emulator
- A freestanding libc, a runtime, test suites

It was a great education in ISA design, compiler engineering, and how all the pieces of a platform fit together. I ported Lua, a Scheme interpreter, Prolog, a BASIC interpreter, Forth, a Z-machine, a nano clone, a dBASE III clone — each one exercising a different corner of the system.

Then I had the moment that changed everything: *almost none of it mattered.*

The custom ISA was converging on RISC-V anyway. Every time I hit a pain point — comparison instructions that didn't compose well, immediate encoding that wasted bits, calling conventions that fought the register allocator — the fix moved the design closer to something that already existed. Fixed-width 32-bit encoding. Load-store architecture. 32 registers with x0 hardwired to zero. PC-relative addressing.

The custom LLVM backend couldn't match 30 years of GCC/LLVM optimization work on RISC-V. The custom assembler and linker didn't buy me anything over stock binutils. The custom binary format wasn't better than ELF. The selfhosting bootstrap was intellectually satisfying but practically irrelevant.

The only piece that was genuinely unique — the only piece you can't get off the shelf — was the dynamic binary translator.

So I threw it all away except the DBT. Pointed stock GCC at `-march=rv32im -mabi=ilp32`. Re-ported every program against the stock toolchain. The result was better in every way: better code generation, better debugging (GDB, objdump, readelf just work), better ecosystem (any RISC-V tutorial applies), and zero maintenance burden on the toolchain.

That's when the thesis crystallized. If you strip away everything that's already solved — the compiler, the binary format, the debug tooling — the only thing left to build is a fast translator from RISC-V to your host. And RISC-V makes that *easy*.

## What a portable bytecode needs

If you're going to compile code once and run it anywhere, your bytecode format needs specific properties:

**Easy to decode.** The translator's hot loop is fetch-decode-translate. If decoding is complex (variable-length instructions, prefix bytes, mode-dependent behavior), you pay for it on every instruction. RISC-V's encoding is fixed-width, regular, and can be fully decoded with a few shifts and masks. The entire decoder is 146 lines.

**Easy to translate.** The guest ISA should map cleanly onto host ISAs without awkward impedance mismatches. RISC-V's register-register operations, simple addressing modes, and explicit comparisons translate nearly 1:1 to x86-64, ARM64, and (trivially) native RISC-V.

**Good compiler support.** The best bytecode in the world is useless if the compiler targeting it produces bad code. RISC-V has production-quality GCC and LLVM backends with full optimization passes (-O0 through -O3, LTO, PGO). This is decades of engineering you get for free.

**Stable.** The format can't break between versions. RISC-V's base ISA (RV32I) is ratified and frozen. Extensions are additive. An RV32IM binary compiled today will be valid forever.

**Subsettable.** This is the one that matters most for a portable execution engine. RISC-V was designed from the start to be implemented in subsets. You don't implement "RISC-V" — you implement RV32IM (base integer + multiply/divide), and you have a fully functional compilation target. Need floating-point? Add F and D — a well-defined increment. Need atomics for threading? Add A. Each extension is small and self-contained. My DBT implements RV32IMFD and that covers every workload I've tested. Compare this to x86-64, where you can't meaningfully implement a subset, or ARM, where the "base" ISA is already enormous.

**Standard tooling.** Stock ELF format means stock `objdump`, stock `readelf`, stock GDB, stock symbol tables. No custom debug formats, no custom profilers, no custom build systems.

RISC-V has all of these. It wasn't designed as a portable bytecode — it was designed as a clean hardware ISA — but the properties that make good silicon also make good bytecode. And the deliberate modularity of the extension system means you can build a translator for the subset you need, not the whole spec.

## The numbers

The execution engine is a dynamic binary translator (DBT) — it JIT-compiles RISC-V basic blocks to x86-64 native code at runtime. The core techniques:

- **Register caching**: 8 guest registers pinned in host registers via LRU eviction, eliminating most memory traffic to the guest register file
- **Instruction fusion**: LUI+ADDI, AUIPC+ADDI, AUIPC+JALR, and SLT+branch pairs fused into single host operations
- **Superblocks**: Traces extend across basic block boundaries with side-exit snapshots, reducing dispatch overhead
- **Block chaining**: Translated blocks jump directly to each other through inline cache probes — no return to the dispatch loop
- **Return address stack**: Predicts JALR return targets, avoiding indirect branch mispredictions on function-heavy code
- **Diamond merge**: Short forward branches (if/else under 16 bytes) are converted to branchless conditional sequences

The result: approximately 6.2 billion emulated instructions per second on a modern x86-64 host. That's about 70% of native execution speed.

Compared to QEMU user-mode (`qemu-riscv32-static`) on the same workloads, the DBT is 1.5–4.5x faster. The 1.5x floor is tight computational loops where both translators generate decent straight-line code and the bottleneck is the computation itself. The 4.5x ceiling is dispatch-heavy code — interpreters, complex control flow, lots of indirect calls — where block chaining and return prediction eliminate the overhead that QEMU absorbs on every block transition.

The entire translator is about 5,000 lines of C. It compiles in under a second.

## Why QEMU can't close the gap

This isn't a knock on QEMU — it's a brilliant piece of engineering. But its architecture makes certain optimizations structurally impossible in user-mode.

**Memory access overhead.** QEMU's Tiny Code Generator (TCG) was designed for full-system emulation with virtual memory. Every guest memory access goes through a software TLB lookup, even in user-mode where the guest address space is a simple flat mapping. The user-mode path inherits the system emulation machinery; there's no separate fast path. In my DBT, guest memory access is `[R12 + guest_addr]` — a single host instruction with a base register. No TLB, no page table, no protection check.

**Block transition overhead.** QEMU's block chaining is conservative because it must handle self-modifying code, cross-page jumps, and signal delivery at block boundaries. My DBT knows the code segment is W^X (write-xor-execute) — once translated, a block never changes. Direct jumps are patched inline. Returns use a predicted stack. The dispatch loop is a fallback, not the common path.

**Register pressure.** TCG uses a virtual register model that spills to memory at block boundaries. My DBT maintains an LRU register cache that persists across block chains — a hot guest register stays in a host register across dozens of translated blocks without touching memory.

These aren't missing optimizations. They're the unavoidable cost of QEMU's actual mission: emulating an entire computer. MMU, privilege modes, self-modifying code, multi-hart, device models. Even `qemu-riscv32-static` inherits this architecture — user-mode is a thin layer on top of the system emulation engine, not a separate design.

We aren't emulating a computer. We're translating user-mode code with flat memory and a handful of host service calls. That's a fundamentally different problem, and it admits fundamentally simpler solutions.

## Why not WebAssembly?

WebAssembly was explicitly designed as a portable compilation target. So why not use it?

**Stack-based execution model.** Wasm is a stack machine. Translating it to register-based host hardware requires a register allocation pass that RISC-V translation doesn't need — the guest registers map directly to host registers.

**Heavier runtime.** A minimal Wasm runtime (Wasmtime, Wasmer, wasm3) is tens of thousands of lines of code and requires a non-trivial embedding API. My RISC-V DBT is 5,000 lines with no dependencies beyond a host C compiler.

**Toolchain friction for bare-metal.** Compiling freestanding C to Wasm is possible but not the primary use case. The Wasm ecosystem is oriented around web browsers and WASI, not microcontroller-style bare-metal code. Compiling to RISC-V ELF is just `gcc -march=rv32imfd -nostdlib` — the toolchain was built for exactly this.

**No standard debug format.** DWARF works with ELF. Wasm has its own debug story that's still evolving.

Wasm solves a different problem well — sandboxed execution in browsers and cloud edge. But as a general portable bytecode for native code, RISC-V is simpler, faster to translate, and better supported by existing toolchains.

## Why not LLVM bitcode?

I built an LLVM backend for SLOW-32. I know this path intimately. LLVM IR is the closest competitor to "portable bytecode" — it's the universal intermediate representation that every language frontend targets.

But LLVM bitcode is explicitly *not* a stable format. The LLVM project documents that bitcode is version-specific and not suitable for distribution. An LLVM 17 bitcode file may not load in LLVM 19. I hit this myself — LLVM API changes broke my backend between releases, requiring non-trivial porting work.

RISC-V machine code is the *output* of LLVM's optimization and lowering pipeline. You get all the optimization passes, all the register allocation, all the instruction selection — and then freeze the result in a stable, version-independent format. You don't skip the backend; you run it once, at compile time, and distribute the result. The backend is someone else's problem forever.

## What we haven't done yet

**Threads.** The A (atomic) extension adds load-reserved/store-conditional and atomic read-modify-write operations. We haven't implemented them yet because single-threaded execution covers every workload we've tested. But the approach *generalizes* — a thread is just another context (register file, PC, stack pointer), and the translation pipeline doesn't care how many are running. The hard part is shared memory semantics, and here RISC-V does us another favor: its weak memory ordering (RVWMO) is automatically satisfied by x86-64's stronger TSO model. LR/SC maps to CMPXCHG loops, AMO instructions map to LOCK-prefixed operations, FENCE becomes a no-op. It's maybe 10 new instructions to translate. We just haven't needed it.

**ARM64 host backend.** The current emitter targets x86-64 only. An ARM64 backend would unlock Apple Silicon and Raspberry Pi. The register cache, block chaining, and superblock architecture are host-agnostic — only the ~1,000-line emitter needs porting. ARM64's larger register file would actually allow expanding the cache from 8 to 16+ slots.

**Compressed instructions.** The C extension adds 16-bit compressed instructions that significantly reduce code size. The decoder would need to handle variable-length fetch, but the translation pipeline is otherwise unchanged.

None of these are architectural barriers. The design doesn't preclude them — we just haven't needed them yet.

## The validation

I've ported 8 real programs to verify this isn't just a microbenchmark story:

| Program | Description | Tests |
|---------|-------------|-------|
| Lua 5.4 | Full Lua interpreter | 11 |
| Forth | Forth interpreter | — |
| Scheme | Scheme interpreter | 17 |
| Prolog | Prolog interpreter | 16 |
| SBASIC | BASIC interpreter | 43 |
| MojoZork | Z-Machine (Infocom games) | 3 |
| nano | Terminal text editor | 33 |
| dBASE III | Database with indexing | 102 |

That's 283 tests across 8 programs, all compiled with stock GCC at -O2, all running on a freestanding libc (~10K lines) that provides stdio, stdlib, string, math, time, and terminal I/O through a Linux-style ECALL interface.

These aren't toy programs. Lua has coroutines and a full garbage collector. The dBASE clone handles B-tree indexing with multi-field keys. The nano clone does ANSI terminal rendering with syntax highlighting. They exercise function calls, memory allocation, file I/O, floating-point math, and complex control flow.

## The implication

Here's the thought experiment that changed my perspective:

RISC-V might or might not win the silicon war against ARM. That's a business question about ecosystem, licensing, and manufacturing. But RISC-V has *already won* as an ISA specification — the same way PDF won as a document format regardless of which printer you own.

If you want to compile a C program once and run it on any host at near-native speed, RISC-V user-mode ELF is the simplest path that exists today. Stock compiler, stock binary format, stock debugger, simple translator. No language runtime, no virtual machine, no version instability.

The ISA is the universal adapter between compilers and hardware. It doesn't need to run on RISC-V silicon to be useful — it just needs to be easy to translate. And it is. That's what it was designed for.

## What's next

The code is going public soon. Everything — the DBT, the runtime library, the ported programs, the test suites — in a single repository. If you want to try compiling your own C code for RV32IMFD and running it at 70% native speed on your laptop, you'll be able to.

I'm also working on the ARM64 host backend and a proper benchmark suite for the QEMU comparison. If anyone has opinions on which benchmarks would be most convincing, I'm listening.

— Stephen

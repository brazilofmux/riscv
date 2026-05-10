# TODO — RV32IM DBT Future Work

## Performance

### Benchmark vs QEMU
The one remaining checklist item. Now that 7 real programs are ported, we have meaningful workloads to measure. Key metrics: startup time, sustained throughput (Lua benchmarks, dBASE bulk operations), and code size overhead.

### AArch64 host backend (baseline shipped — needs the rest of the optimizations)
Done: trampoline, full RV32IMFD integer + FP translator (`dbt_a64.c` plus
`emit_a64.h` instruction emitters), block chaining via inline cache,
intrinsic native stubs (memcpy/memset/memmove/strlen), LUI/AUIPC + ADDI
fusion, SLT+branch fusion (correct but inert on current GCC output),
8-slot LRU integer register cache (X22-X28 + X15), superblocks with
per-side-exit cache snapshots, and self-loop back-edge optimization
(warm-entry skips cold loads every iteration). All 314 tests pass
under JIT on aarch64; benchmark_core is ~13x over interpreter
(~5.3 BIPS, up from 1.5 before the cache); lisp 17-stress is ~11x.

The pattern that emerged across this work: AArch64's chained-exit
dispatch is so tight (~9 host instructions, very predictable BR
target) that any optimization which trades register-cache flushing/
reset for "skip dispatch" comes out negative — RAS and diamond merge
both regressed on measured workloads. Optimizations that *preserve*
cache state (superblocks via snapshots, back-edge to warm_entry) are
the ones that pay off.

Still to do, in roughly priority order:
- **AUIPC+JALR fusion** — direct call to known target, like JAL chained.
- **AUIPC+load/store fusion** — known address as imm offset.
- **FP register cache** — the 8 callee-saved D-registers (D8-D15) are
  unused; a small LRU for FP would help the FP-heavy tests.

Tried and reverted: **RAS for JALR returns**. A faithful port of the x86
RAS — push at JAL/JALR with rd=ra, pop+compare at JALR ret, skip the
next_pc store on prediction hit — measured a regression on this AArch64
(benchmark_core +5%, lisp 17-stress +28%). The chained-cache probe is
already so tight (~9 host instructions) that skipping the single
next_pc STR doesn't pay for the ~7-instruction push and ~10 extra
return-side instructions. Modern AArch64 indirect-branch prediction
seems to handle the BR-through-cache pattern well enough on its own.
Slow-32's AArch64 backend reaches the same dead end: its `emit_ras_predict`
does the pop and a cmp but never uses the prediction (`emit_inline_lookup(W8)`
runs the same probe with the actual target regardless), so it pays the
push/pop cost without claiming any benefit. A different RAS shape that
leverages the *hardware* RAS — e.g. translating guest calls/rets to host
BLR/RET pairs through a per-function stub — might be profitable but is
a much bigger structural change than the porting work above.

Tried and reverted: **Diamond merge for short forward branches**. A
faithful port of the x86 diamond merge (b.cond skips a tiny body of
≤3 straight-line insns; both arms converge with empty cache) measured
~10% slower on benchmark_core under alternating-binary comparison and
roughly neutral on lisp. The mechanism is correct (4 merges fired in
benchmark_core, 22 in lisp) but the trade is bad on this host: the
required rc_flush + rc_init on both sides of the diamond forces cold
LDRs for every register accessed in the post-merge code, and that cost
exceeds what we save by avoiding the chained-cache dispatch (which is
itself only ~9 host instructions). Same root cause as the RAS regression
— our chained_exit is too cheap for the optimizations that target
"avoid dispatch" to pay off. A diamond merge that *preserves* cache
state across the body (e.g. by translating the body without going
through the cache, or by a more careful liveness analysis) might be
profitable but is significantly more complex.

### Register cache expansion (x86 side)
The 8-slot LRU cache (RSI, RDI, R8-R11, R14, R15) is the main translation bottleneck for register-heavy loops on x86-64, which doesn't have more GPRs to spare without going through XMM.

### Peephole optimizer
Currently listed in the architecture section but not implemented. Post-translation peephole passes could:
- Eliminate redundant `mov reg, reg` pairs after register allocation
- Fold `mov + add` into `lea`
- Strength-reduce `imul` by power-of-2 constants to `shl`
- Dead store elimination for guest registers written but never read before block exit

## ISA Extensions

### F extension (single-precision float)
RV32IF adds 32 float registers (f0-f31) and instructions like FADD.S, FMUL.S, FLW, FSW. The runtime already has software math (`math_soft.c`), but hardware float in the translator would be significantly faster. x86-64 has SSE2 which maps well.

### D extension (double-precision float)
RV32IFD adds double-precision. Same approach as F but with 64-bit XMM registers. Would need to handle the register file expansion (32 x 64-bit FP registers in rv32_ctx_t).

### A extension (atomics)
RV32IA adds load-reserved/store-conditional (LR.W/SC.W) and atomic read-modify-write (AMOSWAP, AMOADD, etc.). Not needed for single-threaded microcontroller workloads, but would be required for multi-threaded guest code.

### C extension (compressed instructions)
RV32IMC adds 16-bit compressed instructions. The decoder would need to handle variable-length fetch (2 or 4 bytes) based on the low bits. Significant code size reduction for guest binaries.

## Runtime

### fstat implementation (ECALL 80)
Currently stubbed to return -1. Proper implementation requires marshalling `struct stat` across the guest/host boundary (guest is ILP32, host may be LP64). No ported program needs it yet, but future ports might.

### Directory operations
`opendir`/`readdir`/`closedir`/`mkdir`/`rmdir`/`chdir` are all stubbed. Would require adding new ECALLs and `struct dirent` marshalling. Needed if we port programs that traverse directory trees.

### mmap / dynamic linking
The current model is fully static (one ELF, flat address space). Supporting `mmap` would enable dynamic memory regions; supporting `dlopen` would require an ELF dynamic linker. Very unlikely to be needed for the microcontroller target, but listed for completeness.

## Tooling

### Trace / debug mode
The `-i` (interpreter) flag works for correctness debugging but is slow. A hybrid mode that runs the DBT with per-block instruction tracing (like `S5_TRACE` in SLOW-32) would help diagnose future translation bugs without the full interpreter overhead.

### Profile-guided optimization
Instrument the block cache to track execution counts per guest PC. Use the profile to:
- Prioritize superblock extension for hot loops
- Pre-translate hot functions at startup
- Report hot spots to the user for guest-side optimization

## Ports

Potential future guest programs to exercise the runtime:
- **SQLite** — would stress file I/O, malloc, and larger code size
- **MicroPython** — another scripting language, good runtime diversity
- **A C compiler** — self-hosting test (compile a small C program under the DBT)
- **Dhrystone / Coremark** — standard benchmarks for the QEMU comparison

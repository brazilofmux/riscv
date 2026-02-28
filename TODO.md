# TODO — RV32IM DBT Future Work

## Performance

### Benchmark vs QEMU
The one remaining checklist item. Now that 7 real programs are ported, we have meaningful workloads to measure. Key metrics: startup time, sustained throughput (Lua benchmarks, dBASE bulk operations), and code size overhead.

### ARM64 host backend
The emitter (`emit_x64.h`) is x86-64 only. An ARM64 backend would enable running on Apple Silicon and Raspberry Pi. The register cache, block chaining, and superblock architecture are host-agnostic — only the emitter and trampoline need porting.

### Register cache expansion
The 8-slot LRU cache (RSI, RDI, R8-R11, R14, R15) is the main translation bottleneck for register-heavy loops. x86-64 doesn't have more GPRs to spare, but an ARM64 backend could use 16+ cache slots, eliminating most spill/reload overhead.

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

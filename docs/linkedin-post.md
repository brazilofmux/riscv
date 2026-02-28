# LinkedIn / Facebook Post

---

**RISC-V is already the best portable bytecode. We just haven't noticed.**

I built a purpose-built dynamic binary translator for RV32IM (RISC-V 32-bit) in about 5,000 lines of C. It runs stock ELF binaries compiled with stock GCC at 70% of native speed — 6.2 billion emulated instructions per second on a modern x86-64 host.

That's 1.5–4.5x faster than QEMU user-mode on the same workloads.

The backstory: I spent months building a custom ISA called SLOW-32 with the *entire* stack from scratch. A native LLVM backend. A custom assembler and linker. A selfhosting bootstrap chain (Ken Thompson style — Forth builds the assembler, assembler builds the compiler). A QEMU system emulator target. Five emulators. Docker containers. The whole works.

Then the AH! moment: almost none of it mattered. The custom ISA was converging on RISC-V anyway. The custom toolchain couldn't match 30 years of GCC optimization passes. The custom binary format didn't buy anything over stock ELF. The only piece that was genuinely unique — the only piece you can't get off the shelf — was the dynamic binary translator.

So I threw away everything except the DBT, pointed it at stock RISC-V, and the result was *better*.

And RISC-V had already done the exact right thing by defining subsets of itself. You don't implement "RISC-V" — you implement RV32IM (base integer + multiply/divide) and you have a fully functional target. Need floating-point? Add F and D. Need atomics for threads? Add A. Each extension is a small, well-defined increment. My DBT implements RV32IMFD — the full integer and floating-point ISA — and that covers every workload I've thrown at it.

The properties that make RISC-V good silicon — regular encoding, orthogonal registers, simple addressing modes — turn out to be exactly the properties that make a good portable bytecode. Trivial to decode, trivial to translate, maps nearly 1:1 onto every major host architecture.

The reason it's fast isn't clever engineering in the DBT — it's that RISC-V is *easy to translate well*. QEMU can't close the gap because it was written to emulate an entire computer — MMU, privilege modes, self-modifying code, multi-hart. Even in user-mode, that machinery is still there. We aren't emulating a computer. We're translating user-mode code with flat memory and a handful of host service calls. That's a fundamentally different (and simpler) problem.

I've ported 8 real programs (Lua, Forth, Scheme, Prolog, BASIC, a Z-machine, nano, dBASE III) — 283 tests passing. The whole system compiles in under a second.

WebAssembly was *designed* to be a portable execution format. RISC-V wasn't — and it's better at it. Stock compiler. Stock binary format. Stock debugger. No custom toolchain, no version instability.

I think RISC-V user-mode ELF is the best compilation target for portable native code, regardless of whether RISC-V silicon ever dominates. The ISA already won — as a spec, not as a chip.

Repo coming soon. Curious what others think.

— Stephen

/* dbt_a64.c — AArch64 code emission for the RV32IMFD DBT.
 *
 * Stage: stub. dbt_jit_available() returns 0, so main.c forces interpreter
 * mode and the other two hooks are never invoked at runtime. They're still
 * defined here so the linker is satisfied.
 *
 * Phases that fill this in:
 *   P1 — emit_a64.h primitives
 *   P2 — integer translator paths (translate_one + translate_block driver)
 *   P3 — FP translator paths (FLW/FSW, FADD/FSUB/FMUL/FDIV/FSQRT, FCVT, …)
 *   P4 — block chaining inline cache, RAS, fusion, intrinsic native stubs
 *
 * Planned host register convention:
 *   X19 = pointer to rv32_ctx_t          (callee-saved)
 *   X20 = guest memory base              (callee-saved)
 *   X21 = block cache base               (callee-saved)
 *   X9-X16 = scratch
 *   X22-X28 + the spare callee-saveds = LRU integer register cache slots
 *   D0-D1 = FP scratch; D8-D15 (callee-saved low halves) = FP cache slots
 */
#include "dbt.h"
#include "emit_a64.h"
#include <stdio.h>
#include <stdlib.h>

int dbt_jit_available(void) { return 0; }

void dbt_emit_trampoline(dbt_state_t *dbt) {
    (void)dbt;
    fprintf(stderr,
        "rv32-run: AArch64 JIT trampoline emitter not yet implemented\n");
    abort();
}

uint8_t *dbt_translate_block(dbt_state_t *dbt, uint32_t guest_pc) {
    (void)dbt; (void)guest_pc;
    fprintf(stderr,
        "rv32-run: AArch64 JIT translator not yet implemented (pc=0x%08X)\n",
        guest_pc);
    abort();
}

/* shadow.h — Lockstep shadow interpreter for the RV32IMFD JIT.
 *
 * When `dbt->verify` is set (-V flag), every translated block is run twice:
 * first the shadow steps the same guest instructions from a pre-block
 * snapshot, capturing stores in a shadow buffer; then the JIT runs and
 * touches real memory; finally the two end-states are compared (registers,
 * PC, FP regs, and shadow stores vs the real-memory bytes the JIT
 * produced). Any divergence aborts with diagnostics.
 *
 * The chained-exit helpers in dbt_a64/dbt_x64 short-circuit to
 * exit_with_pc when verify is on, so each block returns to the C
 * dispatcher and the shadow gets a chance to verify it.
 *
 * Must be kept in sync with the JIT's translated semantics, which are
 * themselves modeled on interp.c. The shadow's step dispatcher is a
 * deliberate copy of interp.c's, with the mem_read/mem_write helpers
 * swapped for shadow-buffer-aware variants.
 */
#ifndef SHADOW_H
#define SHADOW_H

#include "dbt.h"
#include "elf_loader.h"
#include <stdint.h>

/* Bytes captured per block. 256 bytes is more than the worst-case block
 * (64 SW = 256 bytes). Larger blocks (e.g., self-loops iterating 1000
 * times) bail to per-byte coalescing in the buffer, sharing slots when
 * the same address is written more than once. */
#define SHADOW_STORE_BUF_SIZE 4096

typedef struct {
    uint32_t addr;
    uint8_t  value;
} shadow_store_t;

typedef struct {
    uint32_t x[32];
    uint32_t pc;
    uint64_t f[32];
    uint32_t fcsr;

    /* Pre-block snapshot — restored before each shadow_pre_execute. */
    uint32_t snap_x[32];
    uint32_t snap_pc;
    uint64_t snap_f[32];
    uint32_t snap_fcsr;

    /* Per-block store log (cleared on each pre_execute). */
    shadow_store_t store_buf[SHADOW_STORE_BUF_SIZE];
    int store_buf_count;
    int store_buf_overflow;

    /* Read-only handle to guest memory; loads see pre-block contents (we
     * snapshot real memory effects only after the JIT runs). */
    rv32_binary_t *bin;

    /* Set when the shadow stops because of an instruction the JIT will
     * also exit on (ECALL/EBREAK), so verify knows not to flag PC mismatch. */
    int hit_ecall;
    int hit_ebreak;

    /* PCs of intercepted intrinsic blocks — shadow can't model the host
     * BLR to libc memcpy/memset/etc., so blocks starting at these PCs are
     * skipped. */
    uint32_t skip_pcs[32];
    int skip_pc_count;

    /* Stats */
    uint64_t blocks_verified;
    uint64_t blocks_skipped;
    uint64_t insns_verified;
} shadow_state_t;

void shadow_init(shadow_state_t *s, dbt_state_t *dbt);

/* Snapshot dbt->ctx into s->snap_*. Call before each JIT block. */
void shadow_snapshot(shadow_state_t *s, dbt_state_t *dbt);

/* Run the shadow on the snapshotted state, stopping at the first guest
 * instruction the JIT would also exit on (branch taken/not-taken, JAL,
 * JALR, ECALL/EBREAK, or after an instruction budget). Returns 1 if the
 * block should be verified, 0 if it should be skipped (intrinsic, etc.).
 */
int shadow_pre_execute(shadow_state_t *s, uint32_t guest_pc);

/* Compare shadow state with dbt's actual state. Aborts on divergence. */
void shadow_verify(shadow_state_t *s, dbt_state_t *dbt, uint32_t block_pc);

void shadow_print_stats(shadow_state_t *s);

#endif /* SHADOW_H */

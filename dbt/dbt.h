#ifndef DBT_H
#define DBT_H

#include "elf_loader.h"
#include <stdint.h>

/* Return Address Stack for call/return prediction */
#define RAS_SIZE  32
#define RAS_MASK  (RAS_SIZE - 1)

/* Guest CPU context — laid out for fast access from JIT code.
 *   x86-64 backend: RBX points to this struct during execution.
 *   AArch64 backend: X19 points to this struct during execution.
 * Field offsets (used by hand-emitted addressing in both backends):
 *   x[0..31]   at offset   0..127
 *   next_pc    at offset 128
 *   ras[0..31] at offset 132..259
 *   ras_top    at offset 260
 *   f[0..31]   at offset 264 (after natural 8-byte alignment)
 *   fcsr       at offset 520
 */
typedef struct {
    uint32_t x[32];          /* guest registers (x[0] always 0) */
    uint32_t next_pc;        /* set by translated block exits */
    uint32_t ras[RAS_SIZE];  /* return address stack predictions */
    uint32_t ras_top;        /* RAS circular buffer index */
    uint64_t f[32];          /* FP registers (NaN-boxed: f32 in low 32 + 0xFFFFFFFF upper) */
    uint32_t fcsr;           /* FP control/status: frm[7:5], fflags[4:0] */
} rv32_ctx_t;

/* Block cache entry — exactly 16 bytes for inline JIT lookup.
 *   x86-64 backend: R13 points to cache[0] during execution.
 *   AArch64 backend: X21 points to cache[0] during execution.
 * Lookup: index = (pc >> 2) & MASK; entry = base + index * 16
 */
typedef struct {
    uint32_t guest_pc;    /* guest start address (0 = empty) */
    uint32_t _pad;
    uint8_t *native_code; /* pointer into code buffer */
} block_entry_t;

_Static_assert(sizeof(block_entry_t) == 16, "block_entry_t must be 16 bytes");

#define BLOCK_CACHE_SIZE  (64 * 1024)  /* must be power of 2 */
#define BLOCK_CACHE_MASK  (BLOCK_CACHE_SIZE - 1)

/* Field offsets within rv32_ctx_t. Used by both backends to address
 * the guest CPU state via [ctx_ptr + offset]. */
#define CTX_X_OFF         0     /* x[0..31] at offsets 0..127 */
#define CTX_NEXT_PC_OFF   128   /* x[32] = 128 bytes, then next_pc */
#define CTX_RAS_OFF       132   /* next_pc + 4 = 132, ras[0..31] */
#define CTX_RAS_TOP_OFF   260   /* 132 + 32*4 = 260, ras_top */
#define CTX_FP_OFF        264   /* ras_top + 4 = 264, f[0..31] x 8 bytes */
#define CTX_FCSR_OFF      520   /* 264 + 256 = 520 */

#define CODE_BUF_SIZE     (256 * 1024 * 1024)  /* 256 MB JIT code buffer */

/* DBT state */
typedef struct {
    rv32_ctx_t ctx;
    rv32_binary_t *bin;

    /* Block cache */
    block_entry_t cache[BLOCK_CACHE_SIZE];

    /* JIT code buffer */
    uint8_t *code_buf;
    uint32_t code_used;

    /* Stats */
    uint64_t blocks_translated;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t ras_hits;
    uint64_t ras_misses;
    uint64_t superblock_count;
    uint64_t side_exits_total;
    uint64_t diamond_merges;

    /* Intrinsic function addresses (0 = not found) */
    uint32_t intrinsic_memcpy;
    uint32_t intrinsic_memset;
    uint32_t intrinsic_memmove;
    uint32_t intrinsic_strlen;

    /* Debug */
    int trace;
} dbt_state_t;

int dbt_init(dbt_state_t *dbt, rv32_binary_t *bin);
int dbt_run(dbt_state_t *dbt);
void dbt_cleanup(dbt_state_t *dbt);

/* ---- Arch-specific hooks (provided by dbt_x64.c or dbt_a64.c) ----
 *
 * dbt_translate_block: translate the guest block starting at guest_pc and
 *   return a pointer into dbt->code_buf to the entry point.
 *
 * dbt_emit_trampoline: write the host-arch entry/exit shim at the start of
 *   dbt->code_buf, advancing dbt->code_used. The shim is invoked by dbt_run
 *   as a C function pointer with signature
 *     void(*)(rv32_ctx_t *ctx, uint8_t *mem, void *block, void *cache);
 *   and is responsible for loading the host-arch register convention (ctx
 *   pointer, mem base, cache base) and then calling `block`.
 *
 * dbt_jit_available: 1 if the linked backend can actually translate; 0 if
 *   the host has no JIT yet (e.g. stub a64 backend before P1 is finished).
 *   main.c uses this to fall back to the interpreter automatically.
 */
uint8_t *dbt_translate_block(dbt_state_t *dbt, uint32_t guest_pc);
void     dbt_emit_trampoline(dbt_state_t *dbt);
int      dbt_jit_available(void);

/* Diamond-merge eligibility check (defined in dbt_common.c). Returns 1 if
 * every guest instruction in [start, target) is a side-effect-free
 * straight-line op. */
int      dbt_can_diamond_merge(dbt_state_t *dbt, uint32_t start, uint32_t target);

/* Symbol-table lookup, used by dbt_init for intrinsic interception. */
uint32_t dbt_find_symbol(rv32_binary_t *bin, const char *name);

#endif /* DBT_H */

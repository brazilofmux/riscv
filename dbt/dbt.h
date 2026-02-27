#ifndef DBT_H
#define DBT_H

#include "elf_loader.h"
#include <stdint.h>

/* Return Address Stack for call/return prediction */
#define RAS_SIZE  32
#define RAS_MASK  (RAS_SIZE - 1)

/* Guest CPU context — laid out for fast access from JIT code.
 * RBX points to this struct during execution.
 * x[0..31] at offset 0..124, next_pc at offset 128.
 * ras[0..31] at offset 132..259, ras_top at offset 260.
 */
typedef struct {
    uint32_t x[32];          /* guest registers (x[0] always 0) */
    uint32_t next_pc;        /* set by translated block exits */
    uint32_t ras[RAS_SIZE];  /* return address stack predictions */
    uint32_t ras_top;        /* RAS circular buffer index */
} rv32_ctx_t;

/* Block cache entry — exactly 16 bytes for inline JIT lookup.
 * R13 points to cache[0] during execution.
 * Lookup: index = (pc >> 2) & MASK; entry = R13 + index * 16
 */
typedef struct {
    uint32_t guest_pc;    /* guest start address (0 = empty) */
    uint32_t _pad;
    uint8_t *native_code; /* pointer into code buffer */
} block_entry_t;

_Static_assert(sizeof(block_entry_t) == 16, "block_entry_t must be 16 bytes");

#define BLOCK_CACHE_SIZE  (64 * 1024)  /* must be power of 2 */
#define BLOCK_CACHE_MASK  (BLOCK_CACHE_SIZE - 1)

#define CODE_BUF_SIZE     (64 * 1024 * 1024)  /* 64 MB JIT code buffer */

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

#endif /* DBT_H */

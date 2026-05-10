#ifndef DBT_H
#define DBT_H

#include "elf_loader.h"
#include <stdint.h>
#include <sys/mman.h>

/* ============================================================================
 * JIT memory portability (Apple Silicon W^X)
 * ============================================================================
 * Apple Silicon enforces W^X on JIT pages: a single page cannot be both
 * writable and executable. The mmap must use MAP_JIT, and code emission
 * brackets writes with pthread_jit_write_protect_np(0/1) to flip the
 * thread's view of the JIT page between RW and RX. Linux and Intel macOS
 * have no such restriction; the helpers compile to no-ops there.
 *
 * Instruction-cache invalidation on AArch64 is handled separately by
 * __builtin___clear_cache calls inside the emitter.
 *
 * Invariant
 * ---------
 * Every byte ever written to dbt->code_buf MUST happen between a paired
 * dbt_jit_writable_begin() and dbt_jit_writable_end(). Today there are
 * exactly two callers that satisfy this:
 *
 *   1. dbt_emit_trampoline (called once from dbt_init, bracketed there).
 *   2. dbt_translate_block (called per-block from dbt_run, bracketed
 *      there — covers all in-block emission AND in-block patches like
 *      emit_patch_cond19/emit_patch_b26/emit_patch_rel32 in
 *      chained_exit_*, the back-edge, the branch fallback, and the
 *      superblock cold stubs).
 *
 * Block chaining in this DBT is via an inline-cache probe (each block's
 * tail loads dbt->cache[hash] and BRs through it) — we do NOT patch a
 * previously-emitted block's tail when a new chainee appears. dbt->cache
 * is a regular C array, not part of code_buf, so cache_insert is W^X-safe.
 *
 * If you add a new write site to code_buf — patch-style block chaining,
 * hot-patching a stub, anything outside dbt_translate_block — you MUST
 * bracket it with dbt_jit_writable_begin/end or it will fault on
 * Apple Silicon. */
#if defined(__APPLE__) && defined(__aarch64__)
#include <pthread.h>
#define DBT_JIT_MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_JIT)
static inline void dbt_jit_writable_begin(void) { pthread_jit_write_protect_np(0); }
static inline void dbt_jit_writable_end(void)   { pthread_jit_write_protect_np(1); }
#else
#define DBT_JIT_MMAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)
static inline void dbt_jit_writable_begin(void) { }
static inline void dbt_jit_writable_end(void)   { }
#endif


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

/* Maximum guest instructions per translated block. Both backends respect
 * this — and the shadow interpreter must use the same value, since with
 * chaining/superblocks/self-loop disabled in verify mode this is the
 * upper bound on a block's instruction count when no control-flow op is
 * encountered. */
#define MAX_BLOCK_INSNS  64

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
    /* libm (transcendental) intrinsics — replaces the guest's software
     * Taylor/Newton series with a 1-call host libm dispatch. Indexed by
     * libm_id_t below; 0 = symbol not present in the guest binary. */
    uint32_t intrinsic_libm[/* LIBM_COUNT */ 20];

    /* Debug */
    int trace;
    int verify;        /* lockstep shadow-interp verification mode (-V) */
    void *shadow;      /* shadow_state_t * — opaque here, allocated when verify is set */
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

/* libm intrinsic IDs — must match the order in libm_table[] (dbt_common.c)
 * and the size of dbt_state_t.intrinsic_libm[]. Each ID names a function
 * the guest's libc would otherwise compute by software series; on lookup
 * hit, the JIT emits a tiny stub that loads the guest FP arg(s), BLR/CALL
 * the host libm, stores the result, and returns via guest ra. */
typedef enum {
    LIBM_SIN, LIBM_COS, LIBM_TAN,
    LIBM_ASIN, LIBM_ACOS, LIBM_ATAN,
    LIBM_SINH, LIBM_COSH, LIBM_TANH,
    LIBM_EXP, LIBM_LOG, LIBM_LOG10, LIBM_LOG2,
    LIBM_SQRT, LIBM_FABS, LIBM_FLOOR, LIBM_CEIL,
    LIBM_POW, LIBM_ATAN2, LIBM_FMOD,
    LIBM_COUNT
} libm_id_t;
_Static_assert(LIBM_COUNT == 20,
               "intrinsic_libm[] in dbt_state_t must match LIBM_COUNT");

typedef struct {
    const char *name;   /* guest symbol to look up */
    void       *fn;     /* host libm function pointer */
    int         two_arg;/* 0 = (d) → d, 1 = (d, d) → d */
} libm_info_t;

extern const libm_info_t libm_table[LIBM_COUNT];

#endif /* DBT_H */

/* dbt_a64.c — AArch64 code emission for the RV32IMFD DBT.
 *
 * Stage: P6 baseline + register cache. Integer GPRs are kept in an LRU
 * cache of host registers within each translated block; reads from cached
 * regs become register-to-register moves (or nothing — the slot already
 * holds the value); writes become in-cache and are flushed to ctx only at
 * block exit. Most ALU loops now run with zero ctx round-trips on the hot
 * path. Block chaining, intrinsic stubs, and LUI/AUIPC fusion are still
 * here. RAS, AUIPC+JALR fusion, SLT+branch fusion, diamond merge, and
 * superblocks remain TODO.
 *
 * Host register convention (matches the trampoline below):
 *   X19 = pointer to rv32_ctx_t           (callee-saved, set by trampoline)
 *   X20 = guest memory base               (callee-saved)
 *   X21 = block cache base                (callee-saved)
 *   X22..X28 = 7 LRU register cache slots (callee-saved, saved by trampoline)
 *   X15      = 8th LRU register cache slot (caller-saved — see note below)
 *   W9       = scratch / source operand 1
 *   W10      = scratch / source operand 2
 *   W11      = scratch / result before store
 *   W12      = scratch / effective address / temp
 *   W14      = JALR target staging (outside the chained-exit scratches)
 *
 * X15 footnote: it is caller-saved but safe as a cache slot because
 * translated blocks never BLR. The intrinsic stubs (memcpy/memmove/memset/
 * strlen) DO BLR to host libc, but they are entire blocks unto themselves
 * — they do not share cache state with regular blocks, so X15 carrying a
 * stale guest-reg value across the libc call is fine; the value is dead
 * by the time the stub's chained_exit_indirect runs.
 *
 * Block exit ABI: every translated block sets ctx->next_pc and executes
 * RET. The trampoline BLR'd into the block, so RET returns into the
 * trampoline's epilogue, which restores the callee-saved registers and
 * returns to dbt_run() in dbt_common.c.
 */
#include "dbt.h"
#include "decoder.h"
#include "emit_a64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Host libc functions used by the intrinsic native stubs below. */
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern size_t strlen(const char *);

/* Max guest instructions per translated block — same as x86 backend. */
#define MAX_BLOCK_INSNS  64

/* Named scratch-register aliases for readability inside the translator. */
#define A_CTX    A64_W19
#define A_MEM    A64_W20
#define A_CACHE  A64_W21
#define A_S0     A64_W9
#define A_S1     A64_W10
#define A_S2     A64_W11
#define A_S3     A64_W12

int dbt_jit_available(void) { return 1; }

/* ---- Register cache ----
 *
 * Eight LRU slots. Slot 0..6 are callee-saved (X22..X28, preserved across
 * the trampoline call by save/restore in dbt_emit_trampoline). Slot 7 is
 * X15 — caller-saved on AArch64 but safe within a translated block since
 * regular blocks never BLR. (Intrinsic stubs that BLR to libc are entire
 * stub blocks; they don't share cache state with cached regular blocks.)
 *
 * Each slot tracks: the guest register number it currently holds (-1 =
 * free), a dirty bit, and a last-use timestamp for LRU eviction.
 *
 * x0 is special: rc_read returns WZR (no instruction, value is hardwired
 * zero). rc_write returns A_S2 (writes to x0 are discarded into a scratch
 * — keeps callers uniform without special-casing rd==0 at every site).
 */

#define RC_NUM_SLOTS  8

static const a64_reg_t rc_host_regs[RC_NUM_SLOTS] = {
    A64_W22, A64_W23, A64_W24, A64_W25,
    A64_W26, A64_W27, A64_W28, A64_W15
};

typedef struct {
    int guest_reg;  /* -1 = free */
    int dirty;
    int last_use;
} rc_slot_t;

typedef struct {
    rc_slot_t slots[RC_NUM_SLOTS];
    int clock;
} reg_cache_t;

static void rc_init(reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++) {
        rc->slots[i].guest_reg = -1;
        rc->slots[i].dirty = 0;
        rc->slots[i].last_use = 0;
    }
    rc->clock = 0;
}

static int rc_find(reg_cache_t *rc, int guest_reg) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == guest_reg) return i;
    return -1;
}

static int rc_alloc(reg_cache_t *rc, emit_t *e) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg == -1) return i;
    int lru = 0;
    for (int i = 1; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].last_use < rc->slots[lru].last_use) lru = i;
    if (rc->slots[lru].dirty)
        emit_str_w32_imm(e, rc_host_regs[lru], A_CTX,
                         (uint32_t)(rc->slots[lru].guest_reg * 4));
    rc->slots[lru].guest_reg = -1;
    rc->slots[lru].dirty = 0;
    return lru;
}

/* Return a host register holding guest_reg's value. Loads from ctx on miss.
 * For x0 returns WZR — most call sites pass the result straight into a
 * shifted-register encoding where slot-31 reads as ZR. The arithmetic-
 * immediate helpers (add_w_imm32, cmp_w_imm32_*) already special-case
 * WZR to dodge the SP/ZR footgun. */
static a64_reg_t rc_read(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) return A64_WZR;
    int slot = rc_find(rc, guest_reg);
    if (slot >= 0) {
        rc->slots[slot].last_use = ++rc->clock;
        return rc_host_regs[slot];
    }
    slot = rc_alloc(rc, e);
    emit_ldr_w32_imm(e, rc_host_regs[slot], A_CTX, (uint32_t)(guest_reg * 4));
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 0;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

/* Allocate a host register that the caller will write into. Marks the slot
 * dirty so rc_flush will write it back to ctx at block exit. For x0 returns
 * scratch A_S2 so the caller can emit its instruction unconditionally; the
 * value is discarded since the cache slot table never tracks x0. */
static a64_reg_t rc_write(emit_t *e, reg_cache_t *rc, int guest_reg) {
    if (guest_reg == 0) return A_S2;
    int slot = rc_find(rc, guest_reg);
    if (slot < 0) slot = rc_alloc(rc, e);
    rc->slots[slot].guest_reg = guest_reg;
    rc->slots[slot].dirty = 1;
    rc->slots[slot].last_use = ++rc->clock;
    return rc_host_regs[slot];
}

/* Write back all dirty cached regs to ctx. Does NOT clear dirty bits — the
 * branch translator emits two exit paths that both want the same flush
 * sequence; calling rc_flush before the cmp+b.cond means each exit path
 * already sees ctx in sync without re-running the writeback. */
static void rc_flush(emit_t *e, reg_cache_t *rc) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (rc->slots[i].guest_reg >= 0 && rc->slots[i].dirty)
            emit_str_w32_imm(e, rc_host_regs[i], A_CTX,
                             (uint32_t)(rc->slots[i].guest_reg * 4));
}

/* Same as rc_flush but uses an arbitrary slot snapshot — needed by the
 * superblock cold stubs, which store the cache state at each side-exit
 * branch point and replay the writeback when the branch is taken. */
static void rc_flush_snapshot(emit_t *e, const rc_slot_t *snapshot) {
    for (int i = 0; i < RC_NUM_SLOTS; i++)
        if (snapshot[i].guest_reg >= 0 && snapshot[i].dirty)
            emit_str_w32_imm(e, rc_host_regs[i], A_CTX,
                             (uint32_t)(snapshot[i].guest_reg * 4));
}

/* ---- Superblock side exits ----
 *
 * When we hit a forward conditional branch and have buffer/budget left,
 * we treat the TAKEN side as a "side exit" — emit the b.cond unpatched,
 * snapshot the current cache state, and keep translating the fall-through
 * path inside the same block. At end-of-block we emit a cold stub for
 * each side exit: patch the b.cond to land here, replay the snapshot's
 * dirty writebacks, then chained_exit_known to the original target.
 *
 * The snapshot is necessary because the fall-through path may evict slots
 * or write new guest regs to host regs. At runtime, when the side exit
 * fires, control jumps STRAIGHT from the b.cond to the cold stub —
 * skipping all of the fall-through code — so the host regs still hold
 * exactly the values the snapshot recorded. The stub uses the snapshot
 * to know which dirty values need to land in ctx before exiting. */

typedef struct {
    uint32_t bcond_patch;            /* offset of the b.cond to patch */
    uint32_t target_pc;              /* guest PC for the side exit */
    rc_slot_t snapshot[RC_NUM_SLOTS];/* cache state at the branch point */
} side_exit_t;

#define MAX_SIDE_EXITS 8

/* ---- Small helpers ---- */

/* dst = rn + imm  (32-bit). Picks ADD/SUB-imm12 if the magnitude fits,
 * otherwise materializes the constant in A_S3 (caller must not be relying
 * on A_S3 already holding something live across this call).
 *
 * AArch64 footgun: register 31 in the Rn slot of ADD/SUB-immediate means
 * SP, not WZR — that disambiguation only flips to ZR for shifted-register
 * forms. So `ADD W, WZR, #imm` decodes as `ADD W, WSP, #imm` and reads SP.
 * When the source is WZR, materialize the constant directly. */
static void add_w_imm32(emit_t *e, a64_reg_t dst, a64_reg_t rn, int32_t imm) {
    if (rn == A64_WZR) {
        emit_mov_w32_imm32(e, dst, (uint32_t)imm);
        return;
    }
    if (imm == 0) {
        if (dst != rn) emit_mov_w32_w32(e, dst, rn);
        return;
    }
    if (imm > 0 && imm < 4096) {
        emit_add_w32_imm(e, dst, rn, (uint32_t)imm);
        return;
    }
    if (imm < 0 && imm > -4096) {
        emit_sub_w32_imm(e, dst, rn, (uint32_t)(-imm));
        return;
    }
    emit_mov_w32_imm32(e, A_S3, (uint32_t)imm);
    emit_add_w32(e, dst, rn, A_S3);
}

/* CMP wn, #imm  with the same WZR/SP guard. When rs1 is x[0] we can
 * constant-fold the comparison result, but the cleanest fix is to
 * materialize WZR into a real scratch register first. The result of CMP
 * lands in flags; this helper just emits the right comparison sequence. */
static void cmp_w_imm32_signed(emit_t *e, a64_reg_t rn, int32_t imm) {
    if (rn == A64_WZR) {
        emit_mov_w32_w32(e, A_S0, A64_WZR);
        rn = A_S0;
    }
    if (imm >= 0 && imm < 4096) {
        emit_cmp_w32_imm(e, rn, (uint32_t)imm);
    } else if (imm < 0 && imm > -4096) {
        emit_cmn_w32_imm(e, rn, (uint32_t)(-imm));
    } else {
        emit_mov_w32_imm32(e, A_S1, (uint32_t)imm);
        emit_cmp_w32_w32(e, rn, A_S1);
    }
}

/* CMP wn, #imm  treating the immediate as a 32-bit value. (For SLTIU,
 * RV sign-extends the 12-bit imm but compares the result unsigned.) */
static void cmp_w_imm32_unsigned(emit_t *e, a64_reg_t rn, int32_t imm) {
    if (rn == A64_WZR) {
        emit_mov_w32_w32(e, A_S0, A64_WZR);
        rn = A_S0;
    }
    if (imm >= 0 && imm < 4096) {
        emit_cmp_w32_imm(e, rn, (uint32_t)imm);
    } else {
        emit_mov_w32_imm32(e, A_S1, (uint32_t)imm);
        emit_cmp_w32_w32(e, rn, A_S1);
    }
}

/* ---- FP helpers ----
 *
 * f[0..31] are 64-bit slots in rv32_ctx_t starting at CTX_FP_OFF. Single-
 * precision values are NaN-boxed: lower 4 bytes hold the float bits, upper
 * 4 bytes are 0xFFFFFFFF. Doubles take the full 8 bytes. There is no FP
 * register cache yet — every FP access goes through ctx memory.
 *
 * FP register convention:
 *   D0/D1/D2 = scratch / source / result.
 */
#define FP_OFF(fp_reg) ((uint32_t)(CTX_FP_OFF + (fp_reg) * 8))

static void load_fp_s(emit_t *e, int sd, int f) {
    /* LDR Sd, [X19, fp_off]  — loads lower 4 bytes; upper of D auto-zeros. */
    emit_ldr_s_imm(e, sd, A_CTX, FP_OFF(f));
}

static void load_fp_d(emit_t *e, int dd, int f) {
    emit_ldr_d_imm(e, dd, A_CTX, FP_OFF(f));
}

static void store_fp_s(emit_t *e, int f, int sd) {
    /* Store the single value, then NaN-box by writing 0xFFFFFFFF to the
     * upper 4 bytes (RV32F spec mandates this so subsequent SP reads see
     * a properly-boxed value). */
    emit_str_s_imm(e, sd, A_CTX, FP_OFF(f));
    emit_movn_w32(e, A_S0, 0, 0);   /* W9 = 0xFFFFFFFF */
    emit_str_w32_imm(e, A_S0, A_CTX, FP_OFF(f) + 4);
}

static void store_fp_d(emit_t *e, int f, int dd) {
    emit_str_d_imm(e, dd, A_CTX, FP_OFF(f));
}

/* Tail of every block exit: store next_pc, return to dispatcher. Caller
 * must rc_flush first if there are any dirty cached registers. */
static void exit_with_pc(emit_t *e, uint32_t next_pc) {
    emit_mov_w32_imm32(e, A_S0, next_pc);
    emit_str_w32_imm(e, A_S0, A_CTX, CTX_NEXT_PC_OFF);
    emit_ret(e);
}

/* Chained exit for a KNOWN target PC. Inline-cache probe against the
 * block_entry_t at cache[hash(target_pc)]: if guest_pc matches we BR
 * directly to native_code, skipping the C dispatcher entirely. On miss,
 * fall through to the same exit-and-return sequence as exit_with_pc.
 *
 * X21 = cache base (set by trampoline). 16-byte entries: guest_pc at +0,
 * pad at +4, native_code at +8.
 *
 * Caller must rc_flush first.
 */
static void chained_exit_known(emit_t *e, uint32_t target_pc) {
    uint32_t hash_off = (uint32_t)(((target_pc >> 2) & BLOCK_CACHE_MASK) * 16u);

    emit_mov_w32_imm32(e, A_S0, target_pc);

    emit_mov_w32_imm32(e, A_S1, hash_off);
    emit_add_x64_w32_uxtw(e, A_S2, A_CACHE, A_S1);

    emit_ldr_w32_imm(e, A_S3, A_S2, 0);

    emit_cmp_w32_w32(e, A_S3, A_S0);
    uint32_t bne_off = emit_pos(e);
    emit_b_cond(e, A64_COND_NE, 0);

    /* hit */
    emit_ldr_x64_imm(e, A_S3, A_S2, 8);
    emit_br(e, A_S3);

    /* miss: store next_pc, return to dispatcher */
    uint32_t miss_target = emit_pos(e);
    emit_patch_cond19(e, bne_off, miss_target);
    emit_str_w32_imm(e, A_S0, A_CTX, CTX_NEXT_PC_OFF);
    emit_ret(e);
}

/* Chained exit for an INDIRECT target (JALR). Target PC is in `pc_w`
 * (must not be A_S0..A_S3). Always stores next_pc up front so the
 * dispatcher's ecall check works on miss. Caller must rc_flush first. */
static void chained_exit_indirect(emit_t *e, a64_reg_t pc_w) {
    emit_str_w32_imm(e, pc_w, A_CTX, CTX_NEXT_PC_OFF);

    emit_lsr_w32_imm(e, A_S0, pc_w, 2);
    if (!emit_and_w32_imm(e, A_S0, A_S0, (uint32_t)BLOCK_CACHE_MASK)) {
        emit_mov_w32_imm32(e, A_S1, (uint32_t)BLOCK_CACHE_MASK);
        emit_and_w32(e, A_S0, A_S0, A_S1);
    }
    emit_lsl_w32_imm(e, A_S0, A_S0, 4);

    emit_add_x64_w32_uxtw(e, A_S2, A_CACHE, A_S0);

    emit_ldr_w32_imm(e, A_S3, A_S2, 0);
    emit_cmp_w32_w32(e, A_S3, pc_w);
    uint32_t bne_off = emit_pos(e);
    emit_b_cond(e, A64_COND_NE, 0);

    /* hit */
    emit_ldr_x64_imm(e, A_S3, A_S2, 8);
    emit_br(e, A_S3);

    /* miss */
    uint32_t miss_target = emit_pos(e);
    emit_patch_cond19(e, bne_off, miss_target);
    emit_ret(e);
}

/* ---- Per-instruction translation ----
 *
 * Returns 0 on success, -1 if the opcode isn't handled. Caller handles
 * control-flow opcodes (JAL, JALR, BRANCH, SYSTEM) directly — they end
 * the block. translate_one only deals with straight-line ops and is
 * responsible for keeping the register cache state coherent.
 */
static int translate_one(emit_t *e, reg_cache_t *rc,
                         rv32_insn_t *insn, uint32_t pc) {
    switch (insn->opcode) {

    case OP_LUI: {
        a64_reg_t rd = rc_write(e, rc, insn->rd);
        emit_mov_w32_imm32(e, rd, (uint32_t)insn->imm);
        return 0;
    }

    case OP_AUIPC: {
        a64_reg_t rd = rc_write(e, rc, insn->rd);
        emit_mov_w32_imm32(e, rd, pc + (uint32_t)insn->imm);
        return 0;
    }

    case OP_IMM: {
        a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
        a64_reg_t rd  = rc_write(e, rc, insn->rd);
        switch (insn->funct3) {
        case ALU_ADDI:
            add_w_imm32(e, rd, rs1, insn->imm);
            break;
        case ALU_SLTI:
            cmp_w_imm32_signed(e, rs1, insn->imm);
            emit_cset_w32(e, rd, A64_COND_LT);
            break;
        case ALU_SLTIU:
            cmp_w_imm32_unsigned(e, rs1, insn->imm);
            emit_cset_w32(e, rd, A64_COND_LO);
            break;
        case ALU_XORI:
            if (!emit_eor_w32_imm(e, rd, rs1, (uint32_t)insn->imm)) {
                emit_mov_w32_imm32(e, A_S1, (uint32_t)insn->imm);
                emit_eor_w32(e, rd, rs1, A_S1);
            }
            break;
        case ALU_ORI:
            if (!emit_orr_w32_imm(e, rd, rs1, (uint32_t)insn->imm)) {
                emit_mov_w32_imm32(e, A_S1, (uint32_t)insn->imm);
                emit_orr_w32(e, rd, rs1, A_S1);
            }
            break;
        case ALU_ANDI:
            if (!emit_and_w32_imm(e, rd, rs1, (uint32_t)insn->imm)) {
                emit_mov_w32_imm32(e, A_S1, (uint32_t)insn->imm);
                emit_and_w32(e, rd, rs1, A_S1);
            }
            break;
        case ALU_SLLI:
            emit_lsl_w32_imm(e, rd, rs1, (uint32_t)(insn->imm & 31));
            break;
        case ALU_SRLI:  /* SRLI when funct7=0x00, SRAI when funct7=0x20 */
            if (insn->funct7 == 0x20)
                emit_asr_w32_imm(e, rd, rs1, (uint32_t)(insn->imm & 31));
            else
                emit_lsr_w32_imm(e, rd, rs1, (uint32_t)(insn->imm & 31));
            break;
        default:
            return -1;
        }
        return 0;
    }

    case OP_REG: {
        a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
        a64_reg_t rs2 = rc_read(e, rc, insn->rs2);

        if (insn->funct7 == 0x01) {
            /* M extension: MUL/MULH/MULHSU/MULHU/DIV/DIVU/REM/REMU */
            switch (insn->funct3) {
            case ALU_ADD: { /* MUL */
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                emit_mul_w32(e, rd, rs1, rs2);
                break;
            }
            case ALU_SLL: { /* MULH (signed × signed → high 32) */
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                /* SMULL Xd, Wn, Wm reads Wn/Wm before writing Xd, so any
                 * aliasing of rd with rs1/rs2 is safe. */
                emit_smull(e, rd, rs1, rs2);
                emit_lsr_x64_imm(e, rd, rd, 32);
                break;
            }
            case ALU_SLT: { /* MULHSU (signed × unsigned → high 32) */
                /* Stage rs2 (zero-extended) into A_S3 BEFORE we sign-extend
                 * rs1 into rd, in case rd aliases rs2 (rd is allocated
                 * after rc_write so that's possible if rs2 is the LRU
                 * choice — though rc_read just touched it, so unlikely;
                 * still cheaper than depending on the LRU ordering). */
                emit_mov_w32_w32(e, A_S3, rs2);   /* X_S3 = zero_ext(rs2) */
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                emit_sxtw_x64_w32(e, rd, rs1);    /* X_rd = sign_ext(rs1) */
                emit_mul_x64     (e, rd, rd, A_S3);
                emit_lsr_x64_imm (e, rd, rd, 32);
                break;
            }
            case ALU_SLTU: { /* MULHU (unsigned × unsigned → high 32) */
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                emit_umull(e, rd, rs1, rs2);
                emit_lsr_x64_imm(e, rd, rd, 32);
                break;
            }
            case ALU_XOR: { /* DIV */
                /* RV: divisor==0 → -1; AArch64 SDIV gives 0. Branch around. */
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                uint32_t cbz_off = emit_pos(e);
                emit_cbz_w32(e, rs2, 0);
                emit_sdiv_w32(e, rd, rs1, rs2);
                uint32_t b_off = emit_pos(e);
                emit_b(e, 0);
                uint32_t divzero_target = emit_pos(e);
                emit_patch_cond19(e, cbz_off, divzero_target);
                emit_movn_w32(e, rd, 0, 0);            /* mov w, #-1 */
                uint32_t after_target = emit_pos(e);
                emit_patch_b26(e, b_off, after_target);
                break;
            }
            case ALU_SRL: { /* DIVU */
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                uint32_t cbz_off = emit_pos(e);
                emit_cbz_w32(e, rs2, 0);
                emit_udiv_w32(e, rd, rs1, rs2);
                uint32_t b_off = emit_pos(e);
                emit_b(e, 0);
                uint32_t divzero_target = emit_pos(e);
                emit_patch_cond19(e, cbz_off, divzero_target);
                emit_movn_w32(e, rd, 0, 0);            /* all-ones */
                uint32_t after_target = emit_pos(e);
                emit_patch_b26(e, b_off, after_target);
                break;
            }
            case ALU_OR: { /* REM */
                /* RV REM semantics fall out of SDIV+MSUB:
                 *   divisor==0:  SDIV→0, MSUB(rs1, 0, rs2) = rs1     ✓
                 *   INT_MIN/-1:  SDIV→INT_MIN, MSUB in 32-bit wraps  ✓
                 * MSUB needs rs1, rs2, rd (intermediate) all live, so
                 * compute the quotient into A_S2 first to avoid clobbering
                 * rs1 if rd aliases it. */
                emit_sdiv_w32(e, A_S2, rs1, rs2);
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                emit_msub_w32(e, rd, A_S2, rs2, rs1);
                break;
            }
            case ALU_AND: { /* REMU */
                emit_udiv_w32(e, A_S2, rs1, rs2);
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                emit_msub_w32(e, rd, A_S2, rs2, rs1);
                break;
            }
            default:
                return -1;
            }
        } else {
            /* I extension reg-reg ops. funct7==0x20 distinguishes SUB/SRA. */
            a64_reg_t rd = rc_write(e, rc, insn->rd);
            switch (insn->funct3) {
            case ALU_ADD:
                if (insn->funct7 == 0x20) emit_sub_w32(e, rd, rs1, rs2);
                else                       emit_add_w32(e, rd, rs1, rs2);
                break;
            case ALU_SLL:
                emit_lslv_w32(e, rd, rs1, rs2);
                break;
            case ALU_SLT:
                emit_cmp_w32_w32(e, rs1, rs2);
                emit_cset_w32(e, rd, A64_COND_LT);
                break;
            case ALU_SLTU:
                emit_cmp_w32_w32(e, rs1, rs2);
                emit_cset_w32(e, rd, A64_COND_LO);
                break;
            case ALU_XOR:
                emit_eor_w32(e, rd, rs1, rs2);
                break;
            case ALU_SRL:
                if (insn->funct7 == 0x20) emit_asrv_w32(e, rd, rs1, rs2);
                else                       emit_lsrv_w32(e, rd, rs1, rs2);
                break;
            case ALU_OR:
                emit_orr_w32(e, rd, rs1, rs2);
                break;
            case ALU_AND:
                emit_and_w32(e, rd, rs1, rs2);
                break;
            default:
                return -1;
            }
        }
        return 0;
    }

    case OP_LOAD: {
        a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
        a64_reg_t addr;
        if (insn->imm == 0) {
            addr = rs1;
        } else {
            add_w_imm32(e, A_S3, rs1, insn->imm);
            addr = A_S3;
        }
        a64_reg_t rd = rc_write(e, rc, insn->rd);
        switch (insn->funct3) {
        case LD_LB:  emit_ldrsb_w32_reg_uxtw(e, rd, A_MEM, addr); break;
        case LD_LH:  emit_ldrsh_w32_reg_uxtw(e, rd, A_MEM, addr); break;
        case LD_LW:  emit_ldr_w32_reg_uxtw  (e, rd, A_MEM, addr); break;
        case LD_LBU: emit_ldrb_reg_uxtw     (e, rd, A_MEM, addr); break;
        case LD_LHU: emit_ldrh_reg_uxtw     (e, rd, A_MEM, addr); break;
        default: return -1;
        }
        return 0;
    }

    case OP_STORE: {
        a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
        a64_reg_t rs2 = rc_read(e, rc, insn->rs2);
        a64_reg_t addr;
        if (insn->imm == 0) {
            addr = rs1;
        } else {
            add_w_imm32(e, A_S3, rs1, insn->imm);
            addr = A_S3;
        }
        switch (insn->funct3) {
        case ST_SB: emit_strb_reg_uxtw   (e, rs2, A_MEM, addr); break;
        case ST_SH: emit_strh_reg_uxtw   (e, rs2, A_MEM, addr); break;
        case ST_SW: emit_str_w32_reg_uxtw(e, rs2, A_MEM, addr); break;
        default: return -1;
        }
        return 0;
    }

    case OP_FENCE:
        /* Single-threaded guest, no host barrier needed. */
        return 0;

    case OP_FP_LOAD: {
        /* funct3=2 → FLW (4 bytes + NaN-box); funct3=3 → FLD (8 bytes). */
        a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
        a64_reg_t addr;
        if (insn->imm == 0) {
            addr = rs1;
        } else {
            add_w_imm32(e, A_S3, rs1, insn->imm);
            addr = A_S3;
        }
        if (insn->funct3 == 3) {
            emit_ldr_d_reg_uxtw(e, /*D0*/ 0, A_MEM, addr);
            store_fp_d(e, insn->rd, 0);
        } else {
            emit_ldr_s_reg_uxtw(e, /*S0*/ 0, A_MEM, addr);
            store_fp_s(e, insn->rd, 0);
        }
        return 0;
    }

    case OP_FP_STORE: {
        a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
        a64_reg_t addr;
        if (insn->imm == 0) {
            addr = rs1;
        } else {
            add_w_imm32(e, A_S3, rs1, insn->imm);
            addr = A_S3;
        }
        if (insn->funct3 == 3) {
            load_fp_d(e, /*D0*/ 0, insn->rs2);
            emit_str_d_reg_uxtw(e, 0, A_MEM, addr);
        } else {
            load_fp_s(e, /*S0*/ 0, insn->rs2);
            emit_str_s_reg_uxtw(e, 0, A_MEM, addr);
        }
        return 0;
    }

    /* RV → AArch64 FMA mnemonic mapping (signs differ — see emit_a64.h
     * comment block). FP registers don't go through the int cache. */
    case OP_FMADD: { /* RV: rd = rs1*rs2 + rs3  →  AArch64 FMADD */
        int fmt = insn->funct7 & 3;
        if (fmt == 0) {
            load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2); load_fp_s(e, 2, insn->rs3);
            emit_fmadd_s(e, 2, 0, 1, 2);
            store_fp_s(e, insn->rd, 2);
        } else {
            load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2); load_fp_d(e, 2, insn->rs3);
            emit_fmadd_d(e, 2, 0, 1, 2);
            store_fp_d(e, insn->rd, 2);
        }
        return 0;
    }
    case OP_FMSUB: { /* RV: rd = rs1*rs2 - rs3  →  AArch64 FNMSUB (Sn*Sm - Sa) */
        int fmt = insn->funct7 & 3;
        if (fmt == 0) {
            load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2); load_fp_s(e, 2, insn->rs3);
            emit_fnmsub_s(e, 2, 0, 1, 2);
            store_fp_s(e, insn->rd, 2);
        } else {
            load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2); load_fp_d(e, 2, insn->rs3);
            emit_fnmsub_d(e, 2, 0, 1, 2);
            store_fp_d(e, insn->rd, 2);
        }
        return 0;
    }
    case OP_FNMSUB: { /* RV: rd = -(rs1*rs2) + rs3  →  AArch64 FMSUB (Sa - Sn*Sm) */
        int fmt = insn->funct7 & 3;
        if (fmt == 0) {
            load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2); load_fp_s(e, 2, insn->rs3);
            emit_fmsub_s(e, 2, 0, 1, 2);
            store_fp_s(e, insn->rd, 2);
        } else {
            load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2); load_fp_d(e, 2, insn->rs3);
            emit_fmsub_d(e, 2, 0, 1, 2);
            store_fp_d(e, insn->rd, 2);
        }
        return 0;
    }
    case OP_FNMADD: { /* RV: rd = -(rs1*rs2) - rs3  →  AArch64 FNMADD */
        int fmt = insn->funct7 & 3;
        if (fmt == 0) {
            load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2); load_fp_s(e, 2, insn->rs3);
            emit_fnmadd_s(e, 2, 0, 1, 2);
            store_fp_s(e, insn->rd, 2);
        } else {
            load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2); load_fp_d(e, 2, insn->rs3);
            emit_fnmadd_d(e, 2, 0, 1, 2);
            store_fp_d(e, insn->rd, 2);
        }
        return 0;
    }

    case OP_FP: {
        int funct5 = insn->funct7 >> 2;
        int fmt    = insn->funct7 & 3;        /* 0=S, 1=D */

        switch (funct5) {
        case 0x00: /* FADD */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2);
                emit_fadd_s(e, 2, 0, 1);
                store_fp_s(e, insn->rd, 2);
            } else {
                load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2);
                emit_fadd_d(e, 2, 0, 1);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;
        case 0x01: /* FSUB */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2);
                emit_fsub_s(e, 2, 0, 1);
                store_fp_s(e, insn->rd, 2);
            } else {
                load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2);
                emit_fsub_d(e, 2, 0, 1);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;
        case 0x02: /* FMUL */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2);
                emit_fmul_s(e, 2, 0, 1);
                store_fp_s(e, insn->rd, 2);
            } else {
                load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2);
                emit_fmul_d(e, 2, 0, 1);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;
        case 0x03: /* FDIV */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2);
                emit_fdiv_s(e, 2, 0, 1);
                store_fp_s(e, insn->rd, 2);
            } else {
                load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2);
                emit_fdiv_d(e, 2, 0, 1);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;
        case 0x0B: /* FSQRT */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1);
                emit_fsqrt_s(e, 2, 0);
                store_fp_s(e, insn->rd, 2);
            } else {
                load_fp_d(e, 0, insn->rs1);
                emit_fsqrt_d(e, 2, 0);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;

        case 0x04: { /* FSGNJ / FSGNJN / FSGNJX */
            uint32_t off1 = FP_OFF(insn->rs1);
            uint32_t off2 = FP_OFF(insn->rs2);
            uint32_t offd = FP_OFF(insn->rd);
            if (fmt == 0) {
                emit_ldr_w32_imm(e, A_S0, A_CTX, off1);
                emit_ldr_w32_imm(e, A_S1, A_CTX, off2);
                emit_and_w32_imm(e, A_S0, A_S0, 0x7FFFFFFFu);
                switch (insn->funct3) {
                case 0:
                    emit_and_w32_imm(e, A_S1, A_S1, 0x80000000u);
                    break;
                case 1:
                    emit_mvn_w32(e, A_S1, A_S1);
                    emit_and_w32_imm(e, A_S1, A_S1, 0x80000000u);
                    break;
                case 2:
                    emit_ldr_w32_imm(e, A_S2, A_CTX, off1);
                    emit_eor_w32(e, A_S1, A_S2, A_S1);
                    emit_and_w32_imm(e, A_S1, A_S1, 0x80000000u);
                    break;
                }
                emit_orr_w32(e, A_S0, A_S0, A_S1);
                emit_str_w32_imm(e, A_S0, A_CTX, offd);
                emit_movn_w32(e, A_S1, 0, 0);
                emit_str_w32_imm(e, A_S1, A_CTX, offd + 4);
            } else {
                emit_ldr_x64_imm(e, A_S0, A_CTX, off1);
                emit_ldr_x64_imm(e, A_S1, A_CTX, off2);
                emit_lsl_x64_imm(e, A_S0, A_S0, 1);
                emit_lsr_x64_imm(e, A_S0, A_S0, 1);

                switch (insn->funct3) {
                case 0: break;
                case 1: emit_mvn_x64(e, A_S1, A_S1); break;
                case 2:
                    emit_ldr_x64_imm(e, A_S2, A_CTX, off1);
                    emit_eor_x64(e, A_S1, A_S2, A_S1);
                    break;
                }
                emit_lsr_x64_imm(e, A_S1, A_S1, 63);
                emit_orr_x64_lsl(e, A_S0, A_S0, A_S1, 63);
                emit_str_x64_imm(e, A_S0, A_CTX, offd);
            }
            return 0;
        }

        case 0x05: /* FMIN / FMAX */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2);
                if (insn->funct3 == 0) emit_fmin_s(e, 2, 0, 1);
                else                    emit_fmax_s(e, 2, 0, 1);
                store_fp_s(e, insn->rd, 2);
            } else {
                load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2);
                if (insn->funct3 == 0) emit_fmin_d(e, 2, 0, 1);
                else                    emit_fmax_d(e, 2, 0, 1);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;

        case 0x14: { /* FEQ / FLT / FLE → integer rd */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1); load_fp_s(e, 1, insn->rs2);
                emit_fcmp_s(e, 0, 1);
            } else {
                load_fp_d(e, 0, insn->rs1); load_fp_d(e, 1, insn->rs2);
                emit_fcmp_d(e, 0, 1);
            }
            a64_cond_t cond;
            switch (insn->funct3) {
            case 0:  cond = A64_COND_LS; break;  /* FLE */
            case 1:  cond = A64_COND_MI; break;  /* FLT */
            case 2:  cond = A64_COND_EQ; break;  /* FEQ */
            default: cond = A64_COND_AL;
            }
            a64_reg_t rd = rc_write(e, rc, insn->rd);
            emit_cset_w32(e, rd, cond);
            return 0;
        }

        case 0x18: { /* FCVT.W.S / FCVT.WU.S / FCVT.W.D / FCVT.WU.D */
            a64_reg_t rd = rc_write(e, rc, insn->rd);
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1);
                if (insn->rs2 == 0) emit_fcvtzs_w32_s(e, rd, 0);
                else                emit_fcvtzu_w32_s(e, rd, 0);
            } else {
                load_fp_d(e, 0, insn->rs1);
                if (insn->rs2 == 0) emit_fcvtzs_w32_d(e, rd, 0);
                else                emit_fcvtzu_w32_d(e, rd, 0);
            }
            return 0;
        }

        case 0x1A: { /* FCVT.S.W / FCVT.S.WU / FCVT.D.W / FCVT.D.WU */
            a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
            if (fmt == 0) {
                if (insn->rs2 == 0) emit_scvtf_s_w32(e, 2, rs1);
                else                emit_ucvtf_s_w32(e, 2, rs1);
                store_fp_s(e, insn->rd, 2);
            } else {
                if (insn->rs2 == 0) emit_scvtf_d_w32(e, 2, rs1);
                else                emit_ucvtf_d_w32(e, 2, rs1);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;
        }

        case 0x08: /* FCVT.S.D (single-from-double) when fmt==0;
                    * FCVT.D.S (double-from-single) when fmt==1 */
            if (fmt == 0) {
                load_fp_d(e, 0, insn->rs1);
                emit_fcvt_s_d(e, 2, 0);
                store_fp_s(e, insn->rd, 2);
            } else {
                load_fp_s(e, 0, insn->rs1);
                emit_fcvt_d_s(e, 2, 0);
                store_fp_d(e, insn->rd, 2);
            }
            return 0;

        case 0x1C: /* FMV.X.W (fmt=0, funct3=0) — bitcast f[rs1] low 32 to int */
            if (fmt == 0 && insn->funct3 == 0) {
                a64_reg_t rd = rc_write(e, rc, insn->rd);
                emit_ldr_w32_imm(e, rd, A_CTX, FP_OFF(insn->rs1));
                return 0;
            }
            return -1;  /* FCLASS — interpreter fallback */

        case 0x1E: /* FMV.W.X (fmt=0, funct3=0) — bitcast int rs1 → f[rd] (NaN-boxed) */
            if (fmt == 0 && insn->funct3 == 0) {
                a64_reg_t rs1 = rc_read(e, rc, insn->rs1);
                emit_str_w32_imm(e, rs1, A_CTX, FP_OFF(insn->rd));
                emit_movn_w32(e, A_S1, 0, 0);
                emit_str_w32_imm(e, A_S1, A_CTX, FP_OFF(insn->rd) + 4);
                return 0;
            }
            return -1;

        default:
            return -1;
        }
    }

    default:
        return -1;
    }
}

/* ---- Intrinsic native stubs ----
 *
 * When the guest binary's symbol table tells us where memcpy/memmove/
 * memset/strlen live, we replace the *entire* compiled implementation
 * with a tiny stub that converts the guest argument addresses to host
 * pointers and BLRs to libc. The stub returns through the guest's
 * link register (x[1]) via the indirect-cache probe, so subsequent
 * iterations chain straight to the caller's continuation.
 *
 * Stubs don't use the register cache — they read ctx directly. The BLR to
 * libc would otherwise clobber the caller-saved cache slot (X15), but
 * since regular blocks always flush the cache before chaining, X15 is
 * dead by the time the stub starts.
 */

static uint8_t *emit_memcpy_stub(dbt_state_t *dbt, int is_memmove) {
    emit_t e = { .buf = dbt->code_buf + dbt->code_used,
                 .offset = 0,
                 .capacity = CODE_BUF_SIZE - dbt->code_used };
    uint8_t *block_start = e.buf;

    emit_stp_pre_sp(&e, A64_W29, A64_W30, -16);

    emit_ldr_w32_imm(&e, A64_W0, A_CTX, 10 * 4);
    emit_ldr_w32_imm(&e, A64_W1, A_CTX, 11 * 4);
    emit_ldr_w32_imm(&e, A64_W2, A_CTX, 12 * 4);

    emit_add_x64_w32_uxtw(&e, A64_W0, A_MEM, A64_W0);
    emit_add_x64_w32_uxtw(&e, A64_W1, A_MEM, A64_W1);

    void *fn = is_memmove ? (void *)memmove : (void *)memcpy;
    emit_mov_x64_imm64(&e, A64_W9, (uint64_t)(uintptr_t)fn);
    emit_blr(&e, A64_W9);

    emit_ldp_post_sp(&e, A64_W29, A64_W30, 16);

    emit_ldr_w32_imm(&e, A64_W14, A_CTX, 1 * 4);
    chained_exit_indirect(&e, A64_W14);

    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    __builtin___clear_cache((char *)block_start, (char *)block_start + e.offset);
    return block_start;
}

static uint8_t *emit_memset_stub(dbt_state_t *dbt) {
    emit_t e = { .buf = dbt->code_buf + dbt->code_used,
                 .offset = 0,
                 .capacity = CODE_BUF_SIZE - dbt->code_used };
    uint8_t *block_start = e.buf;

    emit_stp_pre_sp(&e, A64_W29, A64_W30, -16);

    emit_ldr_w32_imm(&e, A64_W0, A_CTX, 10 * 4);
    emit_ldr_w32_imm(&e, A64_W1, A_CTX, 11 * 4);
    emit_ldr_w32_imm(&e, A64_W2, A_CTX, 12 * 4);
    emit_add_x64_w32_uxtw(&e, A64_W0, A_MEM, A64_W0);

    emit_mov_x64_imm64(&e, A64_W9, (uint64_t)(uintptr_t)memset);
    emit_blr(&e, A64_W9);

    emit_ldp_post_sp(&e, A64_W29, A64_W30, 16);

    emit_ldr_w32_imm(&e, A64_W14, A_CTX, 1 * 4);
    chained_exit_indirect(&e, A64_W14);

    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    __builtin___clear_cache((char *)block_start, (char *)block_start + e.offset);
    return block_start;
}

static uint8_t *emit_strlen_stub(dbt_state_t *dbt) {
    emit_t e = { .buf = dbt->code_buf + dbt->code_used,
                 .offset = 0,
                 .capacity = CODE_BUF_SIZE - dbt->code_used };
    uint8_t *block_start = e.buf;

    emit_stp_pre_sp(&e, A64_W29, A64_W30, -16);

    emit_ldr_w32_imm(&e, A64_W0, A_CTX, 10 * 4);
    emit_add_x64_w32_uxtw(&e, A64_W0, A_MEM, A64_W0);

    emit_mov_x64_imm64(&e, A64_W9, (uint64_t)(uintptr_t)strlen);
    emit_blr(&e, A64_W9);

    emit_str_w32_imm(&e, A64_W0, A_CTX, 10 * 4);

    emit_ldp_post_sp(&e, A64_W29, A64_W30, 16);

    emit_ldr_w32_imm(&e, A64_W14, A_CTX, 1 * 4);
    chained_exit_indirect(&e, A64_W14);

    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    __builtin___clear_cache((char *)block_start, (char *)block_start + e.offset);
    return block_start;
}

uint8_t *dbt_translate_block(dbt_state_t *dbt, uint32_t guest_pc) {
    /* Intercept intrinsic functions with native stubs */
    if (dbt->intrinsic_memcpy  && guest_pc == dbt->intrinsic_memcpy)
        return emit_memcpy_stub(dbt, 0);
    if (dbt->intrinsic_memmove && guest_pc == dbt->intrinsic_memmove)
        return emit_memcpy_stub(dbt, 1);
    if (dbt->intrinsic_memset  && guest_pc == dbt->intrinsic_memset)
        return emit_memset_stub(dbt);
    if (dbt->intrinsic_strlen  && guest_pc == dbt->intrinsic_strlen)
        return emit_strlen_stub(dbt);

    /* Conservative "block fits in remaining buffer" check. A worst-case
     * block is ~64 instructions × ~12 host instructions = ~3 KB. */
    if (dbt->code_used + 8192 > CODE_BUF_SIZE) {
        fprintf(stderr, "rv32-run: JIT code buffer exhausted\n");
        exit(1);
    }
    uint8_t *block_start = dbt->code_buf + dbt->code_used;
    emit_t e = { .buf = block_start, .offset = 0,
                 .capacity = CODE_BUF_SIZE - dbt->code_used };

    reg_cache_t rc;
    rc_init(&rc);

    side_exit_t side_exits[MAX_SIDE_EXITS];
    int num_side_exits = 0;

    /* Branch state set up by OP_BRANCH and the SLT+branch fusion before
     * jumping to emit_branch. cmp/cset have already been emitted; flags
     * are live; the cache reflects the post-cmp state. */
    a64_cond_t branch_cond = A64_COND_AL;
    uint32_t   branch_fall_pc  = 0;
    uint32_t   branch_taken_pc = 0;

    uint32_t pc = guest_pc;
    int insns = 0;

    for (; insns < MAX_BLOCK_INSNS; insns++) {
        if (pc + 4 > dbt->bin->code_end) {
            rc_flush(&e, &rc);
            exit_with_pc(&e, pc | 2u);
            goto done;
        }

        uint32_t word;
        memcpy(&word, dbt->bin->memory + pc, 4);
        rv32_insn_t insn;
        rv32_decode(word, &insn);

        /* ---- LUI / AUIPC + ADDI fusion ----
         * GCC emits these pairs to materialize 32-bit constants and
         * absolute addresses. When the second insn's rd matches the
         * first's rd, the combined value is known at translate time —
         * one MOV-imm32 into the cache instead of two ALU instructions
         * plus a ctx round-trip. */
        if ((insn.opcode == OP_LUI || insn.opcode == OP_AUIPC)
            && insn.rd != 0
            && pc + 8 <= dbt->bin->code_end) {
            uint32_t w2;
            memcpy(&w2, dbt->bin->memory + pc + 4, 4);
            rv32_insn_t i2;
            rv32_decode(w2, &i2);
            if (i2.opcode == OP_IMM
                && i2.funct3 == ALU_ADDI
                && i2.rs1 == insn.rd
                && i2.rd  == insn.rd) {
                uint32_t base = (insn.opcode == OP_LUI) ? 0u : pc;
                uint32_t value = base + (uint32_t)insn.imm + (uint32_t)i2.imm;
                a64_reg_t rd = rc_write(&e, &rc, insn.rd);
                emit_mov_w32_imm32(&e, rd, value);
                pc += 8;
                insns += 1;
                continue;
            }
        }

        /* ---- SLT(I)/SLTU(I) + BEQ/BNE x0 fusion ----
         * GCC compiles  `if (a < b) goto L`  as  `slt t, a, b; bne t, x0, L`.
         * The branch's cmp(rd, x0) is redundant — the SLT cmp already set
         * the right NZCV. Fold to one cmp + b.cond, optionally retaining
         * the cset for rd if it's live (cset doesn't disturb flags).
         *
         * Branch direction maps as:
         *   BNE rd, x0   (branch if SLT was true)  → COND_LT / COND_LO
         *   BEQ rd, x0   (branch if SLT was false) → COND_GE / COND_HS
         */
        if (((insn.opcode == OP_REG && insn.funct7 == 0
              && (insn.funct3 == ALU_SLT || insn.funct3 == ALU_SLTU))
             || (insn.opcode == OP_IMM
                 && (insn.funct3 == ALU_SLTI || insn.funct3 == ALU_SLTIU)))
            && insn.rd != 0
            && pc + 8 <= dbt->bin->code_end) {
            uint32_t nw;
            memcpy(&nw, dbt->bin->memory + pc + 4, 4);
            rv32_insn_t ni;
            rv32_decode(nw, &ni);
            if (ni.opcode == OP_BRANCH
                && (ni.funct3 == BR_BEQ || ni.funct3 == BR_BNE)
                && ((ni.rs1 == insn.rd && ni.rs2 == 0)
                    || (ni.rs2 == insn.rd && ni.rs1 == 0))) {
                int is_signed = (insn.opcode == OP_REG)
                    ? (insn.funct3 == ALU_SLT)
                    : (insn.funct3 == ALU_SLTI);

                /* Emit the SLT comparison directly (sets NZCV). */
                if (insn.opcode == OP_REG) {
                    a64_reg_t rs1 = rc_read(&e, &rc, insn.rs1);
                    a64_reg_t rs2 = rc_read(&e, &rc, insn.rs2);
                    emit_cmp_w32_w32(&e, rs1, rs2);
                } else {
                    a64_reg_t rs1 = rc_read(&e, &rc, insn.rs1);
                    if (is_signed) cmp_w_imm32_signed  (&e, rs1, insn.imm);
                    else           cmp_w_imm32_unsigned(&e, rs1, insn.imm);
                }

                /* Materialize the SLT result; cset doesn't touch NZCV. */
                a64_reg_t rd = rc_write(&e, &rc, insn.rd);
                emit_cset_w32(&e, rd, is_signed ? A64_COND_LT : A64_COND_LO);

                if (ni.funct3 == BR_BNE)
                    branch_cond = is_signed ? A64_COND_LT : A64_COND_LO;
                else /* BR_BEQ */
                    branch_cond = is_signed ? A64_COND_GE : A64_COND_HS;

                branch_fall_pc  = pc + 8;
                branch_taken_pc = (uint32_t)((pc + 4) + ni.imm);
                pc = branch_fall_pc;
                insns += 1;  /* +1 here, for-loop's post-step adds the other */
                goto emit_branch;
            }
        }

        switch (insn.opcode) {
        case OP_JAL: {
            if (insn.rd != 0) {
                a64_reg_t rd = rc_write(&e, &rc, insn.rd);
                emit_mov_w32_imm32(&e, rd, pc + 4);
            }
            rc_flush(&e, &rc);
            chained_exit_known(&e, (uint32_t)(pc + insn.imm));
            goto done;
        }

        case OP_JALR: {
            /* target = (rs1 + imm) & ~1; link rd before exit (handles rd==rs1).
             * We compute the target into W14 (outside the chained-exit
             * scratch range A_S0..A_S3) so the helper can use those freely. */
            a64_reg_t rs1 = rc_read(&e, &rc, insn.rs1);
            add_w_imm32(&e, A64_W14, rs1, insn.imm);
            if (!emit_and_w32_imm(&e, A64_W14, A64_W14, 0xFFFFFFFEu)) {
                emit_mov_w32_imm32(&e, A_S1, 0xFFFFFFFEu);
                emit_and_w32(&e, A64_W14, A64_W14, A_S1);
            }
            if (insn.rd != 0) {
                a64_reg_t rd = rc_write(&e, &rc, insn.rd);
                emit_mov_w32_imm32(&e, rd, pc + 4);
            }
            rc_flush(&e, &rc);
            chained_exit_indirect(&e, A64_W14);
            goto done;
        }

        case OP_BRANCH: {
            a64_reg_t rs1 = rc_read(&e, &rc, insn.rs1);
            a64_reg_t rs2 = rc_read(&e, &rc, insn.rs2);
            /* No flush yet: emit_branch may want to keep cache state for
             * the superblock fall-through. The fallback path flushes
             * before its own b.cond/chained-exit pair. */
            emit_cmp_w32_w32(&e, rs1, rs2);
            switch (insn.funct3) {
            case BR_BEQ:  branch_cond = A64_COND_EQ; break;
            case BR_BNE:  branch_cond = A64_COND_NE; break;
            case BR_BLT:  branch_cond = A64_COND_LT; break;
            case BR_BGE:  branch_cond = A64_COND_GE; break;
            case BR_BLTU: branch_cond = A64_COND_LO; break;
            case BR_BGEU: branch_cond = A64_COND_HS; break;
            default:
                rc_flush(&e, &rc);
                exit_with_pc(&e, pc | 2u);
                goto done;
            }
            branch_fall_pc  = pc + 4;
            branch_taken_pc = (uint32_t)(pc + insn.imm);
            pc = branch_fall_pc;
            goto emit_branch;
        }

        case OP_SYSTEM: {
            /* funct3==0: ECALL (imm==0) or EBREAK (imm==1). The dispatcher
             * recognizes the encoded next_pc tags `(pc+4)|1` for ECALL and
             * `(pc+4)|2` for EBREAK. funct3!=0 is a CSR op — for the
             * microcontroller profile we accept them as no-ops and
             * advance, returning 0 to rd. */
            if (insn.funct3 == 0) {
                rc_flush(&e, &rc);
                uint32_t advanced = pc + 4;
                if (insn.imm == 1)
                    exit_with_pc(&e, advanced | 2u);
                else
                    exit_with_pc(&e, advanced | 1u);
                goto done;
            }
            /* CSR — fake a 0 read, ignore writes. */
            if (insn.rd != 0) {
                a64_reg_t rd = rc_write(&e, &rc, insn.rd);
                emit_mov_w32_imm32(&e, rd, 0);
            }
            pc += 4;
            continue;
        }

        default: {
            int rc_status = translate_one(&e, &rc, &insn, pc);
            if (rc_status != 0) {
                /* Unhandled opcode (e.g. an FP form we don't translate
                 * yet). Bail out: signal EBREAK so the user sees a clear
                 * "JIT can't handle this" message. */
                rc_flush(&e, &rc);
                exit_with_pc(&e, pc | 2u);
                goto done;
            }
            pc += 4;
            continue;
        }
        }
        continue;

        /* ---- Shared branch tail ----
         * Reached via `goto emit_branch` from OP_BRANCH or the SLT+branch
         * fusion. Caller has emitted the cmp (NZCV is live) and set:
         *   branch_cond, branch_fall_pc, branch_taken_pc, pc=branch_fall_pc.
         *
         * Strategy: if there's a side-exit slot left and budget for at
         * least a few more guest insns, register the taken side as a side
         * exit and continue translating fall-through. Otherwise emit the
         * regular two-exit branch ending the block. */
    emit_branch:
        if (num_side_exits < MAX_SIDE_EXITS && insns < MAX_BLOCK_INSNS - 4) {
            uint32_t bp = emit_pos(&e);
            emit_b_cond(&e, branch_cond, 0);
            side_exits[num_side_exits].bcond_patch = bp;
            side_exits[num_side_exits].target_pc   = branch_taken_pc;
            memcpy(side_exits[num_side_exits].snapshot,
                   rc.slots, sizeof(rc.slots));
            num_side_exits++;
            continue;
        }
        /* Fallback: end the block with a normal two-target branch. */
        rc_flush(&e, &rc);
        {
            uint32_t bcond_off = emit_pos(&e);
            emit_b_cond(&e, branch_cond, 0);
            chained_exit_known(&e, branch_fall_pc);
            uint32_t taken_off = emit_pos(&e);
            emit_patch_cond19(&e, bcond_off, taken_off);
            chained_exit_known(&e, branch_taken_pc);
        }
        goto done;
    }

    /* Hit MAX_BLOCK_INSNS without a control-flow instruction. Chain
     * straight to the next pc — long straight-line code is common in
     * compiler-generated initialisers. */
    rc_flush(&e, &rc);
    chained_exit_known(&e, pc);

done:
    /* Cold stubs for any unfilled side exits. Each stub patches its
     * b.cond to land here, replays the per-side-exit snapshot's dirty
     * writebacks (only the dirty slots matter — clean cached values are
     * already coherent in ctx), then chains to the side-exit target. */
    for (int i = 0; i < num_side_exits; i++) {
        emit_patch_cond19(&e, side_exits[i].bcond_patch, emit_pos(&e));
        rc_flush_snapshot(&e, side_exits[i].snapshot);
        chained_exit_known(&e, side_exits[i].target_pc);
    }
    if (num_side_exits > 0) {
        dbt->superblock_count++;
        dbt->side_exits_total += (uint64_t)num_side_exits;
    }

    dbt->code_used += e.offset;
    dbt->blocks_translated++;
    /* AArch64 has split I/D caches: any code we just wrote into the
     * buffer is in D-cache only; we need to drain it to memory and
     * invalidate the I-cache range before letting the CPU fetch from it.
     * GCC's __builtin___clear_cache emits the right `dc cvau` / `ic ivau`
     * / `dsb` / `isb` sequence on aarch64. */
    __builtin___clear_cache((char *)block_start, (char *)block_start + e.offset);
    return block_start;
}

/* ---- Trampoline ----
 *
 * Called from dbt_run() as a C function with the System V / AArch64 ABI:
 *   void(*)(rv32_ctx_t *ctx, uint8_t *mem, void *block, void *cache);
 *   args:  x0=ctx, x1=mem, x2=block, x3=cache
 *
 * Saves callee-saved regs (X19-X28 plus X29/X30), loads the host register
 * convention, BLRs into the block, and unwinds on return. Each translated
 * block ends with RET, which lands right after the BLR here. The cache
 * slots X22-X28 are saved/restored here so blocks can use them freely.
 * (Slot 7 = X15 is caller-saved; no save needed.)
 */
void dbt_emit_trampoline(dbt_state_t *dbt) {
    emit_t e = { .buf = dbt->code_buf, .offset = 0, .capacity = CODE_BUF_SIZE };

    /* Frame: 96 bytes (6 × 16-byte pairs).
     *   [sp+ 0]  x29 (fp), x30 (lr)
     *   [sp+16]  x19, x20  (ctx, mem base)
     *   [sp+32]  x21, x22  (cache base, cache slot 0)
     *   [sp+48]  x23, x24
     *   [sp+64]  x25, x26
     *   [sp+80]  x27, x28
     */
    emit_stp_pre_sp (&e, A64_W29, A64_W30, -96);
    emit_stp_x64_off(&e, A64_W19, A64_W20, A64_SP, 16);
    emit_stp_x64_off(&e, A64_W21, A64_W22, A64_SP, 32);
    emit_stp_x64_off(&e, A64_W23, A64_W24, A64_SP, 48);
    emit_stp_x64_off(&e, A64_W25, A64_W26, A64_SP, 64);
    emit_stp_x64_off(&e, A64_W27, A64_W28, A64_SP, 80);

    /* Load host register convention */
    emit_mov_x64_x64(&e, A64_W19, A64_W0);   /* ctx */
    emit_mov_x64_x64(&e, A64_W20, A64_W1);   /* mem */
    emit_mov_x64_x64(&e, A64_W21, A64_W3);   /* cache */

    /* Call the translated block. Each block ends with RET, returning to
     * the next instruction here. */
    emit_blr(&e, A64_W2);

    /* Unwind */
    emit_ldp_x64_off(&e, A64_W27, A64_W28, A64_SP, 80);
    emit_ldp_x64_off(&e, A64_W25, A64_W26, A64_SP, 64);
    emit_ldp_x64_off(&e, A64_W23, A64_W24, A64_SP, 48);
    emit_ldp_x64_off(&e, A64_W21, A64_W22, A64_SP, 32);
    emit_ldp_x64_off(&e, A64_W19, A64_W20, A64_SP, 16);
    emit_ldp_post_sp(&e, A64_W29, A64_W30, 96);
    emit_ret(&e);

    dbt->code_used = e.offset;
    __builtin___clear_cache((char *)dbt->code_buf, (char *)dbt->code_buf + e.offset);
}

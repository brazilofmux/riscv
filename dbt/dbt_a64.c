/* dbt_a64.c — AArch64 code emission for the RV32IMFD DBT.
 *
 * Stage: P2 baseline. Integer-only translator, naive load/store style
 * (every guest register access goes through ctx memory; no register cache
 * — that's a P4 optimization). FP/D opcodes fall back to the interpreter
 * via a tagged exit until P3 lands. Block chaining, RAS, fusion, and
 * intrinsic stubs all wait for P4.
 *
 * Host register convention (matches the trampoline below):
 *   X19 = pointer to rv32_ctx_t           (callee-saved, set by trampoline)
 *   X20 = guest memory base               (callee-saved)
 *   X21 = block cache base                (callee-saved; reserved for P4)
 *   W9  = scratch / source operand 1
 *   W10 = scratch / source operand 2
 *   W11 = scratch / result before store
 *   W12 = scratch / effective address / temp
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

/* ---- Small helpers ---- */

/* Load guest x[g] into `scratch` and return scratch — except for x[0]
 * which is hardwired to zero, in which case WZR is returned and no
 * instruction is emitted. */
static a64_reg_t load_gpr(emit_t *e, a64_reg_t scratch, int g) {
    if (g == 0) return A64_WZR;
    emit_ldr_w32_imm(e, scratch, A_CTX, (uint32_t)(g * 4));
    return scratch;
}

/* Store W register `src` into guest x[g]. Writes to x0 are NOPs. */
static void store_gpr(emit_t *e, int g, a64_reg_t src) {
    if (g == 0) return;
    emit_str_w32_imm(e, src, A_CTX, (uint32_t)(g * 4));
}

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
        /* ZR-as-rn would be SP-as-rn for cmp-imm: route through a scratch. */
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
 * 4 bytes are 0xFFFFFFFF. Doubles take the full 8 bytes.
 *
 * FP register convention (no FP register cache yet):
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

/* Tail of every block exit: store next_pc, return to dispatcher. */
static void exit_with_pc(emit_t *e, uint32_t next_pc) {
    emit_mov_w32_imm32(e, A_S0, next_pc);
    emit_str_w32_imm(e, A_S0, A_CTX, CTX_NEXT_PC_OFF);
    emit_ret(e);
}

/* Same, but next_pc already lives in a W register. */
static void exit_with_w(emit_t *e, a64_reg_t pc_w) {
    emit_str_w32_imm(e, pc_w, A_CTX, CTX_NEXT_PC_OFF);
    emit_ret(e);
}

/* ---- Per-instruction translation ----
 *
 * Returns 0 on success, -1 if the opcode isn't handled (FP, unknown, …).
 * The caller is responsible for handling control-flow opcodes (JAL,
 * JALR, BRANCH, SYSTEM) directly — they end the block. translate_one
 * only deals with straight-line ops.
 */
static int translate_one(emit_t *e, rv32_insn_t *insn, uint32_t pc) {
    switch (insn->opcode) {

    case OP_LUI: {
        if (insn->rd == 0) return 0;
        emit_mov_w32_imm32(e, A_S2, (uint32_t)insn->imm);
        store_gpr(e, insn->rd, A_S2);
        return 0;
    }

    case OP_AUIPC: {
        if (insn->rd == 0) return 0;
        emit_mov_w32_imm32(e, A_S2, pc + (uint32_t)insn->imm);
        store_gpr(e, insn->rd, A_S2);
        return 0;
    }

    case OP_IMM: {
        a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
        switch (insn->funct3) {
        case ALU_ADDI:
            add_w_imm32(e, A_S2, rs1, insn->imm);
            break;
        case ALU_SLTI:
            cmp_w_imm32_signed(e, rs1, insn->imm);
            emit_cset_w32(e, A_S2, A64_COND_LT);
            break;
        case ALU_SLTIU:
            /* RISC-V SLTIU sign-extends the 12-bit imm then treats both
             * operands as unsigned. */
            cmp_w_imm32_unsigned(e, rs1, insn->imm);
            emit_cset_w32(e, A_S2, A64_COND_LO);
            break;
        case ALU_XORI:
            if (!emit_eor_w32_imm(e, A_S2, rs1, (uint32_t)insn->imm)) {
                emit_mov_w32_imm32(e, A_S1, (uint32_t)insn->imm);
                emit_eor_w32(e, A_S2, rs1, A_S1);
            }
            break;
        case ALU_ORI:
            if (!emit_orr_w32_imm(e, A_S2, rs1, (uint32_t)insn->imm)) {
                emit_mov_w32_imm32(e, A_S1, (uint32_t)insn->imm);
                emit_orr_w32(e, A_S2, rs1, A_S1);
            }
            break;
        case ALU_ANDI:
            if (!emit_and_w32_imm(e, A_S2, rs1, (uint32_t)insn->imm)) {
                emit_mov_w32_imm32(e, A_S1, (uint32_t)insn->imm);
                emit_and_w32(e, A_S2, rs1, A_S1);
            }
            break;
        case ALU_SLLI:
            emit_lsl_w32_imm(e, A_S2, rs1, (uint32_t)(insn->imm & 31));
            break;
        case ALU_SRLI:  /* SRLI when funct7=0x00, SRAI when funct7=0x20 */
            if (insn->funct7 == 0x20)
                emit_asr_w32_imm(e, A_S2, rs1, (uint32_t)(insn->imm & 31));
            else
                emit_lsr_w32_imm(e, A_S2, rs1, (uint32_t)(insn->imm & 31));
            break;
        default:
            return -1;
        }
        store_gpr(e, insn->rd, A_S2);
        return 0;
    }

    case OP_REG: {
        a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
        a64_reg_t rs2 = load_gpr(e, A_S1, insn->rs2);

        if (insn->funct7 == 0x01) {
            /* M extension: MUL/MULH/MULHSU/MULHU/DIV/DIVU/REM/REMU */
            switch (insn->funct3) {
            case ALU_ADD:   /* MUL */
                emit_mul_w32(e, A_S2, rs1, rs2);
                break;
            case ALU_SLL: { /* MULH (signed × signed → high 32) */
                emit_smull(e, A_S2, rs1, rs2);
                emit_lsr_x64_imm(e, A_S2, A_S2, 32);
                break;
            }
            case ALU_SLT: { /* MULHSU (signed × unsigned → high 32) */
                emit_sxtw_x64_w32(e, A_S2, rs1);    /* X_S2 = sign-extend(rs1) */
                emit_mov_w32_w32 (e, A_S3, rs2);    /* X_S3 = zero-extend(rs2) (W move clears upper) */
                emit_mul_x64     (e, A_S2, A_S2, A_S3);
                emit_lsr_x64_imm (e, A_S2, A_S2, 32);
                break;
            }
            case ALU_SLTU:  /* MULHU (unsigned × unsigned → high 32) */
                emit_umull(e, A_S2, rs1, rs2);
                emit_lsr_x64_imm(e, A_S2, A_S2, 32);
                break;
            case ALU_XOR: { /* DIV */
                /* RV: divisor==0 → -1; AArch64 SDIV gives 0. Branch around. */
                uint32_t cbz_off = emit_pos(e);
                emit_cbz_w32(e, rs2, 0);                /* patched later */
                emit_sdiv_w32(e, A_S2, rs1, rs2);
                uint32_t b_off = emit_pos(e);
                emit_b(e, 0);                            /* patched */
                uint32_t divzero_target = emit_pos(e);
                emit_patch_cond19(e, cbz_off, divzero_target);
                emit_movn_w32(e, A_S2, 0, 0);            /* mov w, #-1 */
                uint32_t after_target = emit_pos(e);
                emit_patch_b26(e, b_off, after_target);
                break;
            }
            case ALU_SRL: { /* DIVU */
                /* RV: divisor==0 → 2^32-1; AArch64 UDIV gives 0. */
                uint32_t cbz_off = emit_pos(e);
                emit_cbz_w32(e, rs2, 0);
                emit_udiv_w32(e, A_S2, rs1, rs2);
                uint32_t b_off = emit_pos(e);
                emit_b(e, 0);
                uint32_t divzero_target = emit_pos(e);
                emit_patch_cond19(e, cbz_off, divzero_target);
                emit_movn_w32(e, A_S2, 0, 0);            /* all-ones = (uint32_t)-1 */
                uint32_t after_target = emit_pos(e);
                emit_patch_b26(e, b_off, after_target);
                break;
            }
            case ALU_OR: {  /* REM */
                /* RV REM semantics fall out of SDIV+MSUB:
                 *   divisor==0:  SDIV→0, MSUB(rs1, 0, rs2) = rs1     ✓
                 *   INT_MIN/-1:  SDIV→INT_MIN, MSUB in 32-bit wraps  ✓
                 */
                emit_sdiv_w32(e, A_S2, rs1, rs2);
                emit_msub_w32(e, A_S2, A_S2, rs2, rs1);
                break;
            }
            case ALU_AND: { /* REMU */
                emit_udiv_w32(e, A_S2, rs1, rs2);
                emit_msub_w32(e, A_S2, A_S2, rs2, rs1);
                break;
            }
            default:
                return -1;
            }
        } else {
            /* I extension reg-reg ops. funct7==0x20 distinguishes SUB/SRA. */
            switch (insn->funct3) {
            case ALU_ADD:
                if (insn->funct7 == 0x20) emit_sub_w32(e, A_S2, rs1, rs2);
                else                       emit_add_w32(e, A_S2, rs1, rs2);
                break;
            case ALU_SLL:
                emit_lslv_w32(e, A_S2, rs1, rs2);
                break;
            case ALU_SLT:
                emit_cmp_w32_w32(e, rs1, rs2);
                emit_cset_w32(e, A_S2, A64_COND_LT);
                break;
            case ALU_SLTU:
                emit_cmp_w32_w32(e, rs1, rs2);
                emit_cset_w32(e, A_S2, A64_COND_LO);
                break;
            case ALU_XOR:
                emit_eor_w32(e, A_S2, rs1, rs2);
                break;
            case ALU_SRL:
                if (insn->funct7 == 0x20) emit_asrv_w32(e, A_S2, rs1, rs2);
                else                       emit_lsrv_w32(e, A_S2, rs1, rs2);
                break;
            case ALU_OR:
                emit_orr_w32(e, A_S2, rs1, rs2);
                break;
            case ALU_AND:
                emit_and_w32(e, A_S2, rs1, rs2);
                break;
            default:
                return -1;
            }
        }
        store_gpr(e, insn->rd, A_S2);
        return 0;
    }

    case OP_LOAD: {
        a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
        a64_reg_t addr;
        if (insn->imm == 0) {
            addr = rs1;
        } else {
            add_w_imm32(e, A_S3, rs1, insn->imm);
            addr = A_S3;
        }
        switch (insn->funct3) {
        case LD_LB:  emit_ldrsb_w32_reg_uxtw(e, A_S2, A_MEM, addr); break;
        case LD_LH:  emit_ldrsh_w32_reg_uxtw(e, A_S2, A_MEM, addr); break;
        case LD_LW:  emit_ldr_w32_reg_uxtw  (e, A_S2, A_MEM, addr); break;
        case LD_LBU: emit_ldrb_reg_uxtw     (e, A_S2, A_MEM, addr); break;
        case LD_LHU: emit_ldrh_reg_uxtw     (e, A_S2, A_MEM, addr); break;
        default: return -1;
        }
        store_gpr(e, insn->rd, A_S2);
        return 0;
    }

    case OP_STORE: {
        a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
        a64_reg_t rs2 = load_gpr(e, A_S1, insn->rs2);
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
        a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
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
        a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
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
     * comment block). */
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
            /* Sign injection: bit-twiddle on the integer view of the FP
             * bits. Cheaper than going through FP regs. */
            uint32_t off1 = FP_OFF(insn->rs1);
            uint32_t off2 = FP_OFF(insn->rs2);
            uint32_t offd = FP_OFF(insn->rd);
            if (fmt == 0) {
                /* Single: 32-bit ops, take sign bit 31. */
                emit_ldr_w32_imm(e, A_S0, A_CTX, off1);     /* W9  = rs1 bits */
                emit_ldr_w32_imm(e, A_S1, A_CTX, off2);     /* W10 = rs2 bits */
                /* Magnitude = rs1 & 0x7FFFFFFF — encodable as logical-imm */
                emit_and_w32_imm(e, A_S0, A_S0, 0x7FFFFFFFu);
                switch (insn->funct3) {
                case 0: /* FSGNJ: take sign from rs2 */
                    emit_and_w32_imm(e, A_S1, A_S1, 0x80000000u);
                    break;
                case 1: /* FSGNJN: inverted sign of rs2 */
                    emit_mvn_w32(e, A_S1, A_S1);
                    emit_and_w32_imm(e, A_S1, A_S1, 0x80000000u);
                    break;
                case 2: /* FSGNJX: XOR signs of rs1 and rs2 */
                    emit_ldr_w32_imm(e, A_S2, A_CTX, off1);          /* reload rs1 */
                    emit_eor_w32(e, A_S1, A_S2, A_S1);               /* sign(rs1)^sign(rs2) merged with low bits */
                    emit_and_w32_imm(e, A_S1, A_S1, 0x80000000u);
                    break;
                }
                emit_orr_w32(e, A_S0, A_S0, A_S1);
                emit_str_w32_imm(e, A_S0, A_CTX, offd);
                emit_movn_w32(e, A_S1, 0, 0);                         /* NaN-box */
                emit_str_w32_imm(e, A_S1, A_CTX, offd + 4);
            } else {
                /* Double: 64-bit ops on the raw integer view of the FP
                 * bits. Pattern:
                 *   X9  = rs1 bits
                 *   X10 = rs2 bits  (or NOT rs2, or rs1^rs2 — depends on funct3)
                 *   magnitude = X9 LSL #1 LSR #1    (clear bit 63)
                 *   sign      = X10 LSR #63 LSL #63  (keep only bit 63)
                 *   result    = magnitude | sign
                 */
                emit_ldr_x64_imm(e, A_S0, A_CTX, off1);
                emit_ldr_x64_imm(e, A_S1, A_CTX, off2);
                emit_lsl_x64_imm(e, A_S0, A_S0, 1);
                emit_lsr_x64_imm(e, A_S0, A_S0, 1);

                switch (insn->funct3) {
                case 0: break;                              /* FSGNJ : sign = rs2's bit 63 */
                case 1: emit_mvn_x64(e, A_S1, A_S1); break; /* FSGNJN: invert rs2 first */
                case 2:                                      /* FSGNJX: sign = (rs1 ^ rs2)'s bit 63 */
                    emit_ldr_x64_imm(e, A_S2, A_CTX, off1);
                    emit_eor_x64(e, A_S1, A_S2, A_S1);
                    break;
                }
                emit_lsr_x64_imm(e, A_S1, A_S1, 63);          /* keep only sign bit at pos 0 */
                emit_orr_x64_lsl(e, A_S0, A_S0, A_S1, 63);    /* magnitude | (sign << 63) */
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
            emit_cset_w32(e, A_S2, cond);
            store_gpr(e, insn->rd, A_S2);
            return 0;
        }

        case 0x18: { /* FCVT.W.S / FCVT.WU.S / FCVT.W.D / FCVT.WU.D */
            if (fmt == 0) {
                load_fp_s(e, 0, insn->rs1);
                if (insn->rs2 == 0) emit_fcvtzs_w32_s(e, A_S2, 0);
                else                emit_fcvtzu_w32_s(e, A_S2, 0);
            } else {
                load_fp_d(e, 0, insn->rs1);
                if (insn->rs2 == 0) emit_fcvtzs_w32_d(e, A_S2, 0);
                else                emit_fcvtzu_w32_d(e, A_S2, 0);
            }
            store_gpr(e, insn->rd, A_S2);
            return 0;
        }

        case 0x1A: { /* FCVT.S.W / FCVT.S.WU / FCVT.D.W / FCVT.D.WU */
            a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
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
                emit_ldr_w32_imm(e, A_S2, A_CTX, FP_OFF(insn->rs1));
                store_gpr(e, insn->rd, A_S2);
                return 0;
            }
            return -1;  /* FCLASS — interpreter fallback */

        case 0x1E: /* FMV.W.X (fmt=0, funct3=0) — bitcast int rs1 → f[rd] (NaN-boxed) */
            if (fmt == 0 && insn->funct3 == 0) {
                a64_reg_t rs1 = load_gpr(e, A_S0, insn->rs1);
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

/* ---- Block translator ----
 *
 * Walks the guest stream until it hits a control-flow instruction (JAL,
 * JALR, BRANCH, SYSTEM) or an opcode translate_one can't handle yet, and
 * emits the appropriate exit at every block boundary. The exit always
 * RETs back into the trampoline.
 */
uint8_t *dbt_translate_block(dbt_state_t *dbt, uint32_t guest_pc) {
    /* Conservative "block fits in remaining buffer" check. A worst-case
     * block is ~64 instructions × ~12 host instructions = ~3 KB. */
    if (dbt->code_used + 8192 > CODE_BUF_SIZE) {
        fprintf(stderr, "rv32-run: JIT code buffer exhausted\n");
        exit(1);
    }
    uint8_t *block_start = dbt->code_buf + dbt->code_used;
    emit_t e = { .buf = block_start, .offset = 0,
                 .capacity = CODE_BUF_SIZE - dbt->code_used };

    uint32_t pc = guest_pc;
    int insns = 0;

    for (; insns < MAX_BLOCK_INSNS; insns++) {
        if (pc + 4 > dbt->bin->code_end) {
            /* Out of code segment — exit with EBREAK marker so the
             * dispatcher prints a clean error. */
            exit_with_pc(&e, pc | 2u);
            goto done;
        }

        uint32_t word;
        memcpy(&word, dbt->bin->memory + pc, 4);
        rv32_insn_t insn;
        rv32_decode(word, &insn);

        switch (insn.opcode) {
        case OP_JAL: {
            if (insn.rd != 0) {
                emit_mov_w32_imm32(&e, A_S2, pc + 4);
                store_gpr(&e, insn.rd, A_S2);
            }
            exit_with_pc(&e, (uint32_t)(pc + insn.imm));
            goto done;
        }

        case OP_JALR: {
            /* target = (rs1 + imm) & ~1; link rd before exit (handles rd==rs1). */
            a64_reg_t rs1 = load_gpr(&e, A_S0, insn.rs1);
            add_w_imm32(&e, A_S3, rs1, insn.imm);
            /* Mask LSB. 0xFFFFFFFE is encodable as a logical immediate. */
            if (!emit_and_w32_imm(&e, A_S3, A_S3, 0xFFFFFFFEu)) {
                /* Should not happen; defensive fallback. */
                emit_mov_w32_imm32(&e, A_S1, 0xFFFFFFFEu);
                emit_and_w32(&e, A_S3, A_S3, A_S1);
            }
            if (insn.rd != 0) {
                emit_mov_w32_imm32(&e, A_S2, pc + 4);
                store_gpr(&e, insn.rd, A_S2);
            }
            exit_with_w(&e, A_S3);
            goto done;
        }

        case OP_BRANCH: {
            a64_reg_t rs1 = load_gpr(&e, A_S0, insn.rs1);
            a64_reg_t rs2 = load_gpr(&e, A_S1, insn.rs2);
            emit_cmp_w32_w32(&e, rs1, rs2);
            a64_cond_t cond;
            switch (insn.funct3) {
            case BR_BEQ:  cond = A64_COND_EQ; break;
            case BR_BNE:  cond = A64_COND_NE; break;
            case BR_BLT:  cond = A64_COND_LT; break;
            case BR_BGE:  cond = A64_COND_GE; break;
            case BR_BLTU: cond = A64_COND_LO; break;
            case BR_BGEU: cond = A64_COND_HS; break;
            default: cond = A64_COND_AL;
            }
            uint32_t bcond_off = emit_pos(&e);
            emit_b_cond(&e, cond, 0);                /* patched */
            /* Fall-through (not taken): exit with pc + 4 */
            exit_with_pc(&e, pc + 4);
            uint32_t taken_off = emit_pos(&e);
            emit_patch_cond19(&e, bcond_off, taken_off);
            /* Taken: exit with pc + imm */
            exit_with_pc(&e, (uint32_t)(pc + insn.imm));
            goto done;
        }

        case OP_SYSTEM: {
            /* funct3==0: ECALL (imm==0) or EBREAK (imm==1). The dispatcher
             * recognizes the encoded next_pc tags `(pc+4)|1` for ECALL and
             * `(pc+4)|2` for EBREAK. funct3!=0 is a CSR op — for the
             * microcontroller profile we accept them as no-ops and
             * advance, returning 0 to rd. */
            if (insn.funct3 == 0) {
                uint32_t advanced = pc + 4;
                if (insn.imm == 1)
                    exit_with_pc(&e, advanced | 2u);
                else
                    exit_with_pc(&e, advanced | 1u);
                goto done;
            }
            /* CSR — fake a 0 read, ignore writes. */
            if (insn.rd != 0) {
                emit_mov_w32_imm32(&e, A_S2, 0);
                store_gpr(&e, insn.rd, A_S2);
            }
            pc += 4;
            continue;
        }

        default: {
            int rc = translate_one(&e, &insn, pc);
            if (rc != 0) {
                /* Unhandled (FP for now). Bail out: signal EBREAK so
                 * the user sees a clear "JIT can't handle this" message
                 * — until P3 wires up the FP paths. */
                exit_with_pc(&e, pc | 2u);
                goto done;
            }
            pc += 4;
            continue;
        }
        }
    }

    /* Hit MAX_BLOCK_INSNS without seeing a control-flow instruction.
     * Exit at the next pc and let the dispatcher pick up. */
    exit_with_pc(&e, pc);

done:
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
 * Saves callee-saved regs, loads the host register convention, BLRs into
 * the block, and unwinds on return. Each translated block ends with RET,
 * which lands right after the BLR here.
 */
void dbt_emit_trampoline(dbt_state_t *dbt) {
    emit_t e = { .buf = dbt->code_buf, .offset = 0, .capacity = CODE_BUF_SIZE };

    /* Frame: 48 bytes (3 × 16-byte pairs, keeps SP 16-aligned).
     *   [sp+ 0]  x29 (fp), x30 (lr)
     *   [sp+16]  x19, x20
     *   [sp+32]  x21, padding (unused — saved as XZR)
     */
    emit_stp_pre_sp (&e, A64_W29, A64_W30, -48);
    emit_stp_x64_off(&e, A64_W19, A64_W20, A64_SP, 16);
    emit_str_x64_imm(&e, A64_W21, A64_SP, 32);

    /* Load host register convention */
    emit_mov_x64_x64(&e, A64_W19, A64_W0);   /* ctx */
    emit_mov_x64_x64(&e, A64_W20, A64_W1);   /* mem */
    emit_mov_x64_x64(&e, A64_W21, A64_W3);   /* cache */

    /* Call the translated block. Each block ends with RET, returning to
     * the next instruction here. */
    emit_blr(&e, A64_W2);

    /* Unwind */
    emit_ldr_x64_imm(&e, A64_W21, A64_SP, 32);
    emit_ldp_x64_off(&e, A64_W19, A64_W20, A64_SP, 16);
    emit_ldp_post_sp(&e, A64_W29, A64_W30, 48);
    emit_ret(&e);

    dbt->code_used = e.offset;
    __builtin___clear_cache((char *)dbt->code_buf, (char *)dbt->code_buf + e.offset);
}

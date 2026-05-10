/* shadow.c — Lockstep shadow interpreter for the RV32IMFD JIT.
 *
 * Step semantics MUST match interp.c. When you change one, change the
 * other. The dispatch is a deliberate copy so the verifier can't drift.
 *
 * Stores capture into a per-block log. Loads check the log first
 * (store-forwarding) before falling back to the real binary memory,
 * which the JIT has not yet touched at this point in the run loop.
 */
#include "shadow.h"
#include "decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

/* ---- NaN-boxing helpers (mirror interp.c) ---- */

static inline float fp_unbox_s(uint64_t v) {
    if ((v >> 32) != 0xFFFFFFFF) {
        float nan;
        uint32_t cn = 0x7FC00000;
        memcpy(&nan, &cn, 4);
        return nan;
    }
    float f;
    uint32_t lo = (uint32_t)v;
    memcpy(&f, &lo, 4);
    return f;
}

static inline uint64_t fp_box_s(float f) {
    uint32_t bits;
    memcpy(&bits, &f, 4);
    return 0xFFFFFFFF00000000ULL | bits;
}

static inline double fp_unbox_d(uint64_t v) {
    double d;
    memcpy(&d, &v, 8);
    return d;
}

static inline uint64_t fp_box_d(double d) {
    uint64_t bits;
    memcpy(&bits, &d, 8);
    return bits;
}

/* ---- Shadow memory: store-forwarding load + per-byte store log ---- */

static void sh_store_byte(shadow_state_t *s, uint32_t addr, uint8_t val) {
    /* Coalesce: a same-addr write replaces in place. */
    for (int i = s->store_buf_count - 1; i >= 0; i--) {
        if (s->store_buf[i].addr == addr) {
            s->store_buf[i].value = val;
            return;
        }
    }
    if (s->store_buf_count < SHADOW_STORE_BUF_SIZE) {
        s->store_buf[s->store_buf_count].addr = addr;
        s->store_buf[s->store_buf_count].value = val;
        s->store_buf_count++;
    } else {
        s->store_buf_overflow = 1;
    }
}

static uint8_t sh_load_byte(shadow_state_t *s, uint32_t addr) {
    for (int i = s->store_buf_count - 1; i >= 0; i--) {
        if (s->store_buf[i].addr == addr) return s->store_buf[i].value;
    }
    if (addr < s->bin->memory_size) return s->bin->memory[addr];
    /* Out of bounds — interp would exit(1). For shadow, return 0 and let
     * the verifier flag the divergence later if it matters. */
    return 0;
}

static uint16_t sh_load16(shadow_state_t *s, uint32_t addr) {
    return (uint16_t)sh_load_byte(s, addr) |
           ((uint16_t)sh_load_byte(s, addr + 1) << 8);
}

static uint32_t sh_load32(shadow_state_t *s, uint32_t addr) {
    return (uint32_t)sh_load_byte(s, addr) |
           ((uint32_t)sh_load_byte(s, addr + 1) << 8) |
           ((uint32_t)sh_load_byte(s, addr + 2) << 16) |
           ((uint32_t)sh_load_byte(s, addr + 3) << 24);
}

static void sh_store16(shadow_state_t *s, uint32_t addr, uint16_t val) {
    sh_store_byte(s, addr,     (uint8_t)val);
    sh_store_byte(s, addr + 1, (uint8_t)(val >> 8));
}

static void sh_store32(shadow_state_t *s, uint32_t addr, uint32_t val) {
    sh_store_byte(s, addr,     (uint8_t)val);
    sh_store_byte(s, addr + 1, (uint8_t)(val >> 8));
    sh_store_byte(s, addr + 2, (uint8_t)(val >> 16));
    sh_store_byte(s, addr + 3, (uint8_t)(val >> 24));
}

/* ---- Step result codes ---- */

typedef enum {
    STEP_LINEAR = 0,   /* pc advanced linearly to next instruction */
    STEP_BRANCH,       /* control transferred (branch/jal/jalr) */
    STEP_ECALL,        /* hit ECALL — JIT will exit too */
    STEP_EBREAK,       /* hit EBREAK */
    STEP_ABORT,        /* shadow can't model this — bail to skip block */
} step_result_t;

/* ---- Single-instruction step on the shadow state ---- */

static step_result_t shadow_step(shadow_state_t *s) {
    if (s->pc + 4 > s->bin->memory_size) return STEP_ABORT;

    uint32_t word;
    memcpy(&word, s->bin->memory + s->pc, 4);

    rv32_insn_t insn;
    rv32_decode(word, &insn);

    uint32_t next_pc = s->pc + 4;
    s->insns_verified++;

    switch (insn.opcode) {

    case OP_LUI:
        if (insn.rd) s->x[insn.rd] = (uint32_t)insn.imm;
        break;

    case OP_AUIPC:
        if (insn.rd) s->x[insn.rd] = s->pc + (uint32_t)insn.imm;
        break;

    case OP_JAL:
        if (insn.rd) s->x[insn.rd] = next_pc;
        next_pc = s->pc + insn.imm;
        s->x[0] = 0;
        s->pc = next_pc;
        return STEP_BRANCH;

    case OP_JALR: {
        uint32_t target = (s->x[insn.rs1] + insn.imm) & ~1u;
        if (insn.rd) s->x[insn.rd] = next_pc;
        next_pc = target;
        s->x[0] = 0;
        s->pc = next_pc;
        return STEP_BRANCH;
    }

    case OP_BRANCH: {
        uint32_t a = s->x[insn.rs1];
        uint32_t b = s->x[insn.rs2];
        int taken = 0;
        switch (insn.funct3) {
        case BR_BEQ:  taken = (a == b); break;
        case BR_BNE:  taken = (a != b); break;
        case BR_BLT:  taken = ((int32_t)a < (int32_t)b); break;
        case BR_BGE:  taken = ((int32_t)a >= (int32_t)b); break;
        case BR_BLTU: taken = (a < b); break;
        case BR_BGEU: taken = (a >= b); break;
        default: return STEP_ABORT;
        }
        if (taken) next_pc = s->pc + insn.imm;
        s->x[0] = 0;
        s->pc = next_pc;
        return STEP_BRANCH;
    }

    case OP_LOAD: {
        uint32_t addr = s->x[insn.rs1] + insn.imm;
        uint32_t val = 0;
        switch (insn.funct3) {
        case LD_LB:  val = (uint32_t)(int32_t)(int8_t)sh_load_byte(s, addr); break;
        case LD_LH:  val = (uint32_t)(int32_t)(int16_t)sh_load16(s, addr); break;
        case LD_LW:  val = sh_load32(s, addr); break;
        case LD_LBU: val = sh_load_byte(s, addr); break;
        case LD_LHU: val = sh_load16(s, addr); break;
        default: return STEP_ABORT;
        }
        if (insn.rd) s->x[insn.rd] = val;
        break;
    }

    case OP_STORE: {
        uint32_t addr = s->x[insn.rs1] + insn.imm;
        switch (insn.funct3) {
        case ST_SB: sh_store_byte(s, addr, (uint8_t)s->x[insn.rs2]); break;
        case ST_SH: sh_store16(s, addr, (uint16_t)s->x[insn.rs2]); break;
        case ST_SW: sh_store32(s, addr, s->x[insn.rs2]); break;
        default: return STEP_ABORT;
        }
        break;
    }

    case OP_IMM: {
        uint32_t src = s->x[insn.rs1];
        uint32_t result = 0;
        switch (insn.funct3) {
        case ALU_ADDI:  result = src + (uint32_t)insn.imm; break;
        case ALU_SLTI:  result = ((int32_t)src < insn.imm) ? 1 : 0; break;
        case ALU_SLTIU: result = (src < (uint32_t)insn.imm) ? 1 : 0; break;
        case ALU_XORI:  result = src ^ (uint32_t)insn.imm; break;
        case ALU_ORI:   result = src | (uint32_t)insn.imm; break;
        case ALU_ANDI:  result = src & (uint32_t)insn.imm; break;
        case ALU_SLLI:  result = src << (insn.imm & 0x1F); break;
        case ALU_SRLI:
            if (insn.funct7 & 0x20)
                result = (uint32_t)((int32_t)src >> (insn.imm & 0x1F));
            else
                result = src >> (insn.imm & 0x1F);
            break;
        }
        if (insn.rd) s->x[insn.rd] = result;
        break;
    }

    case OP_REG: {
        uint32_t a = s->x[insn.rs1];
        uint32_t b = s->x[insn.rs2];
        uint32_t result = 0;

        if (insn.funct7 == 0x01) {
            switch (insn.funct3) {
            case 0: result = a * b; break;
            case 1: result = (uint32_t)((int64_t)(int32_t)a * (int64_t)(int32_t)b >> 32); break;
            case 2: result = (uint32_t)((int64_t)(int32_t)a * (uint64_t)b >> 32); break;
            case 3: result = (uint32_t)((uint64_t)a * (uint64_t)b >> 32); break;
            case 4:
                if (b == 0) result = (uint32_t)-1;
                else if ((int32_t)a == INT32_MIN && (int32_t)b == -1) result = (uint32_t)INT32_MIN;
                else result = (uint32_t)((int32_t)a / (int32_t)b);
                break;
            case 5: result = b == 0 ? UINT32_MAX : a / b; break;
            case 6:
                if (b == 0) result = a;
                else if ((int32_t)a == INT32_MIN && (int32_t)b == -1) result = 0;
                else result = (uint32_t)((int32_t)a % (int32_t)b);
                break;
            case 7: result = b == 0 ? a : a % b; break;
            }
        } else {
            switch (insn.funct3) {
            case ALU_ADD:  result = (insn.funct7 & 0x20) ? a - b : a + b; break;
            case ALU_SLL:  result = a << (b & 0x1F); break;
            case ALU_SLT:  result = ((int32_t)a < (int32_t)b) ? 1 : 0; break;
            case ALU_SLTU: result = (a < b) ? 1 : 0; break;
            case ALU_XOR:  result = a ^ b; break;
            case ALU_SRL:
                if (insn.funct7 & 0x20) result = (uint32_t)((int32_t)a >> (b & 0x1F));
                else                     result = a >> (b & 0x1F);
                break;
            case ALU_OR:   result = a | b; break;
            case ALU_AND:  result = a & b; break;
            }
        }
        if (insn.rd) s->x[insn.rd] = result;
        break;
    }

    case OP_FENCE:
        break;

    case OP_SYSTEM:
        if (insn.imm == 0 && insn.funct3 == 0) {
            /* ECALL — JIT exits via next_pc | 1; shadow stops here. */
            s->x[0] = 0;
            s->pc = next_pc;
            s->hit_ecall = 1;
            return STEP_ECALL;
        } else if (insn.imm == 1 && insn.funct3 == 0) {
            s->x[0] = 0;
            s->pc = next_pc;
            s->hit_ebreak = 1;
            return STEP_EBREAK;
        } else if (insn.funct3 >= 1 && insn.funct3 <= 3) {
            uint32_t csr_addr = insn.imm & 0xFFF;
            uint32_t csr_val = 0;
            switch (csr_addr) {
            case 0x001: csr_val = s->fcsr & 0x1F; break;
            case 0x002: csr_val = (s->fcsr >> 5) & 0x7; break;
            case 0x003: csr_val = s->fcsr & 0xFF; break;
            default: return STEP_ABORT;
            }
            uint32_t new_val = csr_val;
            uint32_t rs1_val = s->x[insn.rs1];
            switch (insn.funct3) {
            case 1: new_val = rs1_val; break;
            case 2: new_val = csr_val | rs1_val; break;
            case 3: new_val = csr_val & ~rs1_val; break;
            }
            if (insn.rd) s->x[insn.rd] = csr_val;
            switch (csr_addr) {
            case 0x001: s->fcsr = (s->fcsr & ~0x1Fu) | (new_val & 0x1F); break;
            case 0x002: s->fcsr = (s->fcsr & ~0xE0u) | ((new_val & 0x7) << 5); break;
            case 0x003: s->fcsr = new_val & 0xFF; break;
            }
        } else if (insn.funct3 >= 5 && insn.funct3 <= 7) {
            uint32_t csr_addr = insn.imm & 0xFFF;
            uint32_t csr_val = 0;
            switch (csr_addr) {
            case 0x001: csr_val = s->fcsr & 0x1F; break;
            case 0x002: csr_val = (s->fcsr >> 5) & 0x7; break;
            case 0x003: csr_val = s->fcsr & 0xFF; break;
            default: return STEP_ABORT;
            }
            uint32_t new_val = csr_val;
            uint32_t zimm = insn.rs1;
            switch (insn.funct3) {
            case 5: new_val = zimm; break;
            case 6: new_val = csr_val | zimm; break;
            case 7: new_val = csr_val & ~zimm; break;
            }
            if (insn.rd) s->x[insn.rd] = csr_val;
            switch (csr_addr) {
            case 0x001: s->fcsr = (s->fcsr & ~0x1Fu) | (new_val & 0x1F); break;
            case 0x002: s->fcsr = (s->fcsr & ~0xE0u) | ((new_val & 0x7) << 5); break;
            case 0x003: s->fcsr = new_val & 0xFF; break;
            }
        } else {
            return STEP_ABORT;
        }
        break;

    case OP_FP_LOAD: {
        uint32_t addr = s->x[insn.rs1] + insn.imm;
        if (insn.funct3 == 2) {
            s->f[insn.rd] = 0xFFFFFFFF00000000ULL | sh_load32(s, addr);
        } else if (insn.funct3 == 3) {
            uint32_t lo = sh_load32(s, addr);
            uint32_t hi = sh_load32(s, addr + 4);
            s->f[insn.rd] = ((uint64_t)hi << 32) | lo;
        } else {
            return STEP_ABORT;
        }
        break;
    }

    case OP_FP_STORE: {
        uint32_t addr = s->x[insn.rs1] + insn.imm;
        if (insn.funct3 == 2) {
            sh_store32(s, addr, (uint32_t)s->f[insn.rs2]);
        } else if (insn.funct3 == 3) {
            uint64_t val = s->f[insn.rs2];
            sh_store32(s, addr,     (uint32_t)val);
            sh_store32(s, addr + 4, (uint32_t)(val >> 32));
        } else {
            return STEP_ABORT;
        }
        break;
    }

    case OP_FMADD: case OP_FMSUB: case OP_FNMSUB: case OP_FNMADD: {
        int fmt = insn.funct7 & 3;
        if (fmt == 0) {
            float a = fp_unbox_s(s->f[insn.rs1]);
            float b = fp_unbox_s(s->f[insn.rs2]);
            float c = fp_unbox_s(s->f[insn.rs3]);
            float r;
            switch (insn.opcode) {
            case OP_FMADD:  r =  a * b + c; break;
            case OP_FMSUB:  r =  a * b - c; break;
            case OP_FNMSUB: r = -a * b + c; break;
            case OP_FNMADD: r = -a * b - c; break;
            default: r = 0; break;
            }
            s->f[insn.rd] = fp_box_s(r);
        } else if (fmt == 1) {
            double a = fp_unbox_d(s->f[insn.rs1]);
            double b = fp_unbox_d(s->f[insn.rs2]);
            double c = fp_unbox_d(s->f[insn.rs3]);
            double r;
            switch (insn.opcode) {
            case OP_FMADD:  r =  a * b + c; break;
            case OP_FMSUB:  r =  a * b - c; break;
            case OP_FNMSUB: r = -a * b + c; break;
            case OP_FNMADD: r = -a * b - c; break;
            default: r = 0; break;
            }
            s->f[insn.rd] = fp_box_d(r);
        } else {
            return STEP_ABORT;
        }
        break;
    }

    case OP_FP: {
        int funct5 = insn.funct7 >> 2;
        int fmt = insn.funct7 & 3;

        switch (funct5) {
        case 0x00:
            if (fmt == 0) s->f[insn.rd] = fp_box_s(fp_unbox_s(s->f[insn.rs1]) + fp_unbox_s(s->f[insn.rs2]));
            else          s->f[insn.rd] = fp_box_d(fp_unbox_d(s->f[insn.rs1]) + fp_unbox_d(s->f[insn.rs2]));
            break;
        case 0x01:
            if (fmt == 0) s->f[insn.rd] = fp_box_s(fp_unbox_s(s->f[insn.rs1]) - fp_unbox_s(s->f[insn.rs2]));
            else          s->f[insn.rd] = fp_box_d(fp_unbox_d(s->f[insn.rs1]) - fp_unbox_d(s->f[insn.rs2]));
            break;
        case 0x02:
            if (fmt == 0) s->f[insn.rd] = fp_box_s(fp_unbox_s(s->f[insn.rs1]) * fp_unbox_s(s->f[insn.rs2]));
            else          s->f[insn.rd] = fp_box_d(fp_unbox_d(s->f[insn.rs1]) * fp_unbox_d(s->f[insn.rs2]));
            break;
        case 0x03:
            if (fmt == 0) s->f[insn.rd] = fp_box_s(fp_unbox_s(s->f[insn.rs1]) / fp_unbox_s(s->f[insn.rs2]));
            else          s->f[insn.rd] = fp_box_d(fp_unbox_d(s->f[insn.rs1]) / fp_unbox_d(s->f[insn.rs2]));
            break;
        case 0x0B:
            if (fmt == 0) s->f[insn.rd] = fp_box_s(sqrtf(fp_unbox_s(s->f[insn.rs1])));
            else          s->f[insn.rd] = fp_box_d(sqrt (fp_unbox_d(s->f[insn.rs1])));
            break;
        case 0x04: { /* FSGNJ family */
            if (fmt == 0) {
                uint32_t a, b;
                memcpy(&a, &s->f[insn.rs1], 4);
                memcpy(&b, &s->f[insn.rs2], 4);
                uint32_t r;
                switch (insn.funct3) {
                case 0: r = (a & 0x7FFFFFFF) | (b & 0x80000000); break;
                case 1: r = (a & 0x7FFFFFFF) | ((~b) & 0x80000000); break;
                case 2: r = (a & 0x7FFFFFFF) | ((a ^ b) & 0x80000000); break;
                default: r = a; break;
                }
                s->f[insn.rd] = 0xFFFFFFFF00000000ULL | r;
            } else {
                uint64_t a = s->f[insn.rs1];
                uint64_t b = s->f[insn.rs2];
                uint64_t r;
                switch (insn.funct3) {
                case 0: r = (a & 0x7FFFFFFFFFFFFFFFULL) | (b & 0x8000000000000000ULL); break;
                case 1: r = (a & 0x7FFFFFFFFFFFFFFFULL) | ((~b) & 0x8000000000000000ULL); break;
                case 2: r = (a & 0x7FFFFFFFFFFFFFFFULL) | ((a ^ b) & 0x8000000000000000ULL); break;
                default: r = a; break;
                }
                s->f[insn.rd] = r;
            }
            break;
        }
        case 0x05: { /* FMIN / FMAX */
            if (fmt == 0) {
                float a = fp_unbox_s(s->f[insn.rs1]);
                float b = fp_unbox_s(s->f[insn.rs2]);
                float r;
                if (insn.funct3 == 0) {
                    if (isnan(a) && isnan(b)) { uint32_t cn = 0x7FC00000; memcpy(&r, &cn, 4); }
                    else if (isnan(a)) r = b;
                    else if (isnan(b)) r = a;
                    else if (a == 0 && b == 0) {
                        uint32_t sa, sb;
                        memcpy(&sa, &a, 4); memcpy(&sb, &b, 4);
                        r = (sa & 0x80000000) ? a : b;
                    }
                    else r = (a < b) ? a : b;
                } else {
                    if (isnan(a) && isnan(b)) { uint32_t cn = 0x7FC00000; memcpy(&r, &cn, 4); }
                    else if (isnan(a)) r = b;
                    else if (isnan(b)) r = a;
                    else if (a == 0 && b == 0) {
                        uint32_t sa, sb;
                        memcpy(&sa, &a, 4); memcpy(&sb, &b, 4);
                        r = (sa & 0x80000000) ? b : a;
                    }
                    else r = (a > b) ? a : b;
                }
                s->f[insn.rd] = fp_box_s(r);
            } else {
                double a = fp_unbox_d(s->f[insn.rs1]);
                double b = fp_unbox_d(s->f[insn.rs2]);
                double r;
                if (insn.funct3 == 0) {
                    if (isnan(a) && isnan(b)) { uint64_t cn = 0x7FF8000000000000ULL; memcpy(&r, &cn, 8); }
                    else if (isnan(a)) r = b;
                    else if (isnan(b)) r = a;
                    else if (a == 0 && b == 0) {
                        uint64_t sa, sb;
                        memcpy(&sa, &a, 8); memcpy(&sb, &b, 8);
                        r = (sa & 0x8000000000000000ULL) ? a : b;
                    }
                    else r = (a < b) ? a : b;
                } else {
                    if (isnan(a) && isnan(b)) { uint64_t cn = 0x7FF8000000000000ULL; memcpy(&r, &cn, 8); }
                    else if (isnan(a)) r = b;
                    else if (isnan(b)) r = a;
                    else if (a == 0 && b == 0) {
                        uint64_t sa, sb;
                        memcpy(&sa, &a, 8); memcpy(&sb, &b, 8);
                        r = (sa & 0x8000000000000000ULL) ? b : a;
                    }
                    else r = (a > b) ? a : b;
                }
                s->f[insn.rd] = fp_box_d(r);
            }
            break;
        }
        case 0x14: { /* FEQ / FLT / FLE */
            if (fmt == 0) {
                float a = fp_unbox_s(s->f[insn.rs1]);
                float b = fp_unbox_s(s->f[insn.rs2]);
                uint32_t r = 0;
                switch (insn.funct3) {
                case 2: r = (!isnan(a) && !isnan(b) && a == b) ? 1 : 0; break;
                case 1: r = (!isnan(a) && !isnan(b) && a < b)  ? 1 : 0; break;
                case 0: r = (!isnan(a) && !isnan(b) && a <= b) ? 1 : 0; break;
                }
                if (insn.rd) s->x[insn.rd] = r;
            } else {
                double a = fp_unbox_d(s->f[insn.rs1]);
                double b = fp_unbox_d(s->f[insn.rs2]);
                uint32_t r = 0;
                switch (insn.funct3) {
                case 2: r = (!isnan(a) && !isnan(b) && a == b) ? 1 : 0; break;
                case 1: r = (!isnan(a) && !isnan(b) && a < b)  ? 1 : 0; break;
                case 0: r = (!isnan(a) && !isnan(b) && a <= b) ? 1 : 0; break;
                }
                if (insn.rd) s->x[insn.rd] = r;
            }
            break;
        }
        case 0x18: { /* FCVT.W{,U}.{S,D} */
            int32_t r;
            if (fmt == 0) {
                float a = fp_unbox_s(s->f[insn.rs1]);
                if (insn.rs2 == 0) {
                    if (isnan(a)) r = INT32_MAX;
                    else if (a >= 2147483648.0f) r = INT32_MAX;
                    else if (a < -2147483648.0f) r = INT32_MIN;
                    else r = (int32_t)a;
                } else {
                    if (isnan(a) || a < 0.0f) r = 0;
                    else if (a >= 4294967296.0f) r = (int32_t)UINT32_MAX;
                    else r = (int32_t)(uint32_t)a;
                }
            } else {
                double a = fp_unbox_d(s->f[insn.rs1]);
                if (insn.rs2 == 0) {
                    if (isnan(a)) r = INT32_MAX;
                    else if (a >= 2147483648.0) r = INT32_MAX;
                    else if (a < -2147483648.0) r = INT32_MIN;
                    else r = (int32_t)a;
                } else {
                    if (isnan(a) || a < 0.0) r = 0;
                    else if (a >= 4294967296.0) r = (int32_t)UINT32_MAX;
                    else r = (int32_t)(uint32_t)a;
                }
            }
            if (insn.rd) s->x[insn.rd] = (uint32_t)r;
            break;
        }
        case 0x1A: /* FCVT.{S,D}.W{,U} */
            if (fmt == 0) {
                if (insn.rs2 == 0) s->f[insn.rd] = fp_box_s((float)(int32_t)s->x[insn.rs1]);
                else                s->f[insn.rd] = fp_box_s((float)s->x[insn.rs1]);
            } else {
                if (insn.rs2 == 0) s->f[insn.rd] = fp_box_d((double)(int32_t)s->x[insn.rs1]);
                else                s->f[insn.rd] = fp_box_d((double)s->x[insn.rs1]);
            }
            break;
        case 0x08: /* FCVT.S.D / FCVT.D.S */
            if (fmt == 0) s->f[insn.rd] = fp_box_s((float)fp_unbox_d(s->f[insn.rs1]));
            else          s->f[insn.rd] = fp_box_d((double)fp_unbox_s(s->f[insn.rs1]));
            break;
        case 0x1C: /* FMV.X.W / FCLASS */
            if (fmt == 0) {
                if (insn.funct3 == 0) {
                    if (insn.rd) s->x[insn.rd] = (uint32_t)s->f[insn.rs1];
                } else if (insn.funct3 == 1) {
                    float a = fp_unbox_s(s->f[insn.rs1]);
                    uint32_t bits;
                    memcpy(&bits, &a, 4);
                    uint32_t sign = bits >> 31;
                    uint32_t exp = (bits >> 23) & 0xFF;
                    uint32_t frac = bits & 0x7FFFFF;
                    uint32_t cls = 0;
                    if (exp == 0xFF && frac != 0)        cls = (frac & 0x400000) ? (1 << 9) : (1 << 8);
                    else if (exp == 0xFF)                cls = sign ? (1 << 0) : (1 << 7);
                    else if (exp == 0 && frac == 0)      cls = sign ? (1 << 3) : (1 << 4);
                    else if (exp == 0)                   cls = sign ? (1 << 2) : (1 << 5);
                    else                                 cls = sign ? (1 << 1) : (1 << 6);
                    if (insn.rd) s->x[insn.rd] = cls;
                }
            } else if (fmt == 1 && insn.funct3 == 1) {
                double a = fp_unbox_d(s->f[insn.rs1]);
                uint64_t bits;
                memcpy(&bits, &a, 8);
                uint32_t sign = (uint32_t)(bits >> 63);
                uint32_t exp = (uint32_t)((bits >> 52) & 0x7FF);
                uint64_t frac = bits & 0xFFFFFFFFFFFFFULL;
                uint32_t cls = 0;
                if (exp == 0x7FF && frac != 0)         cls = (frac & 0x8000000000000ULL) ? (1 << 9) : (1 << 8);
                else if (exp == 0x7FF)                 cls = sign ? (1 << 0) : (1 << 7);
                else if (exp == 0 && frac == 0)        cls = sign ? (1 << 3) : (1 << 4);
                else if (exp == 0)                     cls = sign ? (1 << 2) : (1 << 5);
                else                                   cls = sign ? (1 << 1) : (1 << 6);
                if (insn.rd) s->x[insn.rd] = cls;
            }
            break;
        case 0x1E:
            if (fmt == 0 && insn.funct3 == 0)
                s->f[insn.rd] = 0xFFFFFFFF00000000ULL | s->x[insn.rs1];
            break;
        default:
            return STEP_ABORT;
        }
        break;
    }

    default:
        return STEP_ABORT;
    }

    s->x[0] = 0;
    s->pc = next_pc;
    return STEP_LINEAR;
}

/* ---- Public API ---- */

void shadow_init(shadow_state_t *s, dbt_state_t *dbt) {
    memset(s, 0, sizeof(*s));
    s->bin = dbt->bin;

    /* Skip blocks that start at intrinsic addresses — the JIT replaces
     * those with a direct host call to libc memcpy/memset/etc., which we
     * can't model in shadow. */
    int n = 0;
    if (dbt->intrinsic_memcpy)  s->skip_pcs[n++] = dbt->intrinsic_memcpy;
    if (dbt->intrinsic_memmove) s->skip_pcs[n++] = dbt->intrinsic_memmove;
    if (dbt->intrinsic_memset)  s->skip_pcs[n++] = dbt->intrinsic_memset;
    if (dbt->intrinsic_strlen)  s->skip_pcs[n++] = dbt->intrinsic_strlen;
    for (int i = 0; i < LIBM_COUNT && n < 32; i++)
        if (dbt->intrinsic_libm[i]) s->skip_pcs[n++] = dbt->intrinsic_libm[i];
    s->skip_pc_count = n;
}

void shadow_snapshot(shadow_state_t *s, dbt_state_t *dbt) {
    memcpy(s->snap_x, dbt->ctx.x, sizeof(s->snap_x));
    s->snap_pc = dbt->ctx.next_pc;
    memcpy(s->snap_f, dbt->ctx.f, sizeof(s->snap_f));
    s->snap_fcsr = dbt->ctx.fcsr;
}

static int is_skip_pc(shadow_state_t *s, uint32_t pc) {
    for (int i = 0; i < s->skip_pc_count; i++)
        if (s->skip_pcs[i] == pc) return 1;
    return 0;
}

int shadow_pre_execute(shadow_state_t *s, uint32_t guest_pc) {
    if (is_skip_pc(s, guest_pc)) {
        s->blocks_skipped++;
        return 0;
    }

    /* Restore state from snapshot. */
    memcpy(s->x, s->snap_x, sizeof(s->x));
    s->pc = s->snap_pc;
    memcpy(s->f, s->snap_f, sizeof(s->f));
    s->fcsr = s->snap_fcsr;
    s->store_buf_count = 0;
    s->store_buf_overflow = 0;
    s->hit_ecall = 0;
    s->hit_ebreak = 0;

    /* Step until the JIT would also exit. With chaining/superblocks/self-
     * loops disabled under verify mode, that's: first branch/jump/system
     * or MAX_BLOCK_INSNS straight-line instructions. */
    int budget = 256;  /* generous slack over MAX_BLOCK_INSNS=64 */
    while (budget-- > 0) {
        step_result_t r = shadow_step(s);
        if (r == STEP_ABORT) {
            /* Shadow can't model this — skip the block. */
            s->blocks_skipped++;
            return 0;
        }
        if (r == STEP_BRANCH || r == STEP_ECALL || r == STEP_EBREAK) break;
        /* Linear: keep going until budget or branch/ecall. */
    }
    return 1;
}

/* Helper: dump diff between two reg files. */
static void dump_regs_diff(FILE *f, const char *label_a, const uint32_t *a,
                           const char *label_b, const uint32_t *b) {
    fprintf(f, "  %-8s  %-8s  %-12s  %-12s  %s\n",
            "reg", "alias", label_a, label_b, "");
    static const char *abi[32] = {
        "zero","ra","sp","gp","tp","t0","t1","t2","s0","s1",
        "a0","a1","a2","a3","a4","a5","a6","a7",
        "s2","s3","s4","s5","s6","s7","s8","s9","s10","s11",
        "t3","t4","t5","t6"
    };
    for (int i = 0; i < 32; i++) {
        if (a[i] != b[i]) {
            fprintf(f, "  x%-7d %-8s  0x%08X    0x%08X    MISMATCH\n",
                    i, abi[i], a[i], b[i]);
        }
    }
}

void shadow_verify(shadow_state_t *s, dbt_state_t *dbt, uint32_t block_pc) {
    /* Skip if pre_execute decided to bail. We detect that by store_buf
     * being empty AND no progress (hit_ecall/etc all false AND insns_verified
     * didn't increase) — but the cleaner signal is: pre_execute returned 0,
     * which the caller records by skipping this verify call. So if we're
     * called here, pre_execute succeeded. */

    uint32_t shadow_next_pc = s->pc;
    uint32_t jit_next_pc    = dbt->ctx.next_pc & ~3u;  /* strip ecall/ebreak bits */

    int pc_mismatch  = (shadow_next_pc != jit_next_pc);
    int reg_mismatch = (memcmp(s->x, dbt->ctx.x, sizeof(s->x)) != 0);
    int fp_mismatch  = (memcmp(s->f, dbt->ctx.f, sizeof(s->f)) != 0);

    /* Compare shadow stores against real memory (which the JIT just wrote). */
    int mem_mismatches = 0;
    int first_mem_mismatch = -1;
    for (int i = 0; i < s->store_buf_count; i++) {
        uint32_t addr = s->store_buf[i].addr;
        if (addr >= s->bin->memory_size) continue;
        uint8_t real = s->bin->memory[addr];
        if (real != s->store_buf[i].value) {
            if (first_mem_mismatch < 0) first_mem_mismatch = i;
            mem_mismatches++;
        }
    }

    if (!pc_mismatch && !reg_mismatch && !fp_mismatch && !mem_mismatches) {
        s->blocks_verified++;
        return;
    }

    /* Divergence — dump everything and abort. */
    fprintf(stderr,
            "\n=== SHADOW DIVERGENCE at block 0x%08X (verified #%" PRIu64 ") ===\n",
            block_pc, s->blocks_verified);
    fprintf(stderr, "  shadow: %d insns stepped from snapshot, %d stores logged%s\n",
            (int)(s->insns_verified -
                  (s->blocks_verified ? 0 : 0) /* approx */),
            s->store_buf_count, s->store_buf_overflow ? " [OVERFLOW]" : "");
    fprintf(stderr, "  next_pc:  shadow=0x%08X  jit=0x%08X%s\n",
            shadow_next_pc, jit_next_pc, pc_mismatch ? "  <<< MISMATCH" : "");
    if (s->hit_ecall)  fprintf(stderr, "  shadow stopped at ECALL\n");
    if (s->hit_ebreak) fprintf(stderr, "  shadow stopped at EBREAK\n");

    if (reg_mismatch) {
        fprintf(stderr, "\nInteger registers (rows shown only when they differ):\n");
        dump_regs_diff(stderr, "shadow", s->x, "jit", dbt->ctx.x);
    }
    if (fp_mismatch) {
        fprintf(stderr, "\nFP registers (rows shown only when they differ):\n");
        for (int i = 0; i < 32; i++) {
            if (s->f[i] != dbt->ctx.f[i])
                fprintf(stderr, "  f%-2d  shadow=0x%016llX  jit=0x%016llX\n",
                        i, (unsigned long long)s->f[i],
                        (unsigned long long)dbt->ctx.f[i]);
        }
    }
    if (mem_mismatches) {
        fprintf(stderr, "\nMemory mismatches (%d of %d shadow stores diverge):\n",
                mem_mismatches, s->store_buf_count);
        int shown = 0;
        for (int i = first_mem_mismatch; i < s->store_buf_count && shown < 16; i++) {
            uint32_t addr = s->store_buf[i].addr;
            if (addr >= s->bin->memory_size) continue;
            uint8_t real = s->bin->memory[addr];
            if (real != s->store_buf[i].value) {
                fprintf(stderr, "  [0x%08X]  shadow=0x%02X  jit=0x%02X\n",
                        addr, s->store_buf[i].value, real);
                shown++;
            }
        }
        if (mem_mismatches > shown)
            fprintf(stderr, "  ... and %d more\n", mem_mismatches - shown);
    }

    fprintf(stderr, "\nPre-block snapshot:\n");
    for (int i = 0; i < 32; i += 4)
        fprintf(stderr, "  x%-2d=0x%08X  x%-2d=0x%08X  x%-2d=0x%08X  x%-2d=0x%08X\n",
                i,   s->snap_x[i],   i+1, s->snap_x[i+1],
                i+2, s->snap_x[i+2], i+3, s->snap_x[i+3]);
    fprintf(stderr, "  snap_pc=0x%08X\n", s->snap_pc);

    fprintf(stderr, "\nGuest disassembly of the block (starting at snap_pc):\n");
    for (uint32_t pc = s->snap_pc, n = 0;
         pc + 4 <= s->bin->memory_size && n < 64;
         pc += 4, n++) {
        uint32_t w;
        memcpy(&w, s->bin->memory + pc, 4);
        fprintf(stderr, "  0x%08X: %08X\n", pc, w);
        /* Crude: stop at first branch/jump/system */
        rv32_insn_t si;
        rv32_decode(w, &si);
        if (si.opcode == OP_JAL || si.opcode == OP_JALR ||
            si.opcode == OP_BRANCH || si.opcode == OP_SYSTEM) break;
    }

    fprintf(stderr, "\n=== END SHADOW DIVERGENCE ===\n");
    abort();
}

void shadow_print_stats(shadow_state_t *s) {
    fprintf(stderr,
            "shadow: %" PRIu64 " blocks verified, %" PRIu64 " skipped, "
            "%" PRIu64 " insns — 0 divergences\n",
            s->blocks_verified, s->blocks_skipped, s->insns_verified);
}

#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>

/* RV32IM opcodes (bits [6:0]) */
#define OP_LUI       0x37
#define OP_AUIPC     0x17
#define OP_JAL       0x6F
#define OP_JALR      0x67
#define OP_BRANCH    0x63
#define OP_LOAD      0x03
#define OP_STORE     0x23
#define OP_IMM       0x13
#define OP_REG       0x33
#define OP_FENCE     0x0F
#define OP_SYSTEM    0x73

/* Branch funct3 */
#define BR_BEQ   0
#define BR_BNE   1
#define BR_BLT   4
#define BR_BGE   5
#define BR_BLTU  6
#define BR_BGEU  7

/* Load funct3 */
#define LD_LB    0
#define LD_LH    1
#define LD_LW    2
#define LD_LBU   4
#define LD_LHU   5

/* Store funct3 */
#define ST_SB    0
#define ST_SH    1
#define ST_SW    2

/* ALU immediate funct3 */
#define ALU_ADDI  0
#define ALU_SLTI  2
#define ALU_SLTIU 3
#define ALU_XORI  4
#define ALU_ORI   6
#define ALU_ANDI  7
#define ALU_SLLI  1
#define ALU_SRLI  5  /* also SRAI when funct7=0x20 */

/* ALU register funct3 (+ funct7 for disambiguation) */
#define ALU_ADD   0  /* SUB when funct7=0x20, MUL when funct7=0x01 */
#define ALU_SLL   1  /* MULH when funct7=0x01 */
#define ALU_SLT   2  /* MULHSU when funct7=0x01 */
#define ALU_SLTU  3  /* MULHU when funct7=0x01 */
#define ALU_XOR   4  /* DIV when funct7=0x01 */
#define ALU_SRL   5  /* SRA when funct7=0x20, DIVU when funct7=0x01 */
#define ALU_OR    6  /* REM when funct7=0x01 */
#define ALU_AND   7  /* REMU when funct7=0x01 */

/* Decoded instruction */
typedef struct {
    uint8_t  opcode;   /* bits [6:0] */
    uint8_t  rd;       /* destination register */
    uint8_t  rs1;      /* source register 1 */
    uint8_t  rs2;      /* source register 2 */
    uint8_t  funct3;   /* function code (bits [14:12]) */
    uint8_t  funct7;   /* function code (bits [31:25]) */
    int32_t  imm;      /* decoded immediate (sign-extended) */
} rv32_insn_t;

/*
 * Decode a 32-bit RV32IM instruction.
 * Returns the instruction length (always 4 for RV32IM).
 */
static inline int rv32_decode(uint32_t word, rv32_insn_t *insn) {
    insn->opcode = word & 0x7F;
    insn->rd     = (word >> 7) & 0x1F;
    insn->funct3 = (word >> 12) & 0x07;
    insn->rs1    = (word >> 15) & 0x1F;
    insn->rs2    = (word >> 20) & 0x1F;
    insn->funct7 = (word >> 25) & 0x7F;

    /* Decode immediate based on instruction format */
    switch (insn->opcode) {
    case OP_LUI:
    case OP_AUIPC:
        /* U-type: imm[31:12] */
        insn->imm = (int32_t)(word & 0xFFFFF000);
        break;

    case OP_JAL:
        /* J-type: imm[20|10:1|11|19:12] */
        insn->imm = (int32_t)(
            ((word >> 31) ? 0xFFF00000 : 0) | /* sign bit 20 */
            (word & 0x000FF000) |               /* bits 19:12 */
            ((word >> 9) & 0x00000800) |         /* bit 11 */
            ((word >> 20) & 0x000007FE)          /* bits 10:1 */
        );
        break;

    case OP_BRANCH:
        /* B-type: imm[12|10:5|4:1|11] */
        insn->imm = (int32_t)(
            ((word >> 31) ? 0xFFFFF000 : 0) | /* sign bit 12 */
            ((word << 4) & 0x00000800) |        /* bit 11 */
            ((word >> 20) & 0x000007E0) |        /* bits 10:5 */
            ((word >> 7) & 0x0000001E)           /* bits 4:1 */
        );
        break;

    case OP_STORE:
        /* S-type: imm[11:5|4:0] */
        insn->imm = (int32_t)(
            ((word >> 31) ? 0xFFFFF000 : 0) |
            ((word >> 20) & 0xFE0) |
            ((word >> 7) & 0x1F)
        );
        break;

    default:
        /* I-type: imm[11:0] (covers LOAD, IMM, JALR, SYSTEM) */
        insn->imm = ((int32_t)word) >> 20;
        break;
    }

    return 4;
}

#endif /* DECODER_H */

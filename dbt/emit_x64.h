#ifndef EMIT_X64_H
#define EMIT_X64_H

#include <stdint.h>
#include <string.h>

/*
 * Lightweight x86-64 code emitter for JIT compilation.
 *
 * Convention:
 *   RBX = pointer to rv32_ctx_t (guest register file + state)
 *   R12 = pointer to guest memory base
 *   RAX, RCX, RDX, RSI, RDI, R8-R11 = scratch
 *
 * Guest register access (no caching, load/store style):
 *   load:  mov eax, [rbx + reg*4]
 *   store: mov [rbx + reg*4], eax
 */

/* Code buffer */
typedef struct {
    uint8_t *buf;
    uint32_t offset;
    uint32_t capacity;
} emit_t;

static inline void emit_byte(emit_t *e, uint8_t b) {
    if (e->offset < e->capacity)
        e->buf[e->offset] = b;
    e->offset++;
}

static inline void emit_bytes(emit_t *e, const void *data, int len) {
    if (e->offset + len <= e->capacity)
        memcpy(e->buf + e->offset, data, len);
    e->offset += len;
}

static inline void emit_u32(emit_t *e, uint32_t val) {
    emit_bytes(e, &val, 4);
}

static inline void emit_u64(emit_t *e, uint64_t val) {
    emit_bytes(e, &val, 8);
}

static inline uint32_t emit_pos(emit_t *e) {
    return e->offset;
}

/* Patch a 32-bit relative displacement at a previous offset */
static inline void emit_patch_rel32(emit_t *e, uint32_t patch_offset, uint32_t target_offset) {
    int32_t disp = (int32_t)(target_offset - (patch_offset + 4));
    memcpy(e->buf + patch_offset, &disp, 4);
}

/* x86-64 register encoding */
#define X64_RAX 0
#define X64_RCX 1
#define X64_RDX 2
#define X64_RBX 3
#define X64_RSP 4
#define X64_RBP 5
#define X64_RSI 6
#define X64_RDI 7
#define X64_R8  8
#define X64_R9  9
#define X64_R10 10
#define X64_R11 11
#define X64_R12 12
#define X64_R13 13
#define X64_R14 14
#define X64_R15 15

/* REX prefix helpers */
static inline uint8_t rex(int w, int r, int x, int b) {
    return 0x40 | (w << 3) | (r << 2) | (x << 1) | b;
}

static inline int reg_hi(int r) { return (r >> 3) & 1; }
static inline int reg_lo(int r) { return r & 7; }

/* ModR/M byte */
static inline uint8_t modrm(int mod, int reg, int rm) {
    return (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* ---- Common instruction patterns ---- */

/* mov r32, [rbx + offset] — load guest register from context */
static inline void emit_load_guest(emit_t *e, int host_reg, int guest_reg) {
    int offset = guest_reg * 4;
    if (reg_hi(host_reg))
        emit_byte(e, rex(0, reg_hi(host_reg), 0, 0));
    emit_byte(e, 0x8B);  /* MOV r32, r/m32 */
    if (offset == 0) {
        emit_byte(e, modrm(0x00, host_reg, X64_RBX));
    } else if (offset <= 127) {
        emit_byte(e, modrm(0x01, host_reg, X64_RBX));
        emit_byte(e, (uint8_t)offset);
    } else {
        emit_byte(e, modrm(0x02, host_reg, X64_RBX));
        emit_u32(e, (uint32_t)offset);
    }
}

/* mov [rbx + offset], r32 — store to guest register in context */
static inline void emit_store_guest(emit_t *e, int guest_reg, int host_reg) {
    if (guest_reg == 0) return;  /* x0 is hardwired zero, never write */
    int offset = guest_reg * 4;
    if (reg_hi(host_reg))
        emit_byte(e, rex(0, reg_hi(host_reg), 0, 0));
    emit_byte(e, 0x89);  /* MOV r/m32, r32 */
    if (offset <= 127) {
        emit_byte(e, modrm(0x01, host_reg, X64_RBX));
        emit_byte(e, (uint8_t)offset);
    } else {
        emit_byte(e, modrm(0x02, host_reg, X64_RBX));
        emit_u32(e, (uint32_t)offset);
    }
}

/* mov r32, imm32 */
static inline void emit_mov_r32_imm32(emit_t *e, int reg, uint32_t imm) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xB8 + reg_lo(reg));
    emit_u32(e, imm);
}

/* xor r32, r32 (zeroing) */
static inline void emit_xor_rr(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x31);
    emit_byte(e, modrm(0x03, dst, src));
}

/* mov r32, r32 */
static inline void emit_mov_rr(emit_t *e, int dst, int src) {
    if (dst == src) return;
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x89);
    emit_byte(e, modrm(0x03, src, dst));
}

/* add r32, r32 */
static inline void emit_add_rr(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x01);
    emit_byte(e, modrm(0x03, src, dst));
}

/* sub r32, r32 */
static inline void emit_sub_rr(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x29);
    emit_byte(e, modrm(0x03, src, dst));
}

/* and r32, r32 */
static inline void emit_and_rr(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x21);
    emit_byte(e, modrm(0x03, src, dst));
}

/* or r32, r32 */
static inline void emit_or_rr(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x09);
    emit_byte(e, modrm(0x03, src, dst));
}

/* xor r32, r32 (non-zeroing — different regs) */
static inline void emit_xor_rr_op(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x31);
    emit_byte(e, modrm(0x03, src, dst));
}

/* add r32, imm32 (or imm8) */
static inline void emit_add_r_imm(emit_t *e, int reg, int32_t imm) {
    if (imm == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 0, reg));
        emit_byte(e, (uint8_t)(int8_t)imm);
    } else if (reg == X64_RAX) {
        emit_byte(e, 0x05);
        emit_u32(e, (uint32_t)imm);
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 0, reg));
        emit_u32(e, (uint32_t)imm);
    }
}

/* and r32, imm32 */
static inline void emit_and_r_imm(emit_t *e, int reg, int32_t imm) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 4, reg));
        emit_byte(e, (uint8_t)(int8_t)imm);
    } else if (reg == X64_RAX) {
        emit_byte(e, 0x25);
        emit_u32(e, (uint32_t)imm);
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 4, reg));
        emit_u32(e, (uint32_t)imm);
    }
}

/* or r32, imm32 */
static inline void emit_or_r_imm(emit_t *e, int reg, int32_t imm) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 1, reg));
        emit_byte(e, (uint8_t)(int8_t)imm);
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 1, reg));
        emit_u32(e, (uint32_t)imm);
    }
}

/* xor r32, imm32 */
static inline void emit_xor_r_imm(emit_t *e, int reg, int32_t imm) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 6, reg));
        emit_byte(e, (uint8_t)(int8_t)imm);
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 6, reg));
        emit_u32(e, (uint32_t)imm);
    }
}

/* shl r32, imm8 */
static inline void emit_shl_r_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 4, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 4, reg));
        emit_byte(e, amt);
    }
}

/* shr r32, imm8 */
static inline void emit_shr_r_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 5, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 5, reg));
        emit_byte(e, amt);
    }
}

/* sar r32, imm8 */
static inline void emit_sar_r_imm(emit_t *e, int reg, uint8_t amt) {
    if (amt == 0) return;
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (amt == 1) {
        emit_byte(e, 0xD1);
        emit_byte(e, modrm(0x03, 7, reg));
    } else {
        emit_byte(e, 0xC1);
        emit_byte(e, modrm(0x03, 7, reg));
        emit_byte(e, amt);
    }
}

/* shl r32, cl */
static inline void emit_shl_r_cl(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 4, reg));
}

/* shr r32, cl */
static inline void emit_shr_r_cl(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 5, reg));
}

/* sar r32, cl */
static inline void emit_sar_r_cl(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xD3);
    emit_byte(e, modrm(0x03, 7, reg));
}

/* imul r32, r32 (signed multiply, low 32) */
static inline void emit_imul_rr(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xAF);
    emit_byte(e, modrm(0x03, dst, src));
}

/* cmp r32, r32 */
static inline void emit_cmp_rr(emit_t *e, int r1, int r2) {
    if (reg_hi(r1) || reg_hi(r2))
        emit_byte(e, rex(0, reg_hi(r2), 0, reg_hi(r1)));
    emit_byte(e, 0x39);
    emit_byte(e, modrm(0x03, r2, r1));
}

/* cmp r32, imm32 */
static inline void emit_cmp_r_imm(emit_t *e, int reg, int32_t imm) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    if (imm >= -128 && imm <= 127) {
        emit_byte(e, 0x83);
        emit_byte(e, modrm(0x03, 7, reg));
        emit_byte(e, (uint8_t)(int8_t)imm);
    } else if (reg == X64_RAX) {
        emit_byte(e, 0x3D);
        emit_u32(e, (uint32_t)imm);
    } else {
        emit_byte(e, 0x81);
        emit_byte(e, modrm(0x03, 7, reg));
        emit_u32(e, (uint32_t)imm);
    }
}

/* test r32, r32 */
static inline void emit_test_rr(emit_t *e, int r1, int r2) {
    if (reg_hi(r1) || reg_hi(r2))
        emit_byte(e, rex(0, reg_hi(r2), 0, reg_hi(r1)));
    emit_byte(e, 0x85);
    emit_byte(e, modrm(0x03, r2, r1));
}

/* setCC r8 (AL typically) — cc is the condition byte (e.g., 0x94 = sete) */
static inline void emit_setcc(emit_t *e, uint8_t cc, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0x0F);
    emit_byte(e, cc);
    emit_byte(e, modrm(0x03, 0, reg));
}

#define SETCC_E   0x94
#define SETCC_NE  0x95
#define SETCC_L   0x9C  /* signed < */
#define SETCC_GE  0x9D  /* signed >= */
#define SETCC_B   0x92  /* unsigned < */
#define SETCC_AE  0x93  /* unsigned >= */

/* movzx r32, r8 — always emit REX so regs 4-7 map to SPL/BPL/SIL/DIL */
static inline void emit_movzx_r32_r8(emit_t *e, int dst, int src) {
    emit_byte(e, rex(0, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB6);
    emit_byte(e, modrm(0x03, dst, src));
}

/* movsx r32, r8 — sign-extend byte; always emit REX for safe byte encoding */
static inline void emit_movsx_r32_r8(emit_t *e, int dst, int src) {
    emit_byte(e, rex(0, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xBE);
    emit_byte(e, modrm(0x03, dst, src));
}

/* movsx r32, r16 — sign-extend word */
static inline void emit_movsx_r32_r16(emit_t *e, int dst, int src) {
    if (reg_hi(dst) || reg_hi(src))
        emit_byte(e, rex(0, reg_hi(dst), 0, reg_hi(src)));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xBF);
    emit_byte(e, modrm(0x03, dst, src));
}

/* neg r32 */
static inline void emit_neg(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 3, reg));
}

/* not r32 */
static inline void emit_not(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 2, reg));
}

/* cdq (sign-extend EAX into EDX:EAX) */
static inline void emit_cdq(emit_t *e) {
    emit_byte(e, 0x99);
}

/* idiv r32 (signed divide EDX:EAX by r32, quotient→EAX, remainder→EDX) */
static inline void emit_idiv(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 7, reg));
}

/* div r32 (unsigned divide) */
static inline void emit_div(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 6, reg));
}

/* mul r32 (unsigned multiply EDX:EAX = EAX * r32) */
static inline void emit_mul(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 4, reg));
}

/* imul r32 (signed multiply EDX:EAX = EAX * r32) — single operand form */
static inline void emit_imul_1(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xF7);
    emit_byte(e, modrm(0x03, 5, reg));
}

/* Shared SIB+displacement encoding for [R12 + index + disp] addressing.
 * mod_reg is the ModRM reg field (dst for loads, src for stores).
 * index_reg must not have reg_lo==4 (RSP), which our cache regs never do. */
static inline void emit_sib_disp(emit_t *e, int mod_reg, int index_reg, int32_t disp) {
    int mod = (disp == 0) ? 0x00 : (disp >= -128 && disp <= 127) ? 0x01 : 0x02;
    emit_byte(e, modrm(mod, mod_reg, 0x04));  /* rm=100 → SIB follows */
    emit_byte(e, (uint8_t)((reg_lo(index_reg) << 3) | reg_lo(X64_R12)));
    if (mod == 0x01) emit_byte(e, (uint8_t)(int8_t)disp);
    else if (mod == 0x02) emit_u32(e, (uint32_t)disp);
}

/* mov r32, [R12 + idx + disp] */
static inline void emit_load_mem32(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x8B);
    emit_sib_disp(e, dst, idx, disp);
}

/* movsx r32, word [R12 + idx + disp] */
static inline void emit_load_mem16s(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xBF);
    emit_sib_disp(e, dst, idx, disp);
}

/* movzx r32, word [R12 + idx + disp] */
static inline void emit_load_mem16u(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB7);
    emit_sib_disp(e, dst, idx, disp);
}

/* movsx r32, byte [R12 + idx + disp] */
static inline void emit_load_mem8s(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xBE);
    emit_sib_disp(e, dst, idx, disp);
}

/* movzx r32, byte [R12 + idx + disp] */
static inline void emit_load_mem8u(emit_t *e, int dst, int idx, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0xB6);
    emit_sib_disp(e, dst, idx, disp);
}

/* SIB+displacement encoding for [R12 + disp] addressing (no index register).
 * SIB byte: index=100 (none), base=100 (R12 lo). REX.B=1 for R12. */
static inline void emit_sib_disp_noindex(emit_t *e, int mod_reg, int32_t disp) {
    int mod = (disp == 0) ? 0x00 : (disp >= -128 && disp <= 127) ? 0x01 : 0x02;
    emit_byte(e, modrm(mod, mod_reg, 0x04));  /* rm=100 → SIB follows */
    emit_byte(e, 0x24);  /* index=100 (none), base=100 (R12 lo) */
    if (mod == 0x01) emit_byte(e, (uint8_t)(int8_t)disp);
    else if (mod == 0x02) emit_u32(e, (uint32_t)disp);
}

/* mov r32, [R12 + disp] (no index) */
static inline void emit_load_abs32(emit_t *e, int dst, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), 0, 1));
    emit_byte(e, 0x8B);
    emit_sib_disp_noindex(e, dst, disp);
}

/* movsx r32, word [R12 + disp] (no index) */
static inline void emit_load_abs16s(emit_t *e, int dst, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0xBF);
    emit_sib_disp_noindex(e, dst, disp);
}

/* movzx r32, word [R12 + disp] (no index) */
static inline void emit_load_abs16u(emit_t *e, int dst, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0xB7);
    emit_sib_disp_noindex(e, dst, disp);
}

/* movsx r32, byte [R12 + disp] (no index) */
static inline void emit_load_abs8s(emit_t *e, int dst, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0xBE);
    emit_sib_disp_noindex(e, dst, disp);
}

/* movzx r32, byte [R12 + disp] (no index) */
static inline void emit_load_abs8u(emit_t *e, int dst, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(dst), 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0xB6);
    emit_sib_disp_noindex(e, dst, disp);
}

/* mov [R12 + disp], r32 (no index) */
static inline void emit_store_abs32(emit_t *e, int src, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(src), 0, 1));
    emit_byte(e, 0x89);
    emit_sib_disp_noindex(e, src, disp);
}

/* mov word [R12 + disp], r16 (no index) */
static inline void emit_store_abs16(emit_t *e, int src, int32_t disp) {
    emit_byte(e, 0x66);
    emit_byte(e, rex(0, reg_hi(src), 0, 1));
    emit_byte(e, 0x89);
    emit_sib_disp_noindex(e, src, disp);
}

/* mov byte [R12 + disp], r8 (no index) */
static inline void emit_store_abs8(emit_t *e, int src, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(src), 0, 1));
    emit_byte(e, 0x88);
    emit_sib_disp_noindex(e, src, disp);
}

/* mov [R12 + idx + disp], r32 */
static inline void emit_store_mem32(emit_t *e, int idx, int src, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(src), reg_hi(idx), 1));
    emit_byte(e, 0x89);
    emit_sib_disp(e, src, idx, disp);
}

/* mov word [R12 + idx + disp], r16 */
static inline void emit_store_mem16(emit_t *e, int idx, int src, int32_t disp) {
    emit_byte(e, 0x66);
    emit_byte(e, rex(0, reg_hi(src), reg_hi(idx), 1));
    emit_byte(e, 0x89);
    emit_sib_disp(e, src, idx, disp);
}

/* mov byte [R12 + idx + disp], r8 */
static inline void emit_store_mem8(emit_t *e, int idx, int src, int32_t disp) {
    emit_byte(e, rex(0, reg_hi(src), reg_hi(idx), 1));
    emit_byte(e, 0x88);
    emit_sib_disp(e, src, idx, disp);
}

/* jmp rel32 */
static inline void emit_jmp_rel32(emit_t *e) {
    emit_byte(e, 0xE9);
    emit_u32(e, 0);  /* placeholder, patch later */
}

/* jcc rel32 */
static inline void emit_jcc_rel32(emit_t *e, uint8_t cc) {
    emit_byte(e, 0x0F);
    emit_byte(e, 0x80 + cc);
    emit_u32(e, 0);
}

#define JCC_E   0x04
#define JCC_NE  0x05
#define JCC_L   0x0C
#define JCC_GE  0x0D
#define JCC_B   0x02
#define JCC_AE  0x03

/* ret */
static inline void emit_ret(emit_t *e) {
    emit_byte(e, 0xC3);
}

/* push r64 */
static inline void emit_push(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0x50 + reg_lo(reg));
}

/* pop r64 */
static inline void emit_pop(emit_t *e, int reg) {
    if (reg_hi(reg))
        emit_byte(e, rex(0, 0, 0, reg_hi(reg)));
    emit_byte(e, 0x58 + reg_lo(reg));
}

/* mov r64, imm64 */
static inline void emit_mov_r64_imm64(emit_t *e, int reg, uint64_t imm) {
    emit_byte(e, rex(1, 0, 0, reg_hi(reg)));
    emit_byte(e, 0xB8 + reg_lo(reg));
    emit_u64(e, imm);
}

/* Context offsets in rv32_ctx_t */
#define CTX_NEXT_PC_OFF   128   /* x[32] = 128 bytes, then next_pc */
#define CTX_RAS_OFF       132   /* next_pc + 4 = 132, ras[0..31] */
#define CTX_RAS_TOP_OFF   260   /* 132 + 32*4 = 260, ras_top */
#define CTX_FP_OFF        264   /* ras_top + 4 = 264, f[0..31] x 8 bytes */
#define CTX_FCSR_OFF      520   /* 264 + 256 = 520 */

static inline void emit_exit_with_pc(emit_t *e, uint32_t next_pc) {
    /* mov dword [rbx + CTX_NEXT_PC_OFF], imm32 */
    emit_byte(e, 0xC7);
    emit_byte(e, modrm(0x02, 0, X64_RBX));
    emit_u32(e, CTX_NEXT_PC_OFF);
    emit_u32(e, next_pc);
    emit_ret(e);
}

/* add r64, r64 (64-bit) */
static inline void emit_add_r64_r64(emit_t *e, int dst, int src) {
    emit_byte(e, rex(1, reg_hi(src), 0, reg_hi(dst)));
    emit_byte(e, 0x01);
    emit_byte(e, modrm(0x03, src, dst));
}

/* call rax */
static inline void emit_call_rax(emit_t *e) {
    emit_byte(e, 0xFF);
    emit_byte(e, 0xD0);  /* ModRM: mod=11, /2, rm=RAX */
}

/* ---- SSE/FP emitter functions for RV32F/D ---- */

/* XMM register indices (0-15, we use 0 and 1 as scratch) */
#define XMM0  0
#define XMM1  1

/*
 * FP context access: load/store from ctx->f[freg] via RBX.
 * Each f[] slot is 8 bytes (uint64_t), at offset CTX_FP_OFF + freg*8.
 */

/* movss xmm, [rbx + CTX_FP_OFF + freg*8] — load single from FP context */
static inline void emit_load_fp_s(emit_t *e, int xmm, int freg) {
    int off = CTX_FP_OFF + freg * 8;
    emit_byte(e, 0xF3); /* prefix for scalar single */
    emit_byte(e, 0x0F);
    emit_byte(e, 0x10); /* movss xmm, m32 */
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, (uint8_t)off);
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, (uint32_t)off);
    }
}

/* Store single to FP context with NaN-boxing: upper 32 bits = 0xFFFFFFFF.
 * movss [rbx + off], xmm; mov dword [rbx + off + 4], 0xFFFFFFFF */
static inline void emit_store_fp_s(emit_t *e, int freg, int xmm) {
    int off = CTX_FP_OFF + freg * 8;
    /* movss [rbx + off], xmm */
    emit_byte(e, 0xF3);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x11); /* movss m32, xmm */
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, (uint8_t)off);
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, (uint32_t)off);
    }
    /* NaN-box: mov dword [rbx + off + 4], 0xFFFFFFFF */
    emit_byte(e, 0xC7);
    emit_byte(e, modrm(0x02, 0, X64_RBX));
    emit_u32(e, (uint32_t)(off + 4));
    emit_u32(e, 0xFFFFFFFF);
}

/* movsd xmm, [rbx + CTX_FP_OFF + freg*8] — load double from FP context */
static inline void emit_load_fp_d(emit_t *e, int xmm, int freg) {
    int off = CTX_FP_OFF + freg * 8;
    emit_byte(e, 0xF2); /* prefix for scalar double */
    emit_byte(e, 0x0F);
    emit_byte(e, 0x10); /* movsd xmm, m64 */
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, (uint8_t)off);
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, (uint32_t)off);
    }
}

/* movsd [rbx + CTX_FP_OFF + freg*8], xmm — store double to FP context */
static inline void emit_store_fp_d(emit_t *e, int freg, int xmm) {
    int off = CTX_FP_OFF + freg * 8;
    emit_byte(e, 0xF2);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x11); /* movsd m64, xmm */
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, (uint8_t)off);
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, (uint32_t)off);
    }
}

/* FP memory access: load/store via [R12 + idx + disp] (guest memory) */

/* movss xmm, [R12 + idx + disp] — load float from guest memory */
static inline void emit_load_mem_f32(emit_t *e, int xmm, int idx, int32_t disp) {
    emit_byte(e, 0xF3);
    emit_byte(e, rex(0, 0, reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x10);
    emit_sib_disp(e, xmm, idx, disp);
}

/* movss [R12 + idx + disp], xmm — store float to guest memory */
static inline void emit_store_mem_f32(emit_t *e, int idx, int xmm, int32_t disp) {
    emit_byte(e, 0xF3);
    emit_byte(e, rex(0, 0, reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x11);
    emit_sib_disp(e, xmm, idx, disp);
}

/* movsd xmm, [R12 + idx + disp] — load double from guest memory */
static inline void emit_load_mem_f64(emit_t *e, int xmm, int idx, int32_t disp) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(0, 0, reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x10);
    emit_sib_disp(e, xmm, idx, disp);
}

/* movsd [R12 + idx + disp], xmm — store double to guest memory */
static inline void emit_store_mem_f64(emit_t *e, int idx, int xmm, int32_t disp) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(0, 0, reg_hi(idx), 1));
    emit_byte(e, 0x0F);
    emit_byte(e, 0x11);
    emit_sib_disp(e, xmm, idx, disp);
}

/* ---- SSE scalar arithmetic (xmm, xmm) ---- */

/* Helper: emit prefix + 0F + opcode + modrm for SSE scalar single (F3 prefix) */
static inline void emit_sse_ss(emit_t *e, uint8_t op, int dst, int src) {
    emit_byte(e, 0xF3); emit_byte(e, 0x0F); emit_byte(e, op);
    emit_byte(e, modrm(0x03, dst, src));
}

/* Helper: emit prefix + 0F + opcode + modrm for SSE scalar double (F2 prefix) */
static inline void emit_sse_sd(emit_t *e, uint8_t op, int dst, int src) {
    emit_byte(e, 0xF2); emit_byte(e, 0x0F); emit_byte(e, op);
    emit_byte(e, modrm(0x03, dst, src));
}

static inline void emit_addss(emit_t *e, int d, int s) { emit_sse_ss(e, 0x58, d, s); }
static inline void emit_subss(emit_t *e, int d, int s) { emit_sse_ss(e, 0x5C, d, s); }
static inline void emit_mulss(emit_t *e, int d, int s) { emit_sse_ss(e, 0x59, d, s); }
static inline void emit_divss(emit_t *e, int d, int s) { emit_sse_ss(e, 0x5E, d, s); }
static inline void emit_sqrtss(emit_t *e, int d, int s) { emit_sse_ss(e, 0x51, d, s); }
static inline void emit_minss(emit_t *e, int d, int s) { emit_sse_ss(e, 0x5D, d, s); }
static inline void emit_maxss(emit_t *e, int d, int s) { emit_sse_ss(e, 0x5F, d, s); }

static inline void emit_addsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x58, d, s); }
static inline void emit_subsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5C, d, s); }
static inline void emit_mulsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x59, d, s); }
static inline void emit_divsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5E, d, s); }
static inline void emit_sqrtsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x51, d, s); }
static inline void emit_minsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5D, d, s); }
static inline void emit_maxsd(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5F, d, s); }

/* ---- SSE comparisons ---- */

/* ucomiss xmm, xmm (0F 2E) — sets EFLAGS */
static inline void emit_ucomiss(emit_t *e, int xmm1, int xmm2) {
    emit_byte(e, 0x0F); emit_byte(e, 0x2E);
    emit_byte(e, modrm(0x03, xmm1, xmm2));
}

/* ucomisd xmm, xmm (66 0F 2E) — sets EFLAGS */
static inline void emit_ucomisd(emit_t *e, int xmm1, int xmm2) {
    emit_byte(e, 0x66); emit_byte(e, 0x0F); emit_byte(e, 0x2E);
    emit_byte(e, modrm(0x03, xmm1, xmm2));
}

/* ---- SSE conversions ---- */

/* cvtss2sd xmm, xmm (F3 0F 5A) */
static inline void emit_cvtss2sd(emit_t *e, int d, int s) { emit_sse_ss(e, 0x5A, d, s); }

/* cvtsd2ss xmm, xmm (F2 0F 5A) */
static inline void emit_cvtsd2ss(emit_t *e, int d, int s) { emit_sse_sd(e, 0x5A, d, s); }

/* cvtsi2ss xmm, r32 (F3 0F 2A) */
static inline void emit_cvtsi2ss_r32(emit_t *e, int xmm, int r32) {
    emit_byte(e, 0xF3);
    if (reg_hi(r32)) emit_byte(e, rex(0, 0, 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0x2A);
    emit_byte(e, modrm(0x03, xmm, r32));
}

/* cvtsi2sd xmm, r32 (F2 0F 2A) */
static inline void emit_cvtsi2sd_r32(emit_t *e, int xmm, int r32) {
    emit_byte(e, 0xF2);
    if (reg_hi(r32)) emit_byte(e, rex(0, 0, 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0x2A);
    emit_byte(e, modrm(0x03, xmm, r32));
}

/* cvttss2si r32, xmm (F3 0F 2C) — truncate toward zero */
static inline void emit_cvttss2si_r32(emit_t *e, int r32, int xmm) {
    emit_byte(e, 0xF3);
    if (reg_hi(r32)) emit_byte(e, rex(0, 1, 0, 0));
    emit_byte(e, 0x0F); emit_byte(e, 0x2C);
    emit_byte(e, modrm(0x03, r32, xmm));
}

/* cvttsd2si r32, xmm (F2 0F 2C) — truncate toward zero */
static inline void emit_cvttsd2si_r32(emit_t *e, int r32, int xmm) {
    emit_byte(e, 0xF2);
    if (reg_hi(r32)) emit_byte(e, rex(0, 1, 0, 0));
    emit_byte(e, 0x0F); emit_byte(e, 0x2C);
    emit_byte(e, modrm(0x03, r32, xmm));
}

/* cvtsi2ss xmm, r64 (F3 REX.W 0F 2A) — 64-bit int to float (for unsigned 32-bit) */
static inline void emit_cvtsi2ss_r64(emit_t *e, int xmm, int r64) {
    emit_byte(e, 0xF3);
    emit_byte(e, rex(1, 0, 0, reg_hi(r64)));
    emit_byte(e, 0x0F); emit_byte(e, 0x2A);
    emit_byte(e, modrm(0x03, xmm, r64));
}

/* cvtsi2sd xmm, r64 (F2 REX.W 0F 2A) — 64-bit int to double (for unsigned 32-bit) */
static inline void emit_cvtsi2sd_r64(emit_t *e, int xmm, int r64) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(1, 0, 0, reg_hi(r64)));
    emit_byte(e, 0x0F); emit_byte(e, 0x2A);
    emit_byte(e, modrm(0x03, xmm, r64));
}

/* cvttss2si r64, xmm (F3 REX.W 0F 2C) — for unsigned 32-bit result */
static inline void emit_cvttss2si_r64(emit_t *e, int r64, int xmm) {
    emit_byte(e, 0xF3);
    emit_byte(e, rex(1, reg_hi(r64), 0, 0));
    emit_byte(e, 0x0F); emit_byte(e, 0x2C);
    emit_byte(e, modrm(0x03, r64, xmm));
}

/* cvttsd2si r64, xmm (F2 REX.W 0F 2C) — for unsigned 32-bit result */
static inline void emit_cvttsd2si_r64(emit_t *e, int r64, int xmm) {
    emit_byte(e, 0xF2);
    emit_byte(e, rex(1, reg_hi(r64), 0, 0));
    emit_byte(e, 0x0F); emit_byte(e, 0x2C);
    emit_byte(e, modrm(0x03, r64, xmm));
}

/* ---- Bit transfer (FMV.W.X / FMV.X.W) ---- */

/* movd xmm, r32 (66 0F 6E) */
static inline void emit_movd_xmm_r32(emit_t *e, int xmm, int r32) {
    emit_byte(e, 0x66);
    if (reg_hi(r32)) emit_byte(e, rex(0, 0, 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0x6E);
    emit_byte(e, modrm(0x03, xmm, r32));
}

/* movd r32, xmm (66 0F 7E) */
static inline void emit_movd_r32_xmm(emit_t *e, int r32, int xmm) {
    emit_byte(e, 0x66);
    if (reg_hi(r32)) emit_byte(e, rex(0, 0, 0, 1));
    emit_byte(e, 0x0F); emit_byte(e, 0x7E);
    emit_byte(e, modrm(0x03, xmm, r32));
}

/* movq xmm, [rbx + off] — load 64-bit into xmm (F3 0F 7E) */
static inline void emit_movq_xmm_ctx(emit_t *e, int xmm, int off) {
    emit_byte(e, 0xF3);
    emit_byte(e, 0x0F);
    emit_byte(e, 0x7E);
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, (uint8_t)off);
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, (uint32_t)off);
    }
}

/* movq [rbx + off], xmm — store 64-bit from xmm (66 0F D6) */
static inline void emit_movq_ctx_xmm(emit_t *e, int off, int xmm) {
    emit_byte(e, 0x66);
    emit_byte(e, 0x0F);
    emit_byte(e, 0xD6);
    if (off <= 127) {
        emit_byte(e, modrm(0x01, xmm, X64_RBX));
        emit_byte(e, (uint8_t)off);
    } else {
        emit_byte(e, modrm(0x02, xmm, X64_RBX));
        emit_u32(e, (uint32_t)off);
    }
}

/* xorps xmm, xmm — zero an XMM register (0F 57) */
static inline void emit_xorps(emit_t *e, int d, int s) {
    emit_byte(e, 0x0F); emit_byte(e, 0x57);
    emit_byte(e, modrm(0x03, d, s));
}

/* xorpd xmm, xmm — XOR double (66 0F 57) */
static inline void emit_xorpd(emit_t *e, int d, int s) {
    emit_byte(e, 0x66); emit_byte(e, 0x0F); emit_byte(e, 0x57);
    emit_byte(e, modrm(0x03, d, s));
}

/* movaps xmm, xmm (0F 28) */
static inline void emit_movaps(emit_t *e, int d, int s) {
    if (d == s) return;
    emit_byte(e, 0x0F); emit_byte(e, 0x28);
    emit_byte(e, modrm(0x03, d, s));
}

#endif /* EMIT_X64_H */

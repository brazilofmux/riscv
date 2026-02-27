# RV32IM setjmp/longjmp implementation
# jmp_buf layout: 14 callee-saved registers (56 bytes)
#   [0]  ra   (x1)
#   [4]  sp   (x2)
#   [8]  s0   (x8)
#   [12] s1   (x9)
#   [16] s2   (x18)
#   [20] s3   (x19)
#   [24] s4   (x20)
#   [28] s5   (x21)
#   [32] s6   (x22)
#   [36] s7   (x23)
#   [40] s8   (x24)
#   [44] s9   (x25)
#   [48] s10  (x26)
#   [52] s11  (x27)

    .text

    .global setjmp
    .type setjmp, @function
setjmp:
    sw      ra,  0(a0)
    sw      sp,  4(a0)
    sw      s0,  8(a0)
    sw      s1, 12(a0)
    sw      s2, 16(a0)
    sw      s3, 20(a0)
    sw      s4, 24(a0)
    sw      s5, 28(a0)
    sw      s6, 32(a0)
    sw      s7, 36(a0)
    sw      s8, 40(a0)
    sw      s9, 44(a0)
    sw      s10, 48(a0)
    sw      s11, 52(a0)
    li      a0, 0
    ret
    .size setjmp, . - setjmp

    .global longjmp
    .type longjmp, @function
longjmp:
    lw      ra,  0(a0)
    lw      sp,  4(a0)
    lw      s0,  8(a0)
    lw      s1, 12(a0)
    lw      s2, 16(a0)
    lw      s3, 20(a0)
    lw      s4, 24(a0)
    lw      s5, 28(a0)
    lw      s6, 32(a0)
    lw      s7, 36(a0)
    lw      s8, 40(a0)
    lw      s9, 44(a0)
    lw      s10, 48(a0)
    lw      s11, 52(a0)
    mv      a0, a1
    bnez    a0, 1f
    li      a0, 1       # longjmp(env, 0) returns 1
1:
    ret
    .size longjmp, . - longjmp

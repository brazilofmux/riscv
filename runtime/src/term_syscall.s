# RV32IM terminal syscall wrappers
# Convention: a7 = syscall number, a0-a5 = args, a0 = return value

    .text

    .global _term_setraw
    .type _term_setraw, @function
_term_setraw:
    li      a7, 500          # term_setraw(mode=a0)
    ecall
    ret
    .size _term_setraw, . - _term_setraw

    .global _term_getsize
    .type _term_getsize, @function
_term_getsize:
    li      a7, 501          # term_getsize(buf=a0)
    ecall
    ret
    .size _term_getsize, . - _term_getsize

    .global _term_kbhit_raw
    .type _term_kbhit_raw, @function
_term_kbhit_raw:
    li      a7, 502          # term_kbhit() -> a0
    ecall
    ret
    .size _term_kbhit_raw, . - _term_kbhit_raw

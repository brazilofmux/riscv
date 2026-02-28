# RV32IM time syscall wrappers
# Convention: a7 = syscall number, a0-a5 = args, a0 = return value

    .text

    .global _clock_gettime
    .type _clock_gettime, @function
_clock_gettime:
    li      a7, 403          # clock_gettime(clockid=a0, tp=a1)
    ecall
    ret
    .size _clock_gettime, . - _clock_gettime

    .global _get_cpu_clock
    .type _get_cpu_clock, @function
_get_cpu_clock:
    li      a7, 404          # get_cpu_clock() -> a0
    ecall
    ret
    .size _get_cpu_clock, . - _get_cpu_clock

# RV32IM syscall wrappers using ECALL
# Convention: a7 = syscall number, a0-a5 = args, a0 = return value

    .text

    .global _exit
    .type _exit, @function
_exit:
    li      a7, 93
    ecall
    j       _exit            # should not return
    .size _exit, . - _exit

    .global _write
    .type _write, @function
_write:
    li      a7, 64           # write(fd=a0, buf=a1, len=a2)
    ecall
    ret
    .size _write, . - _write

    .global _read
    .type _read, @function
_read:
    li      a7, 63           # read(fd=a0, buf=a1, len=a2)
    ecall
    ret
    .size _read, . - _read

    .global _openat
    .type _openat, @function
_openat:
    li      a7, 56           # openat(dirfd=a0, path=a1, flags=a2, mode=a3)
    ecall
    ret
    .size _openat, . - _openat

    .global _close
    .type _close, @function
_close:
    li      a7, 57           # close(fd=a0)
    ecall
    ret
    .size _close, . - _close

    .global _lseek
    .type _lseek, @function
_lseek:
    li      a7, 62           # lseek(fd=a0, offset=a1, whence=a2)
    ecall
    ret
    .size _lseek, . - _lseek

    .global _fstat
    .type _fstat, @function
_fstat:
    li      a7, 80           # fstat(fd=a0, statbuf=a1)
    ecall
    ret
    .size _fstat, . - _fstat

    .global _unlinkat
    .type _unlinkat, @function
_unlinkat:
    li      a7, 35           # unlinkat(dirfd=a0, path=a1, flags=a2)
    ecall
    ret
    .size _unlinkat, . - _unlinkat

    .global _ftruncate
    .type _ftruncate, @function
_ftruncate:
    li      a7, 46           # ftruncate(fd=a0, length=a1)
    ecall
    ret
    .size _ftruncate, . - _ftruncate

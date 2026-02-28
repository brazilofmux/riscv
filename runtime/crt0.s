# RV32IM C runtime startup code
# SP is set by the ELF loader (host DBT sets x2 = stack_top)
# This code zeroes .bss and calls main()

    .section .text.init
    .global _start
    .type _start, @function
_start:
    # Zero BSS section (word-aligned by linker script)
    la      t0, __bss_start
    la      t1, __bss_end
    beq     t0, t1, .Lbss_done
.Lbss_loop:
    sw      zero, 0(t0)
    addi    t0, t0, 4
    bne     t0, t1, .Lbss_loop
.Lbss_done:

    # Expand data segment so the heap is mapped.
    # Under our DBT this is a no-op; under QEMU user-mode it maps
    # the memory between __bss_end and __heap_end.
    la      a0, __heap_end
    li      a7, 214         # brk
    ecall

    # Set frame pointer to zero (end of call chain)
    li      fp, 0

    # Load argc/argv from the stack (Linux ABI layout).
    # Works under both our DBT and qemu-riscv32 user-mode.
    lw      a0, 0(sp)           # a0 = argc
    addi    a1, sp, 4           # a1 = &argv[0]
    call    main

    # Exit with main's return value (in a0)
    call    exit

    # Should not reach here
.Lhalt:
    j       .Lhalt

    .size _start, . - _start

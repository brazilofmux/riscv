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

    # Set frame pointer to zero (end of call chain)
    li      fp, 0

    # Call main(argc=0, argv=NULL)
    li      a0, 0
    li      a1, 0
    call    main

    # Exit with main's return value (in a0)
    call    exit

    # Should not reach here
.Lhalt:
    j       .Lhalt

    .size _start, . - _start

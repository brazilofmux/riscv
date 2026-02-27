.section .text.init
.global _start

# Register assignments
# x0: Zero
# x1-x2: Temp
# x3-x10: C Args (volatile)
# x11-x25: Saved/General
# x26: IP (Instruction Pointer)
# x27: RSP (Return Stack Pointer)
# x28: DSP (Data Stack Pointer)
# x29: System Stack (SP)
# x30: Frame Pointer
# x1: Link Register

# Direct Threaded Code implementation

# Entry point
_start:
    # Initialize stacks
    lui x28, %hi(dstack_top)
    addi x28, x28, %lo(dstack_top)
    
    lui x27, %hi(rstack_top)
    addi x27, x27, %lo(rstack_top)

    # Set IP to the start of our Forth program (COLD_START)
    lui x26, %hi(cold_start_body)
    addi x26, x26, %lo(cold_start_body)

    # Start the interpreter
    j next

# The Inner Interpreter
next:
    lw x25, 0(x26)    # W = *IP (XT pointer)
    addi x26, x26, 4   # IP++
    lw x24, 0(x25)    # code pointer = *XT
    jr x24    # Jump to code pointer

# ----------------------------------------------------------------------
# Primitives with Dictionary Headers
# ----------------------------------------------------------------------
# Link format: Points to previous word's header (start)
# Header: Link(4), Len(1), Name(N), Padding(Align 2), XT(4)

# Word: EXIT
.text
    .p2align 2, 0
head_exit:
    .word 0            # Link (First word)
    .byte 4
    .ascii "EXIT"
    .p2align 2, 0
xt_exit:
    .word exit_word

exit_word:
    lw x26, 0(x27)    # IP = *RSP
    addi x27, x27, 4   # RSP++
    j next

# Word: DOCOL (Internal)
docol_word:
    addi x27, x27, -4  # RSP--
    sw x26, 0(x27)    # *RSP = IP (Save old IP)
    addi x26, x25, 4   # IP = XT + 4 (body)
    j next

# Word: DOCREATE (Internal) - runtime for CREATE'd words
# Layout: [docreate] [does-cell] [data...]
#          W          W+4         W+8 = PFA
docreate:
    addi x1, x25, 8    # PFA = W + 8 (skip code-ptr + does-cell)
    addi x28, x28, -4
    sw x1, 0(x28)     # push PFA
    j next

# Word: DODOES (Internal) - runtime for DOES> modified words
# Layout: [dodoes] [does-thread-addr] [data...]
#          W        W+4                W+8 = PFA
dodoes:
    addi x1, x25, 8    # PFA = W + 8
    addi x28, x28, -4
    sw x1, 0(x28)     # push PFA
    addi x27, x27, -4
    sw x26, 0(x27)    # push IP to return stack
    lw x26, 4(x25)    # IP = does-thread (from XT+4)
    j next

# Word: LIT ( -- x ) runtime: push next cell from instruction stream
.text
    .p2align 2, 0
head_lit:
    .word head_exit
    .byte 3
    .ascii "LIT"
    .p2align 2, 0
xt_lit:
    .word lit_word
lit_word:
    lw x1, 0(x26)     # x1 = *IP
    addi x26, x26, 4   # IP++
    addi x28, x28, -4  # DSP--
    sw x1, 0(x28)     # *DSP = x1
    j next

# Word: EXECUTE ( xt -- )
.text
    .p2align 2, 0
head_execute:
    .word head_lit
    .byte 7
    .ascii "EXECUTE"
    .p2align 2, 0
xt_execute:
    .word execute_word

execute_word:
    lw x25, 0(x28)     # xt = *DSP
    addi x28, x28, 4    # DSP++
    lw x24, 0(x25)     # code pointer
    jr x24

# Word: DUP ( a -- a a )
.text
    .p2align 2, 0
head_dup:
    .word head_execute
    .byte 3
    .ascii "DUP"
    .p2align 2, 0
xt_dup:
    .word dup_word

dup_word:
    lw x1, 0(x28)     # x1 = TOS
    addi x28, x28, -4  # DSP--
    sw x1, 0(x28)     # Push x1
    j next

# Word: DROP ( a -- )
.text
    .p2align 2, 0
head_drop:
    .word head_dup
    .byte 4
    .ascii "DROP"
    .p2align 2, 0
xt_drop:
    .word drop_word

drop_word:
    addi x28, x28, 4   # DSP++
    j next

# Word: SWAP ( a b -- b a )
.text
    .p2align 2, 0
head_swap:
    .word head_drop
    .byte 4
    .ascii "SWAP"
    .p2align 2, 0
xt_swap:
    .word swap_word

swap_word:
    lw x1, 0(x28)     # x1 = b (TOS)
    lw x2, 4(x28)     # x2 = a (TOS+4)
    sw x2, 0(x28)     # *DSP = a (New TOS)
    sw x1, 4(x28)     # *(DSP+4) = b
    j next

# Word: EMIT ( c -- )
.text
    .p2align 2, 0
head_emit:
    .word head_swap
    .byte 4
    .ascii "EMIT"
    .p2align 2, 0
xt_emit:
    .word emit_word

emit_word:
    lw x3, 0(x28)     # x3 = TOS (char)
    addi x28, x28, 4   # DSP++
    la x11, emit_buf
    sb x3, 0(x11)      # store char in buffer
    li x10, 1           # fd = stdout
    li x12, 1           # len = 1
    li x17, 64          # syscall = write
    ecall
    j next

# Word: KEY ( -- c )
.text
    .p2align 2, 0
head_key:
    .word head_emit
    .byte 3
    .ascii "KEY"
    .p2align 2, 0
xt_key:
    .word key_word

key_word:
    li x10, 0           # fd = stdin
    la x11, key_buf
    li x12, 1           # len = 1
    li x17, 63          # syscall = read
    ecall
    blez x10, .Lkey_eof
    la x1, key_buf
    lbu x1, 0(x1)       # load the byte
    j .Lkey_push
.Lkey_eof:
    li x1, -1           # EOF sentinel
.Lkey_push:
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: FIND ( c-addr -- xt | 0 )
.text
    .p2align 2, 0
head_find:
    .word head_key
    .byte 4
    .ascii "FIND"
    .p2align 2, 0
xt_find:
    .word find_word

# FIND ( c-addr -- xt flag | 0 0 )
# flag = 1 if the word is IMMEDIATE, 0 otherwise
# Searches all wordlists in the search order
find_word:
    lw x4, 0(x28)     # x4 = search string address
    addi x28, x28, 4   # pop input

    # Set up search order iteration
    lui x20, %hi(search_order)
    addi x20, x20, %lo(search_order)   # x20 = search_order base
    add x21, x0, x0                    # x21 = index (0)
    lui x22, %hi(search_order_count)
    addi x22, x22, %lo(search_order_count)
    lw x22, 0(x22)                    # x22 = count

find_search_next_wl:
    bge x21, x22, find_fail            # all wordlists exhausted

    # Load head of current wordlist
    add x23, x21, x21
    add x23, x23, x23                  # x23 = index * 4
    add x23, x20, x23                  # x23 = &search_order[index]
    lw x5, 0(x23)                     # x5 = wid (pointer to head cell)
    lw x5, 0(x5)                      # x5 = head of wordlist

find_loop:
    beq x5, x0, find_wl_exhausted

    # Compare length (mask out IMMEDIATE bit)
    lbu x6, 4(x5)     # dict len/raw flags
    addi x7, x0, 0x7F
    and x8, x6, x7     # dict_len = len & 0x7F
    lbu x7, 0(x4)     # search len
    bne x8, x7, find_next

    # Compare bytes
    add x9, x0, x0     # i = 0
find_str_loop:
    bge x9, x8, find_match

    add x10, x5, x9
    lbu x10, 5(x10)     # dict char (offset 5 + i)

    add x11, x4, x9
    lbu x11, 1(x11)     # search char (offset 1 + i)

    bne x10, x11, find_next

    addi x9, x9, 1
    j find_str_loop

find_next:
    lw x5, 0(x5)      # Load link
    j find_loop

find_wl_exhausted:
    addi x21, x21, 1   # next wordlist
    j find_search_next_wl

find_match:
    # Found. Calculate XT address.
    # XT is at header + aligned(5 + len)
    addi x5, x5, 5
    add x5, x5, x8     # x5 = end of name string

    # Align x5 to 4 bytes
    # mask = -4 (0xFFFFFFFC)
    addi x10, x0, -4
    addi x5, x5, 3
    and x5, x5, x10

    # Push xt pointer and immediate flag
    addi x28, x28, -4
    sw x5, 0(x28)     # xt

    addi x10, x0, 0x80
    and x6, x6, x10
    beq x6, x0, find_not_immediate
    addi x6, x0, 1
find_not_immediate:
    addi x28, x28, -4
    sw x6, 0(x28)     # flag

    j next

find_fail:
    addi x28, x28, -4
    sw x0, 0(x28)     # xt = 0
    addi x28, x28, -4
    sw x0, 0(x28)     # flag = 0
    j next


# Word: BYE
.text
    .p2align 2, 0
head_bye:
    .word head_find
    .byte 3
    .ascii "BYE"
    .p2align 2, 0
xt_bye:
    .word bye_word

bye_word:
    li x10, 0           # exit code = 0
    li x17, 93          # syscall = exit
    ecall

# Word: ACCEPT ( c-addr +n -- +n' )
# Read at most +n chars into c-addr. Stop at newline. Return num chars read.
.text
    .p2align 2, 0
head_accept:
    .word head_bye
    .byte 6
    .ascii "ACCEPT"
    .p2align 2, 0
xt_accept:
    .word accept_word

accept_word:
    lw x4, 0(x28)     # x4 = max length (+n)
    lw x5, 4(x28)     # x5 = buffer address (c-addr)
    addi x28, x28, 8   # Pop both args

    add x6, x0, x0     # x6 = count (i)

accept_loop:
    beq x6, x4, accept_done   # If count == max, done

    # Save loop state to callee-saved regs (ecall clobbers x10-x12, x17)
    mv x18, x4          # x18 = max len
    mv x19, x5          # x19 = buffer addr
    mv x20, x6          # x20 = count

    # Read one char via ecall
    li x10, 0            # fd = stdin
    la x11, key_buf
    li x12, 1            # len = 1
    li x17, 63           # syscall = read
    ecall

    # Restore loop state
    mv x4, x18
    mv x5, x19
    mv x6, x20

    # Check read result
    blez x10, accept_eof  # EOF or error
    la x1, key_buf
    lbu x1, 0(x1)        # x1 = char read

    # Check Newline (10)
    addi x2, x0, 10
    beq x1, x2, accept_done

    # Check CR (13) - treat as newline
    addi x2, x0, 13
    beq x1, x2, accept_done
    
    # Store char
    add x7, x5, x6     # addr + i
    sb x1, 0(x7)
    
    addi x6, x6, 1     # i++
    j accept_loop

accept_eof:
    # EOF with no chars read: return -1 to signal EOF
    bne x6, x0, accept_done  # if we already read something, return count
    addi x6, x0, -1           # signal EOF
accept_done:
    addi x28, x28, -4  # push result
    sw x6, 0(x28)     # Store count

    # Update #TIB and reset >IN (use count or 0 for EOF)
    add x7, x6, x0      # save count
    blt x6, x0, accept_eof_tib  # if EOF (-1), set #TIB=0
    lui x8, %hi(var_source_len)
    addi x8, x8, %lo(var_source_len)
    sw x6, 0(x8)
    j accept_reset_in
accept_eof_tib:
    lui x8, %hi(var_source_len)
    addi x8, x8, %lo(var_source_len)
    sw x0, 0(x8)
accept_reset_in:
    lui x8, %hi(var_to_in)
    addi x8, x8, %lo(var_to_in)
    sw x0, 0(x8)
    j next


# Word: PARSE-WORD ( -- c-addr u )
# Skips leading delimiters (space=32)
# Parses until next delimiter
# Returns string address (in SOURCE) and length
# Updates >IN
.text
    .p2align 2, 0
head_parse_name:
    .word head_accept
    .byte 10
    .ascii "PARSE-NAME"
    .p2align 2, 0
xt_parse_name:
    .word parse_word_word

head_parse_word:
    .word head_parse_name
    .byte 10
    .ascii "PARSE-WORD"
    .p2align 2, 0
xt_parse_word:
    .word parse_word_word

parse_word_word:
    # Load SOURCE base, #TIB, >IN
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)           # x1 = SOURCE base
    
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)           # x2 = #TIB (Limit)
    
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)           # x4 = >IN (Current Offset)
    
    # 1. Skip leading spaces
parse_skip:
    bge x4, x2, parse_fail  # If >IN >= #TIB, fail (return 0 0)
    
    add x5, x1, x4          # current addr = TIB + >IN
    lbu x6, 0(x5)          # load char
    
    addi x7, x0, 32         # Space
    bne x6, x7, parse_found # If not space, found start
    
    addi x4, x4, 1          # >IN++
    j parse_skip

parse_found:
    add x11, x1, x4         # x11 = Start Address
    add x12, x4, x0         # x12 = Start Index
    
    # 2. Scan until space or end
parse_scan:
    bge x4, x2, parse_end   # If >IN >= #TIB, end
    
    add x5, x1, x4
    lbu x6, 0(x5)
    
    addi x7, x0, 32         # Space
    beq x6, x7, parse_end   # If space, end
    
    # Also check for invisible control chars? For now just space.
    
    addi x4, x4, 1          # >IN++
    j parse_scan

parse_end:
    # x4 is now at the delimiter (or end)
    # Length = x4 - x12
    sub x13, x4, x12
    
    # Update >IN variable
    sw x4, 0(x3)
    
    # Push ( addr len )
    addi x28, x28, -4
    sw x11, 0(x28)    # Push addr
    addi x28, x28, -4
    sw x13, 0(x28)    # Push len
    j next

parse_fail:
    # Return ( 0 0 )
    addi x28, x28, -4
    sw x0, 0(x28)
    addi x28, x28, -4
    sw x0, 0(x28)
    j next


# Word: TYPE ( c-addr u -- )
.text
    .p2align 2, 0
head_type:
    .word head_parse_word
    .byte 4
    .ascii "TYPE"
    .p2align 2, 0
xt_type:
    .word type_word

type_word:
    lw x12, 0(x28)     # x12 = len (a2)
    lw x11, 4(x28)     # x11 = addr (a1)
    addi x28, x28, 8   # Pop both
    beq x12, x0, type_done
    li x10, 1           # fd = stdout
    li x17, 64          # syscall = write
    ecall
type_done:
    j next

# Word: DOT ( n -- ) print number in current BASE with trailing space
.text
    .p2align 2, 0
head_dot:
    .word head_type
    .byte 1
    .ascii "."
    .p2align 2, 0
xt_dot:
    .word dot_word
dot_word:
    jal dot_print_sub
    j next

dot_print_sub:
    add x16, x1, x0     # save caller return
    lw x4, 0(x28)     # n
    addi x28, x28, 4   # pop

    # Load BASE
    lui x5, %hi(var_base)
    addi x5, x5, %lo(var_base)
    lw x5, 0(x5)      # base

    add x6, x0, x0     # sign flag
    add x10, x4, x0    # work = n

    # If base==10 and n<0, remember sign and negate
    addi x7, x0, 10
    bne x5, x7, dot_no_sign
    blt x4, x0, dot_make_positive
    j dot_no_sign
dot_make_positive:
    sub x10, x0, x10   # work = -n
    addi x6, x0, 1     # sign = 1
dot_no_sign:

    # Set buffer end (use top half of PAD)
    lui x8, %hi(pad)
    addi x8, x8, %lo(pad)
    addi x8, x8, 127   # buffer end
    add x9, x8, x0     # keep end pointer

    # Special case n==0
    bne x10, x0, dot_loop
    addi x8, x8, -1
    addi x7, x0, 48    # '0'
    sb x7, 0(x8)
    j dot_digits_done

dot_loop:
    div x11, x10, x5   # quot = work / base
    rem x12, x10, x5   # rem  = work % base
    add x10, x11, x0   # work = quot

    # Convert digit to ASCII
    addi x7, x0, 10
    blt x12, x7, dot_digit_numeric
    addi x12, x12, 55  # 'A' = 65 -> 10 -> +55
    j dot_store_digit
dot_digit_numeric:
    addi x12, x12, 48
dot_store_digit:
    addi x8, x8, -1
    sb x12, 0(x8)

    bne x10, x0, dot_loop

dot_digits_done:
    # Add sign if needed
    beq x6, x0, dot_build_done
    addi x8, x8, -1
    addi x7, x0, 45    # '-'
    sb x7, 0(x8)

dot_build_done:
    sub x13, x9, x8    # len = end - start

    # Append trailing space
    addi x7, x0, 32
    sb x7, 0(x9)
    addi x13, x13, 1   # include space

    # Emit string with single write ecall
    mv x11, x8          # addr = start of string
    mv x12, x13         # len
    li x10, 1            # fd = stdout
    li x17, 64           # syscall = write
    ecall
    mv x1, x16          # restore return address
    ret


# Word: HELLO ( -- )
.text
    .p2align 2, 0
head_hello:
    .word head_dot    # Linked to DOT
    .byte 5
    .ascii "HELLO"
    .p2align 2, 0
xt_hello:
    .word docol_word
    .word xt_lit
    .word 72
    .word xt_emit  # H
    .word xt_lit
    .word 101
    .word xt_emit # e
    .word xt_lit
    .word 108
    .word xt_emit # l
    .word xt_lit
    .word 108
    .word xt_emit # l
    .word xt_lit
    .word 111
    .word xt_emit # o
    .word xt_lit
    .word 10
    .word xt_emit  # \n
    .word xt_exit


# Word: STATE ( -- a-addr )
.text
    .p2align 2, 0
head_state:
    .word head_hello
    .byte 5
    .ascii "STATE"
    .p2align 2, 0
xt_state:
    .word state_word
state_word:
    lui x1, %hi(var_state)
    addi x1, x1, %lo(var_state)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: BASE ( -- a-addr )
.text
    .p2align 2, 0
head_base:
    .word head_state
    .byte 4
    .ascii "BASE"
    .p2align 2, 0
xt_base:
    .word base_word
base_word:
    lui x1, %hi(var_base)
    addi x1, x1, %lo(var_base)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: BASE! ( x -- ) store into BASE without needing addr
.text
    .p2align 2, 0
head_base_store:
    .word head_base
    .byte 5
    .ascii "BASE!"
    .p2align 2, 0
xt_base_store:
    .word base_store_word
base_store_word:
    lw x1, 0(x28)     # x
    addi x28, x28, 4   # pop
    lui x2, %hi(var_base)
    addi x2, x2, %lo(var_base)
    sw x1, 0(x2)      # *var_base = x
    j next

# Word: LATEST ( -- a-addr ) address of latest dictionary pointer
.text
    .p2align 2, 0
head_latest:
    .word head_base_store
    .byte 6
    .ascii "LATEST"
    .p2align 2, 0
xt_latest:
    .word latest_word
latest_word:
    lui x1, %hi(var_compilation_wid)
    addi x1, x1, %lo(var_compilation_wid)
    lw x1, 0(x1)      # x1 = current compilation wid
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: HERE ( -- a-addr )
.text
    .p2align 2, 0
head_here:
    .word head_latest
    .byte 4
    .ascii "HERE"
    .p2align 2, 0
xt_here:
    .word here_word
here_word:
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: TIB ( -- a-addr )
.text
    .p2align 2, 0
head_tib:
    .word head_here
    .byte 3
    .ascii "TIB"
    .p2align 2, 0
xt_tib:
    .word tib_word
tib_word:
    lui x1, %hi(tib)
    addi x1, x1, %lo(tib)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: SOURCE-PTR ( -- a-addr )
.text
    .p2align 2, 0
head_source_ptr:
    .word head_tib
    .byte 10
    .ascii "SOURCE-PTR"
    .p2align 2, 0
xt_source_ptr:
    .word source_ptr_word
source_ptr_word:
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: TOIN ( -- a-addr )
.text
    .p2align 2, 0
head_to_in:
    .word head_source_ptr
    .byte 4
    .ascii "TOIN"
    .p2align 2, 0
xt_to_in:
    .word to_in_word
to_in_word:
    lui x1, %hi(var_to_in)
    addi x1, x1, %lo(var_to_in)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: NTIB ( -- a-addr ) - Length of TIB
.text
    .p2align 2, 0
head_num_tib:
    .word head_to_in
    .byte 4
    .ascii "NTIB"
    .p2align 2, 0
xt_num_tib:
    .word num_tib_word
num_tib_word:
    lui x1, %hi(var_source_len)
    addi x1, x1, %lo(var_source_len)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: ! ( x a-addr -- )
.text
    .p2align 2, 0
head_store:
    .word head_num_tib
    .byte 1
    .ascii "!"
    .p2align 2, 0
xt_store:
    .word store_word

store_word:
    lw x1, 0(x28)     # addr
    lw x2, 4(x28)     # x
    addi x28, x28, 8   # Pop
    sw x2, 0(x1)      # *addr = x (Remember: stw base, src, offset)
    j next

# Word: @ ( a-addr -- x )
.text
    .p2align 2, 0
head_fetch:
    .word head_store
    .byte 1
    .ascii "@"
    .p2align 2, 0
xt_fetch:
    .word fetch_word

fetch_word:
    lw x1, 0(x28)     # addr
    lw x2, 0(x1)      # x = *addr
    sw x2, 0(x28)     # Replace TOS
    j next

# Word: CR ( -- )
.text
    .p2align 2, 0
head_cr:
    .word head_fetch
    .byte 2
    .ascii "CR"
    .p2align 2, 0
xt_cr:
    .word cr_word
cr_word:
    li x10, 1           # fd = stdout
    la x11, newline_buf
    li x12, 1           # len = 1
    li x17, 64          # syscall = write
    ecall
    j next

# Word: C@ ( c-addr -- x ) unsigned byte fetch
.text
    .p2align 2, 0
head_cfetch:
    .word head_cr
    .byte 2
    .ascii "C@"
    .p2align 2, 0
xt_cfetch:
    .word cfetch_word
cfetch_word:
    lw x1, 0(x28)     # addr
    lbu x2, 0(x1)     # byte
    sw x2, 0(x28)     # replace TOS
    j next

# Word: OVER ( a b -- a b a )
.text
    .p2align 2, 0
head_over:
    .word head_cfetch
    .byte 4
    .ascii "OVER"
    .p2align 2, 0
xt_over:
    .word over_word
over_word:
    lw x1, 4(x28)     # fetch second stack item
    addi x28, x28, -4  # push
    sw x1, 0(x28)
    j next

# Word: >R ( x -- )  move to return stack
.text
    .p2align 2, 0
head_to_r:
    .word head_over
    .byte 2
    .ascii ">R"
    .p2align 2, 0
xt_to_r:
    .word to_r_word
to_r_word:
    lw x1, 0(x28)
    addi x28, x28, 4   # pop data
    addi x27, x27, -4
    sw x1, 0(x27)
    j next

# Word: R> ( -- x )  move from return stack
.text
    .p2align 2, 0
head_r_from:
    .word head_to_r
    .byte 2
    .ascii "R>"
    .p2align 2, 0
xt_r_from:
    .word r_from_word
r_from_word:
    lw x1, 0(x27)
    addi x27, x27, 4
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: R@ ( -- x )  copy return stack top
.text
    .p2align 2, 0
head_r_fetch:
    .word head_r_from
    .byte 2
    .ascii "R@"
    .p2align 2, 0
xt_r_fetch:
    .word r_fetch_word
r_fetch_word:
    lw x1, 0(x27)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: + ( a b -- sum )
.text
    .p2align 2, 0
head_plus:
    .word head_r_fetch
    .byte 1
    .ascii "+"
    .p2align 2, 0
xt_plus:
    .word plus_word
plus_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    add x2, x2, x1
    addi x28, x28, 4   # pop one
    sw x2, 0(x28)     # replace TOS
    j next

# Word: - ( a b -- a-b )
.text
    .p2align 2, 0
head_minus:
    .word head_plus
    .byte 1
    .ascii "-"
    .p2align 2, 0
xt_minus:
    .word minus_word
minus_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    sub x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: * ( a b -- a*b )
.text
    .p2align 2, 0
head_mul:
    .word head_minus
    .byte 1
    .ascii "*"
    .p2align 2, 0
xt_mul:
    .word mul_word
mul_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    mul x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: AND ( a b -- a&b )
.text
    .p2align 2, 0
head_and:
    .word head_mul
    .byte 3
    .ascii "AND"
    .p2align 2, 0
xt_and:
    .word and_word
and_word:
    lw x1, 0(x28)
    lw x2, 4(x28)
    and x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: OR ( a b -- a|b )
.text
    .p2align 2, 0
head_or:
    .word head_and
    .byte 2
    .ascii "OR"
    .p2align 2, 0
xt_or:
    .word or_word
or_word:
    lw x1, 0(x28)
    lw x2, 4(x28)
    or x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: = ( a b -- flag )
.text
    .p2align 2, 0
head_equals:
    .word head_or
    .byte 1
    .ascii "="
    .p2align 2, 0
xt_equals:
    .word equals_word
equals_word:
    lw x1, 0(x28)
    lw x2, 4(x28)
    addi x3, x0, 1
    bne x1, x2, equals_zero
    j equals_push
equals_zero:
    addi x3, x0, 0
equals_push:
    addi x28, x28, 4
    sw x3, 0(x28)
    j next

# Word: < ( a b -- flag ) signed
.text
    .p2align 2, 0
head_less:
    .word head_equals
    .byte 1
    .ascii "<"
    .p2align 2, 0
xt_less:
    .word less_word
less_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    addi x3, x0, 0
    blt x2, x1, less_set
    j less_push
less_set:
    addi x3, x0, 1
less_push:
    addi x28, x28, 4
    sw x3, 0(x28)
    j next

# Word: BRANCH ( -- ) (runtime for compiled loops)
.text
    .p2align 2, 0
head_branch:
    .word head_less
    .byte 6
    .ascii "BRANCH"
    .p2align 2, 0
xt_branch:
    .word branch_word
branch_word:
    lw x1, 0(x26)     # offset
    addi x26, x26, 4   # skip offset cell
    add x26, x26, x1   # IP += offset (offset is relative to next cell)
    j next

# Word: 0BRANCH ( flag -- ) branches if flag == 0
.text
    .p2align 2, 0
head_0branch:
    .word head_branch
    .byte 7
    .ascii "0BRANCH"
    .p2align 2, 0
xt_0branch:
    .word zbranch_word
zbranch_word:
    lw x1, 0(x28)     # flag
    addi x28, x28, 4   # pop flag
    bne x1, x0, zbranch_fallthrough
    lw x1, 0(x26)     # offset
    add x26, x26, x1   # IP += offset
    addi x26, x26, 4   # skip offset cell
    j next
zbranch_fallthrough:
    addi x26, x26, 4   # skip offset cell
    j next

# Word: WORD ( -- c-addr ) parses next token into PAD as counted string
.text
    .p2align 2, 0
head_word:
    .word head_0branch
    .byte 4
    .ascii "WORD"
    .p2align 2, 0
xt_word:
    .word word_word
word_word:
    # Load SOURCE base, #TIB, >IN
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)           # x1 = SOURCE base
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)           # x2 = #TIB
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)           # x4 = >IN

    # Skip leading spaces
word_skip:
    bge x4, x2, word_empty
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    bne x6, x7, word_found
    addi x4, x4, 1
    j word_skip

word_found:
    add x12, x4, x0         # start index
    add x11, x1, x4         # start addr

    # Scan until space or end
word_scan:
    bge x4, x2, word_end
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    beq x6, x7, word_end
    addi x4, x4, 1
    j word_scan

word_end:
    sub x13, x4, x12        # len
    sw x4, 0(x3)           # update >IN

    # Build counted string in PAD (uppercased)
    lui x14, %hi(pad)
    addi x14, x14, %lo(pad)
    sb x13, 0(x14)         # length byte
    addi x15, x14, 1        # dest pointer
    add x16, x11, x0        # src pointer
    add x17, x0, x0         # i = 0

word_copy_loop:
    bge x17, x13, word_copy_done
    lbu x18, 0(x16)
    addi x19, x0, 97        # 'a'
    blt x18, x19, word_no_upper
    addi x19, x0, 122       # 'z'
    bgt x18, x19, word_no_upper
    addi x18, x18, -32      # uppercase
word_no_upper:
    sb x18, 0(x15)
    addi x15, x15, 1
    addi x16, x16, 1
    addi x17, x17, 1
    j word_copy_loop

word_copy_done:
    addi x28, x28, -4
    sw x14, 0(x28)         # push PAD address
    j next

word_empty:
    lui x14, %hi(pad)
    addi x14, x14, %lo(pad)
    sb x0, 0(x14)          # len = 0
    addi x28, x28, -4
    sw x14, 0(x28)
    j next

# Word: NUMBER ( c-addr -- n flag ) convert counted string using BASE
# Supports prefixes: $hex, #decimal, %binary, 'c' character literal
.text
    .p2align 2, 0
head_number:
    .word head_word
    .byte 6
    .ascii "NUMBER"
    .p2align 2, 0
xt_number:
    .word number_word
number_word:
    lw x4, 0(x28)      # c-addr
    addi x28, x28, 4    # pop input
    lbu x5, 0(x4)      # len
    beq x5, x0, number_fail

    lui x13, %hi(var_base)
    addi x13, x13, %lo(var_base)
    lw x13, 0(x13)     # x13 = base (may be overridden by prefix)

    addi x6, x4, 1      # ptr to chars
    add x7, x0, x0      # i = 0
    add x10, x0, x0     # accumulator
    add x11, x0, x0     # sign flag

    # Check for leading sign
    lbu x8, 0(x6)
    addi x9, x0, 45     # '-'
    bne x8, x9, number_check_prefix
    addi x11, x0, 1     # negative
    addi x7, x7, 1
    addi x6, x6, 1

number_check_prefix:
    # Must have at least one more char after sign
    bge x7, x5, number_fail
    lbu x8, 0(x6)

    # $hex prefix
    addi x9, x0, 36     # '$'
    beq x8, x9, number_hex_prefix
    # #decimal prefix
    addi x9, x0, 35     # '#'
    beq x8, x9, number_dec_prefix
    # %binary prefix
    addi x9, x0, 37     # '%'
    beq x8, x9, number_bin_prefix
    # 'c' character literal
    addi x9, x0, 39     # single quote
    beq x8, x9, number_char_literal
    # No prefix: use current BASE
    j number_loop

number_hex_prefix:
    addi x13, x0, 16
    addi x6, x6, 1
    addi x7, x7, 1
    j number_loop

number_dec_prefix:
    addi x13, x0, 10
    addi x6, x6, 1
    addi x7, x7, 1
    j number_loop

number_bin_prefix:
    addi x13, x0, 2
    addi x6, x6, 1
    addi x7, x7, 1
    j number_loop

number_char_literal:
    # Expected form: 'c' (3 remaining chars: quote, char, quote)
    sub x8, x5, x7         # remaining = len - consumed
    addi x9, x0, 3
    bne x8, x9, number_fail
    # Check closing quote at x6+2
    lbu x8, 2(x6)
    addi x9, x0, 39        # '''
    bne x8, x9, number_fail
    # Get character value
    lbu x10, 1(x6)        # char between quotes
    j number_done

number_loop:
    bge x7, x5, number_done
    lbu x8, 0(x6)
    addi x9, x0, 97         # 'a'
    blt x8, x9, number_no_upper
    addi x9, x0, 122        # 'z'
    bgt x8, x9, number_no_upper
    addi x8, x8, -32        # uppercase
number_no_upper:
    # Check for underscore separator (skip it)
    addi x9, x0, 95         # '_'
    bne x8, x9, number_not_sep
    addi x6, x6, 1
    addi x7, x7, 1
    j number_loop
number_not_sep:
    addi x9, x0, 58         # '9'+1
    blt x8, x9, number_digit
    addi x9, x0, 65         # 'A'
    blt x8, x9, number_fail
    addi x9, x0, 91         # 'Z'+1
    bge x8, x9, number_fail
    addi x9, x8, -55        # digit = c - 'A' + 10
    j number_have_digit

number_digit:
    addi x9, x8, -48        # digit = c - '0'

number_have_digit:
    bge x9, x13, number_fail
    mul x10, x10, x13       # acc *= base
    add x10, x10, x9        # acc += digit
    addi x6, x6, 1
    addi x7, x7, 1
    j number_loop

number_fail:
    addi x28, x28, -4
    sw x0, 0(x28)          # n = 0
    addi x28, x28, -4
    sw x0, 0(x28)          # flag = 0
    j next

number_done:
    beq x11, x0, number_push
    sub x10, x0, x10        # negate if needed
number_push:
    addi x28, x28, -4
    sw x10, 0(x28)         # push n
    addi x28, x28, -4
    addi x1, x0, 1
    sw x1, 0(x28)          # flag = 1
    j next

# Word: INTERPRET ( -- ) processes current TIB
.text
    .p2align 2, 0
head_interpret:
    .word head_number
    .byte 9
    .ascii "INTERPRET"
    .p2align 2, 0
xt_interpret:
    .word interpret_word
interpret_word:
interpret_loop:
    # Parse next token via WORD
    lui x7, %hi(xt_word)
    addi x7, x7, %lo(xt_word)
    lui x13, %hi(interpret_after_word)
    addi x13, x13, %lo(interpret_after_word)
    j interpret_run_xt

interpret_after_word:
    lw x4, 0(x28)           # c-addr
    lbu x5, 0(x4)           # len
    beq x5, x0, interpret_done

    # Duplicate address for number path (addr -> addr addr)
    addi x28, x28, -4
    sw x4, 0(x28)

    # FIND on top address -> xt flag
    lui x7, %hi(xt_find)
    addi x7, x7, %lo(xt_find)
    lui x13, %hi(interpret_after_find)
    addi x13, x13, %lo(interpret_after_find)
    j interpret_run_xt

interpret_after_find:
    lw x6, 0(x28)           # flag
    lw x7, 4(x28)           # xt
    bne x7, x0, interpret_found

    # Not found: drop flag+xt to expose addr
    addi x28, x28, 8         # pop flag and xt
    lui x7, %hi(xt_number)
    addi x7, x7, %lo(xt_number)
    lui x13, %hi(interpret_after_number)
    addi x13, x13, %lo(interpret_after_number)
    j interpret_run_xt

interpret_after_number:
    lw x6, 0(x28)           # flag
    lw x7, 4(x28)           # n
    beq x6, x0, interpret_error

    # Number succeeded
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x9, 0(x8)
    beq x9, x0, interpret_number_interp

    # Compiling: compile LIT and literal
    addi x28, x28, 8         # drop n and flag
    lui x10, %hi(var_here)
    addi x10, x10, %lo(var_here)
    lw x11, 0(x10)          # HERE
    lui x12, %hi(xt_lit)
    addi x12, x12, %lo(xt_lit)
    sw x12, 0(x11)
    addi x11, x11, 4
    sw x7, 0(x11)
    addi x11, x11, 4
    sw x11, 0(x10)          # update HERE
    j interpret_loop

interpret_number_interp:
    addi x28, x28, 4         # drop flag, keep n
    j interpret_loop

interpret_found:
    # Stack: flag (TOS), xt
    addi x28, x28, 12        # drop flag, xt, and original c-addr
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x9, 0(x8)
    beq x9, x0, interpret_execute

    # Compiling
    bne x6, x0, interpret_found_immediate

    # Compile xt
    lui x10, %hi(var_here)
    addi x10, x10, %lo(var_here)
    lw x11, 0(x10)
    sw x7, 0(x11)
    addi x11, x11, 4
    sw x11, 0(x10)
    j interpret_loop

interpret_found_immediate:
    add x13, x0, x0          # resume = interpret_loop
    j interpret_run_xt

interpret_execute:
    add x13, x0, x0          # resume = interpret_loop
    j interpret_run_xt

interpret_run_xt:
    # x7 = XT to run
    # x13 = resume target (0 => interpret_loop)
    beq x13, x0, interpret_default_resume
    j interpret_resume_set
interpret_default_resume:
    lui x13, %hi(interpret_loop)
    addi x13, x13, %lo(interpret_loop)
interpret_resume_set:
    lui x12, %hi(interp_resume_target)
    addi x12, x12, %lo(interp_resume_target)
    sw x13, 0(x12)          # save resume target

    # Save caller IP so we can resume after executing XT
    lui x10, %hi(interp_saved_ip)
    addi x10, x10, %lo(interp_saved_ip)
    sw x26, 0(x10)

    # Build tiny thread: XT, interpret_resume
    lui x11, %hi(interp_exec_thread)
    addi x11, x11, %lo(interp_exec_thread)
    sw x7, 0(x11)           # thread[0] = XT

    add x26, x11, x0         # IP = &thread[0]
    j next             # run XT, then interpret_resume

interpret_resume:
    lui x1, %hi(interp_saved_ip)
    addi x1, x1, %lo(interp_saved_ip)
    lw x26, 0(x1)           # restore caller IP

    lui x2, %hi(interp_resume_target)
    addi x2, x2, %lo(interp_resume_target)
    lw x2, 0(x2)
    jr x2           # jump to resume target

interpret_error:
    addi x28, x28, 8         # drop n and flag
    # Print the offending word from PAD (counted string)
    la x11, pad
    lbu x12, 0(x11)          # x12 = length
    addi x11, x11, 1         # x11 = first char addr
    beq x12, x0, .Lie_suffix
    li x10, 1                # fd = stdout
    li x17, 64               # syscall = write
    ecall
.Lie_suffix:
    # Print " ?\n"
    li x10, 1
    la x11, str_error_suffix
    li x12, 3                # len = 3 (" ?\n")
    li x17, 64
    ecall
    j interpret_loop

interpret_done:
    addi x28, x28, 4         # drop c-addr when len==0
    j next

# Word: COMMA ( x -- )  store cell at HERE and advance
.text
    .p2align 2, 0
head_comma:
    .word head_interpret
    .byte 1
    .ascii ","
    .p2align 2, 0
xt_comma:
    .word comma_word
comma_word:
    lw x1, 0(x28)     # x
    addi x28, x28, 4   # pop
    lui x2, %hi(var_here)
    addi x2, x2, %lo(var_here)
    lw x3, 0(x2)      # HERE
    addi x4, x3, 4
    lui x5, %hi(user_dictionary_end)
    addi x5, x5, %lo(user_dictionary_end)
    bltu x5, x4, comma_overflow
    sw x1, 0(x3)
    sw x4, 0(x2)      # HERE += 4
    j next
comma_overflow:
    j abort_word

# Word: ALLOT ( n -- )  adjust HERE by n bytes
.text
    .p2align 2, 0
head_allot:
    .word head_comma
    .byte 5
    .ascii "ALLOT"
    .p2align 2, 0
xt_allot:
    .word allot_word
allot_word:
    lw x1, 0(x28)     # n
    addi x28, x28, 4
    lui x2, %hi(var_here)
    addi x2, x2, %lo(var_here)
    lw x3, 0(x2)
    add x3, x3, x1     # HERE += n
    lui x4, %hi(user_dictionary_end)
    addi x4, x4, %lo(user_dictionary_end)
    bltu x4, x3, allot_overflow
    sw x3, 0(x2)
    j next
allot_overflow:
    j abort_word

# Word: [ ( -- )  enter interpret state (IMMEDIATE)
.text
    .p2align 2, 0
head_lbrac:
    .word head_allot
    .byte 0x81
    .ascii "["
    .p2align 2, 0
xt_lbrac:
    .word lbrac_word
lbrac_word:
    lui x1, %hi(var_state)
    addi x1, x1, %lo(var_state)
    sw x0, 0(x1)      # STATE := 0
    j next

# Word: ] ( -- )  enter compile state
.text
    .p2align 2, 0
head_rbrac:
    .word head_lbrac
    .byte 1
    .ascii "]"
    .p2align 2, 0
xt_rbrac:
    .word rbrac_word
rbrac_word:
    lui x1, %hi(var_state)
    addi x1, x1, %lo(var_state)
    addi x2, x0, 1
    sw x2, 0(x1)      # STATE := 1
    j next

# Word: IMMEDIATE ( -- )  set IMMEDIATE bit on latest
.text
    .p2align 2, 0
head_immediate:
    .word head_rbrac
    .byte 0x89
    .ascii "IMMEDIATE"
    .p2align 2, 0
xt_immediate:
    .word immediate_word
immediate_word:
    lui x1, %hi(var_compilation_wid)
    addi x1, x1, %lo(var_compilation_wid)
    lw x1, 0(x1)      # x1 = wid (pointer to head cell)
    lw x2, 0(x1)      # latest header in compilation wordlist
    beq x2, x0, immediate_done
    addi x2, x2, 4     # len byte
    lbu x3, 0(x2)
    addi x4, x0, 0x80
    or x3, x3, x4
    sb x3, 0(x2)
immediate_done:
    j next

# Word: ; ( -- )  IMMEDIATE, finish current definition
.text
    .p2align 2, 0
head_semicolon:
    .word head_immediate
    .byte 0x81
    .ascii ";"
    .p2align 2, 0
xt_semicolon:
    .word semicolon_word
semicolon_word:
    # Compile EXIT
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)      # HERE
    lui x3, %hi(xt_exit)
    addi x3, x3, %lo(xt_exit)
    sw x3, 0(x2)
    addi x2, x2, 4
    sw x2, 0(x1)      # HERE += 4

    # STATE := 0
    lui x4, %hi(var_state)
    addi x4, x4, %lo(var_state)
    sw x0, 0(x4)
    j next

# Word: : ( -- )  IMMEDIATE, start a new colon definition
.text
    .p2align 2, 0
head_colon:
    .word head_semicolon
    .byte 0x81
    .ascii ":"
    .p2align 2, 0
xt_colon:
    .word colon_word
colon_word:
    # Parse next name directly (similar to WORD but no stack effect)
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)           # x1 = SOURCE base
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)           # #TIB
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)           # >IN

colon_skip:
    bge x4, x2, colon_done  # no name available
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    bne x6, x7, colon_found
    addi x4, x4, 1
    j colon_skip

colon_found:
    add x11, x1, x4         # start addr
    add x12, x4, x0         # start index

colon_scan:
    bge x4, x2, colon_end
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    beq x6, x7, colon_end
    addi x4, x4, 1
    j colon_scan

colon_end:
    sub x13, x4, x12        # len
    sw x4, 0(x3)           # update >IN
    beq x13, x0, colon_done

    # Build counted string in PAD (uppercased)
    lui x14, %hi(pad)
    addi x14, x14, %lo(pad)
    sb x13, 0(x14)
    addi x15, x14, 1        # dest pointer
    add x16, x11, x0        # src pointer
    add x17, x0, x0         # i = 0

colon_copy_loop:
    bge x17, x13, colon_copy_done
    lbu x18, 0(x16)
    addi x19, x0, 97        # 'a'
    blt x18, x19, colon_no_upper
    addi x19, x0, 122       # 'z'
    bgt x18, x19, colon_no_upper
    addi x18, x18, -32      # uppercase
colon_no_upper:
    sb x18, 0(x15)
    addi x15, x15, 1
    addi x16, x16, 1
    addi x17, x17, 1
    j colon_copy_loop

colon_copy_done:
    # Bounds check for header + codeword
    lui x20, %hi(var_here)
    addi x20, x20, %lo(var_here)
    lw x21, 0(x20)         # start HERE
    addi x22, x21, 4        # link cell
    addi x22, x22, 1        # length byte
    add x22, x22, x13       # name bytes
    addi x23, x22, 3
    addi x24, x0, -4
    and x22, x23, x24       # align to 4
    addi x22, x22, 4        # codeword
    lui x25, %hi(user_dictionary_end)
    addi x25, x25, %lo(user_dictionary_end)
    bltu x25, x22, colon_overflow

    # HERE pointer
    lui x3, %hi(var_here)
    addi x3, x3, %lo(var_here)
    lw x4, 0(x3)       # x4 = HERE
    add x12, x4, x0     # remember header start

    # Link — use compilation wordlist indirection
    lui x5, %hi(var_compilation_wid)
    addi x5, x5, %lo(var_compilation_wid)
    lw x5, 0(x5)       # x5 = wid (pointer to head cell)
    lw x6, 0(x5)       # previous head of compilation wordlist
    sw x6, 0(x4)
    addi x4, x4, 4

    # Length byte (mask off immediate bit)
    addi x7, x0, 0x7F
    and x2, x13, x7
    sb x2, 0(x4)
    addi x4, x4, 1

    # Copy name characters from PAD+1
    addi x8, x14, 1     # src = PAD+1
    add x9, x0, x0      # i = 0
colon_header_copy:
    bge x9, x13, colon_header_done
    lbu x10, 0(x8)
    sb x10, 0(x4)
    addi x4, x4, 1
    addi x8, x8, 1
    addi x9, x9, 1
    j colon_header_copy
colon_header_done:
    # Align to 4-byte boundary
    addi x10, x0, 3
    add x4, x4, x10
    addi x10, x0, -4
    and x4, x4, x10

    # Codeword = DOCOL
    lui x11, %hi(docol_word)
    addi x11, x11, %lo(docol_word)
    sw x11, 0(x4)
    addi x4, x4, 4

    # Update HERE and compilation wordlist head
    sw x4, 0(x3)       # HERE = body start
    sw x12, 0(x5)      # [wid] = new header

    # Enter compile state
    lui x13, %hi(var_state)
    addi x13, x13, %lo(var_state)
    addi x14, x0, 1
    sw x14, 0(x13)

colon_done:
    j next
colon_overflow:
    j abort_word

# Word: IF ( -- patch ) IMMEDIATE
.text
    .p2align 2, 0
head_if:
    .word head_colon
    .byte 0x82
    .ascii "IF"
    .p2align 2, 0
xt_if:
    .word if_word
if_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, if_done
    # HERE
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)          # x2 = HERE
    # Compile 0BRANCH XT
    lui x3, %hi(xt_0branch)
    addi x3, x3, %lo(xt_0branch)
    sw x3, 0(x2)
    addi x2, x2, 4
    # Placeholder
    sw x0, 0(x2)
    addi x2, x2, 4
    sw x2, 0(x1)          # HERE = x2
    # Push patch address (offset cell)
    addi x4, x2, -4
    addi x28, x28, -4
    sw x4, 0(x28)
if_done:
    j next

# Word: ELSE ( patch -- patch2 ) IMMEDIATE
.text
    .p2align 2, 0
head_else:
    .word head_if
    .byte 0x84
    .ascii "ELSE"
    .p2align 2, 0
xt_else:
    .word else_word
else_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, else_done
    lw x4, 0(x28)     # old patch addr
    addi x28, x28, 4

    # current HERE
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)

    # Compile BRANCH with placeholder
    lui x3, %hi(xt_branch)
    addi x3, x3, %lo(xt_branch)
    sw x3, 0(x2)
    addi x2, x2, 4
    sw x0, 0(x2)      # placeholder
    addi x2, x2, 4
    sw x2, 0(x1)      # update HERE

    # Patch old IF to jump here (after branch placeholder)
    addi x5, x2, 0     # target = new HERE
    addi x6, x4, 4
    sub x5, x5, x6     # offset
    sw x5, 0(x4)

    # Push new patch address (branch placeholder)
    addi x7, x2, -4
    addi x28, x28, -4
    sw x7, 0(x28)
else_done:
    j next

# Word: THEN ( patch -- ) IMMEDIATE
.text
    .p2align 2, 0
head_then:
    .word head_else
    .byte 0x84
    .ascii "THEN"
    .p2align 2, 0
xt_then:
    .word then_word
then_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, then_done
    lw x4, 0(x28)     # patch addr
    addi x28, x28, 4
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)      # target = HERE
    addi x3, x4, 4
    sub x2, x2, x3     # offset = target - (patch+4)
    sw x2, 0(x4)
then_done:
    j next

# Word: BEGIN ( -- addr ) IMMEDIATE
.text
    .p2align 2, 0
head_begin:
    .word head_then
    .byte 0x85
    .ascii "BEGIN"
    .p2align 2, 0
xt_begin:
    .word begin_word
begin_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, begin_done
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)
    addi x28, x28, -4
    sw x2, 0(x28)
begin_done:
    j next

# Word: AGAIN ( addr -- ) IMMEDIATE
.text
    .p2align 2, 0
head_again:
    .word head_begin
    .byte 0x85
    .ascii "AGAIN"
    .p2align 2, 0
xt_again:
    .word again_word
again_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, again_done
    lw x4, 0(x28)     # target addr (begin)
    addi x28, x28, 4
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)      # HERE
    lui x3, %hi(xt_branch)
    addi x3, x3, %lo(xt_branch)
    sw x3, 0(x2)
    addi x2, x2, 4
    # offset back
    addi x5, x2, 4     # patch addr +4
    sub x5, x4, x5     # offset = target - (patch+4)
    sw x5, 0(x2)
    addi x2, x2, 4
    sw x2, 0(x1)
again_done:
    j next

# Word: UNTIL ( addr -- ) IMMEDIATE
.text
    .p2align 2, 0
head_until:
    .word head_again
    .byte 0x85
    .ascii "UNTIL"
    .p2align 2, 0
xt_until:
    .word until_word
until_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, until_done
    lw x4, 0(x28)     # begin addr
    addi x28, x28, 4
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)      # HERE
    lui x3, %hi(xt_0branch)
    addi x3, x3, %lo(xt_0branch)
    sw x3, 0(x2)
    addi x2, x2, 4
    addi x5, x2, 4
    sub x5, x4, x5
    sw x5, 0(x2)
    addi x2, x2, 4
    sw x2, 0(x1)
until_done:
    j next

# Word: WHILE ( addr -- addr patch ) IMMEDIATE
.text
    .p2align 2, 0
head_while:
    .word head_until
    .byte 0x85
    .ascii "WHILE"
    .p2align 2, 0
xt_while:
    .word while_word
while_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, while_done
    lw x4, 0(x28)     # begin addr
    addi x28, x28, 4   # pop begin
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)      # HERE
    lui x3, %hi(xt_0branch)
    addi x3, x3, %lo(xt_0branch)
    sw x3, 0(x2)
    addi x2, x2, 4
    sw x0, 0(x2)      # placeholder
    addi x2, x2, 4
    sw x2, 0(x1)      # HERE update
    # push begin then patch addr (stack: begin patch)
    addi x28, x28, -4
    sw x4, 0(x28)     # begin
    addi x5, x2, -4
    addi x28, x28, -4
    sw x5, 0(x28)     # patch
while_done:
    j next

# Word: REPEAT ( begin patch -- ) IMMEDIATE
.text
    .p2align 2, 0
head_repeat:
    .word head_while
    .byte 0x86
    .ascii "REPEAT"
    .p2align 2, 0
xt_repeat:
    .word repeat_word
repeat_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, repeat_done
    lw x5, 0(x28)     # patch addr
    lw x4, 4(x28)     # begin addr
    addi x28, x28, 8
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)      # HERE
    # Compile BRANCH back to begin
    lui x3, %hi(xt_branch)
    addi x3, x3, %lo(xt_branch)
    sw x3, 0(x2)
    addi x2, x2, 4
    addi x6, x2, 4
    sub x6, x4, x6
    sw x6, 0(x2)
    addi x2, x2, 4
    sw x2, 0(x1)
    # Patch forward placeholder to HERE
    addi x7, x5, 4
    sub x7, x2, x7
    sw x7, 0(x5)
repeat_done:
    j next

# Word: XOR ( a b -- a^b )
.text
    .p2align 2, 0
head_xor:
    .word head_repeat
    .byte 3
    .ascii "XOR"
    .p2align 2, 0
xt_xor:
    .word xor_word
xor_word:
    lw x1, 0(x28)
    lw x2, 4(x28)
    xor x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: INVERT ( x -- ~x )
.text
    .p2align 2, 0
head_invert:
    .word head_xor
    .byte 6
    .ascii "INVERT"
    .p2align 2, 0
xt_invert:
    .word invert_word
invert_word:
    lw x1, 0(x28)
    addi x2, x0, -1    # x2 = 0xFFFFFFFF
    xor x1, x1, x2     # bitwise NOT (full 32-bit)
    sw x1, 0(x28)
    j next

# Word: 0= ( x -- flag )
.text
    .p2align 2, 0
head_zero_equal:
    .word head_invert
    .byte 2
    .ascii "0="
    .p2align 2, 0
xt_zero_equal:
    .word zero_equal_word
zero_equal_word:
    lw x1, 0(x28)
    addi x2, x0, 0
    beq x1, x0, zero_equal_set
    j zero_equal_push
zero_equal_set:
    addi x2, x0, 1
zero_equal_push:
    sw x2, 0(x28)
    j next

# Word: 0< ( x -- flag ) signed
.text
    .p2align 2, 0
head_zero_less:
    .word head_zero_equal
    .byte 2
    .ascii "0<"
    .p2align 2, 0
xt_zero_less:
    .word zero_less_word
zero_less_word:
    lw x1, 0(x28)
    addi x2, x0, 0
    blt x1, x0, zero_less_set
    j zero_less_push
zero_less_set:
    addi x2, x0, 1
zero_less_push:
    sw x2, 0(x28)
    j next

# Word: > ( a b -- flag ) signed
.text
    .p2align 2, 0
head_greater:
    .word head_zero_less
    .byte 1
    .ascii ">"
    .p2align 2, 0
xt_greater:
    .word greater_word
greater_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    addi x3, x0, 0
    blt x1, x2, greater_set
    j greater_push
greater_set:
    addi x3, x0, 1
greater_push:
    addi x28, x28, 4
    sw x3, 0(x28)
    j next

# Word: <> ( a b -- flag )
.text
    .p2align 2, 0
head_not_equals:
    .word head_greater
    .byte 2
    .ascii "<>"
    .p2align 2, 0
xt_not_equals:
    .word not_equals_word
not_equals_word:
    lw x1, 0(x28)
    lw x2, 4(x28)
    addi x3, x0, 1
    bne x1, x2, not_equals_push
    addi x3, x0, 0
not_equals_push:
    addi x28, x28, 4
    sw x3, 0(x28)
    j next

# Word: / ( a b -- a/b ) signed
.text
    .p2align 2, 0
head_div:
    .word head_not_equals
    .byte 1
    .ascii "/"
    .p2align 2, 0
xt_div:
    .word div_word
div_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    div x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: MOD ( a b -- a mod b )
.text
    .p2align 2, 0
head_mod:
    .word head_div
    .byte 3
    .ascii "MOD"
    .p2align 2, 0
xt_mod:
    .word mod_word
mod_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    rem x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: /MOD ( a b -- rem quot )
.text
    .p2align 2, 0
head_divmod:
    .word head_mod
    .byte 4
    .ascii "/MOD"
    .p2align 2, 0
xt_divmod:
    .word divmod_word
divmod_word:
    lw x1, 0(x28)     # b
    lw x2, 4(x28)     # a
    div x3, x2, x1     # quot
    rem x4, x2, x1     # rem
    sw x3, 0(x28)     # replace top with quot
    sw x4, 4(x28)     # second slot with rem
    j next

# Word: .S ( -- ) print stack contents without altering
.text
    .p2align 2, 0
head_dot_s:
    .word head_divmod
    .byte 2
    .ascii ".S"
    .p2align 2, 0
xt_dot_s:
    .word dot_s_word
dot_s_word:
    # Save original DSP
    add x21, x28, x0

    # Load dstack_top into a preserved register (dot_print_sub clobbers x1)
    lui x20, %hi(dstack_top)
    addi x20, x20, %lo(dstack_top)

    # Compute depth = (dstack_top - DSP)/4
    sub x2, x20, x21
    srai x2, x2, 2     # divide by 4

    # Print depth (stack will be restored by DOT)
    addi x28, x21, -4
    sw x2, 0(x28)
    jal dot_print_sub

    # Walk from top (current DSP) to dstack_top
    add x22, x21, x0   # cursor = DSP
dot_s_loop:
    bge x22, x20, dot_s_done
    lw x6, 0(x22)     # value
    addi x28, x21, -4  # temporary push at original DSP
    sw x6, 0(x28)
    jal dot_print_sub  # prints value, restores x28 to x21
    addi x22, x22, 4
    j dot_s_loop

dot_s_done:
    add x28, x21, x0   # ensure DSP unchanged
    j next

# Word: DEPTH ( -- n )
.text
    .p2align 2, 0
head_depth:
    .word head_dot_s
    .byte 5
    .ascii "DEPTH"
    .p2align 2, 0
xt_depth:
    .word depth_word
depth_word:
    lui x1, %hi(dstack_top)
    addi x1, x1, %lo(dstack_top)
    sub x2, x1, x28
    srai x2, x2, 2
    addi x28, x28, -4
    sw x2, 0(x28)
    j next

# Word: DSP@ ( -- addr ) return data stack pointer
.text
    .p2align 2, 0
head_dsp_fetch:
    .word head_depth
    .byte 4
    .ascii "DSP@"
    .p2align 2, 0
xt_dsp_fetch:
    .word dsp_fetch_word
dsp_fetch_word:
    add x1, x28, x0     # save DSP
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: DSTKTOP ( -- addr ) address of data stack top
.text
    .p2align 2, 0
head_dstack_top:
    .word head_dsp_fetch
    .byte 7
    .ascii "DSTKTOP"
    .p2align 2, 0
xt_dstack_top:
    .word dstack_top_word
dstack_top_word:
    lui x1, %hi(dstack_top)
    addi x1, x1, %lo(dstack_top)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: C! ( c addr -- ) store byte
.text
    .p2align 2, 0
head_cstore:
    .word head_dstack_top
    .byte 2
    .ascii "C!"
    .p2align 2, 0
xt_cstore:
    .word cstore_word
cstore_word:
    lw x1, 0(x28)     # addr
    lw x2, 4(x28)     # c
    addi x28, x28, 8   # pop both
    sb x2, 0(x1)      # *addr = c (byte)
    j next

# Word: LSHIFT ( x n -- x<<n )
.text
    .p2align 2, 0
head_lshift:
    .word head_cstore
    .byte 6
    .ascii "LSHIFT"
    .p2align 2, 0
xt_lshift:
    .word lshift_word
lshift_word:
    lw x1, 0(x28)     # n
    lw x2, 4(x28)     # x
    sll x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: RSHIFT ( x n -- x>>n ) logical
.text
    .p2align 2, 0
head_rshift:
    .word head_lshift
    .byte 6
    .ascii "RSHIFT"
    .p2align 2, 0
xt_rshift:
    .word rshift_word
rshift_word:
    lw x1, 0(x28)     # n
    lw x2, 4(x28)     # x
    srl x2, x2, x1
    addi x28, x28, 4
    sw x2, 0(x28)
    j next

# Word: NEGATE ( n -- -n )
.text
    .p2align 2, 0
head_negate:
    .word head_rshift
    .byte 6
    .ascii "NEGATE"
    .p2align 2, 0
xt_negate:
    .word negate_word
negate_word:
    lw x1, 0(x28)
    sub x1, x0, x1
    sw x1, 0(x28)
    j next

# Word: 1+ ( n -- n+1 )
.text
    .p2align 2, 0
head_one_plus:
    .word head_negate
    .byte 2
    .ascii "1+"
    .p2align 2, 0
xt_one_plus:
    .word one_plus_word
one_plus_word:
    lw x1, 0(x28)
    addi x1, x1, 1
    sw x1, 0(x28)
    j next

# Word: 1- ( n -- n-1 )
.text
    .p2align 2, 0
head_one_minus:
    .word head_one_plus
    .byte 2
    .ascii "1-"
    .p2align 2, 0
xt_one_minus:
    .word one_minus_word
one_minus_word:
    lw x1, 0(x28)
    addi x1, x1, -1
    sw x1, 0(x28)
    j next

# Word: PROMPTS-ON ( -- ) enable interactive prompts
.text
    .p2align 2, 0
head_prompts_on:
    .word head_one_minus
    .byte 10
    .ascii "PROMPTS-ON"
    .p2align 2, 0
xt_prompts_on:
    .word prompts_on_word
prompts_on_word:
    lui x1, %hi(var_prompt_enabled)
    addi x1, x1, %lo(var_prompt_enabled)
    addi x2, x0, 1
    sw x2, 0(x1)
    j next

# Word: (DO) ( limit start -- ) runtime for DO
# Word: (DOES>) (Internal) - runtime for DOES>
# Called during defining word execution. Patches LATEST word for DOES> behavior.
# IP points to the does-thread (the XTs after DOES> in the defining word).
.text
    .p2align 2, 0
xt_does_runtime:
    .word does_runtime_word
does_runtime_word:
    # Find latest word's XT address (via compilation wordlist)
    lui x1, %hi(var_compilation_wid)
    addi x1, x1, %lo(var_compilation_wid)
    lw x1, 0(x1)          # x1 = wid (pointer to head cell)
    lw x2, 0(x1)          # x2 = latest header in compilation wordlist
    lbu x3, 4(x2)         # length byte (with possible IMMEDIATE flag)
    addi x4, x0, 0x7F
    and x3, x3, x4         # mask off IMMEDIATE bit
    addi x3, x3, 5         # skip: link(4) + len(1) + name_len
    add x3, x2, x3         # past end of name
    addi x3, x3, 3         # align up
    addi x4, x0, -4
    and x3, x3, x4         # x3 = XT address
    # Patch code field to dodoes
    lui x4, %hi(dodoes)
    addi x4, x4, %lo(dodoes)
    sw x4, 0(x3)          # XT[0] = dodoes
    # Store does-thread address (current IP) into does-cell at XT+4
    sw x26, 4(x3)         # XT[4] = does-thread address
    # EXIT: pop IP from return stack (don't execute the does-thread now)
    lw x26, 0(x27)
    addi x27, x27, 4
    j next

# Word: (S") (Internal) - runtime for string literals
# Reads inline [length][string...padded] from IP, pushes ( c-addr u )
.text
    .p2align 2, 0
xt_sliteral:
    .word sliteral_word
sliteral_word:
    lw x1, 0(x26)         # x1 = length (from inline cell)
    addi x2, x26, 4        # x2 = string start (IP + 4)
    # Advance IP past length cell + padded string
    addi x3, x1, 3         # round up to 4-byte boundary
    addi x4, x0, -4
    and x3, x3, x4         # x3 = padded string length
    addi x26, x26, 4       # skip length cell
    add x26, x26, x3       # skip string data
    # Push ( c-addr u )
    addi x28, x28, -4
    sw x2, 0(x28)         # push c-addr
    addi x28, x28, -4
    sw x1, 0(x28)         # push u (length, TOS)
    j next

# Pushes limit then index onto return stack
.text
    .p2align 2, 0
head_do_runtime:
    .word head_prompts_on
    .byte 4
    .ascii "(DO)"
    .p2align 2, 0
xt_do_runtime:
    .word do_runtime_word
do_runtime_word:
    lw x1, 4(x28)         # x1 = limit (second on stack)
    lw x2, 0(x28)         # x2 = start/index (top of stack)
    addi x28, x28, 8       # pop both
    addi x27, x27, -4
    sw x1, 0(x27)         # push limit onto return stack
    addi x27, x27, -4
    sw x2, 0(x27)         # push index onto return stack
    j next

# Word: (LOOP) ( -- ) runtime for LOOP, inline offset follows
# Increments index by 1. If boundary crossed, exit loop.
.text
    .p2align 2, 0
head_loop_runtime:
    .word head_do_runtime
    .byte 6
    .ascii "(LOOP)"
    .p2align 2, 0
xt_loop_runtime:
    .word loop_runtime_word
loop_runtime_word:
    lw x1, 0(x27)         # x1 = index
    lw x2, 4(x27)         # x2 = limit
    sub x3, x1, x2         # x3 = old_index - limit
    addi x4, x1, 1         # x4 = new_index = index + 1
    sub x5, x4, x2         # x5 = new_index - limit
    xor x6, x3, x5         # sign-flip test
    slt x6, x6, x0         # x6 = 1 if sign bit set (boundary crossed)
    bne x6, x0, loop_exit
    # Continue loop: store new index, branch back
    sw x4, 0(x27)         # update index on return stack
    lw x1, 0(x26)         # load offset
    addi x26, x26, 4       # skip offset cell
    add x26, x26, x1       # IP += offset (branch back)
    j next
loop_exit:
    # Exit loop: drop loop params from return stack, skip offset
    addi x27, x27, 8       # drop index and limit
    addi x26, x26, 4       # skip offset cell
    j next

# Word: (+LOOP) ( n -- ) runtime for +LOOP, inline offset follows
# Adds n to index. If boundary crossed, exit loop.
.text
    .p2align 2, 0
head_ploop_runtime:
    .word head_loop_runtime
    .byte 7
    .ascii "(+LOOP)"
    .p2align 2, 0
xt_ploop_runtime:
    .word ploop_runtime_word
ploop_runtime_word:
    lw x7, 0(x28)         # x7 = increment n
    addi x28, x28, 4       # pop n
    lw x1, 0(x27)         # x1 = index
    lw x2, 4(x27)         # x2 = limit
    sub x3, x1, x2         # x3 = old_index - limit
    add x4, x1, x7         # x4 = new_index = index + n
    sub x5, x4, x2         # x5 = new_index - limit
    xor x6, x3, x5         # sign-flip test
    slt x6, x6, x0         # x6 = 1 if sign bit set (boundary crossed)
    bne x6, x0, ploop_exit
    # Continue loop: store new index, branch back
    sw x4, 0(x27)         # update index on return stack
    lw x1, 0(x26)         # load offset
    addi x26, x26, 4       # skip offset cell
    add x26, x26, x1       # IP += offset (branch back)
    j next
ploop_exit:
    # Exit loop: drop loop params from return stack, skip offset
    addi x27, x27, 8       # drop index and limit
    addi x26, x26, 4       # skip offset cell
    j next

# Word: I ( -- n ) push current loop index to data stack
.text
    .p2align 2, 0
head_i:
    .word head_ploop_runtime
    .byte 1
    .ascii "I"
    .p2align 2, 0
xt_i:
    .word i_word
i_word:
    lw x1, 0(x27)         # index is at RSP[0]
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: J ( -- n ) push outer loop index to data stack
.text
    .p2align 2, 0
head_j:
    .word head_i
    .byte 1
    .ascii "J"
    .p2align 2, 0
xt_j:
    .word j_word
j_word:
    lw x1, 8(x27)         # outer index is at RSP[8] (skip inner index + inner limit)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: UNLOOP ( -- ) drop loop params from return stack
.text
    .p2align 2, 0
head_unloop:
    .word head_j
    .byte 6
    .ascii "UNLOOP"
    .p2align 2, 0
xt_unloop:
    .word unloop_word
unloop_word:
    addi x27, x27, 8       # drop index and limit from return stack
    j next

# Word: DO ( -- addr ) IMMEDIATE compile-time
# Compiles (DO) xt, pushes HERE as loop-back target
.text
    .p2align 2, 0
head_do:
    .word head_unloop
    .byte 0x82
    .ascii "DO"
    .p2align 2, 0
xt_do:
    .word do_word
do_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, do_done
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)          # x2 = HERE
    lui x3, %hi(xt_do_runtime)
    addi x3, x3, %lo(xt_do_runtime)
    sw x3, 0(x2)          # compile (DO) xt
    addi x2, x2, 4
    sw x2, 0(x1)          # update HERE
    # Save current leave-list head, reset for this loop
    lui x4, %hi(var_leave_list)
    addi x4, x4, %lo(var_leave_list)
    lw x5, 0(x4)          # x5 = old leave-list head
    sw x0, 0(x4)          # var_leave_list = 0 (empty for new loop)
    addi x28, x28, -4
    sw x5, 0(x28)         # push old leave-list head
    addi x28, x28, -4
    sw x2, 0(x28)         # push HERE (loop-back target)
do_done:
    j next

# Word: LOOP ( addr -- ) IMMEDIATE compile-time
# Compiles (LOOP) xt + backward offset to loop-back target
.text
    .p2align 2, 0
head_loop:
    .word head_do
    .byte 0x84
    .ascii "LOOP"
    .p2align 2, 0
xt_loop:
    .word loop_word
loop_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, loop_done
    lw x4, 0(x28)         # x4 = loop-back target addr
    lw x10, 4(x28)        # x10 = old leave-list head (saved by DO)
    addi x28, x28, 8       # pop both
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)          # x2 = HERE
    lui x3, %hi(xt_loop_runtime)
    addi x3, x3, %lo(xt_loop_runtime)
    sw x3, 0(x2)          # compile (LOOP) xt
    addi x2, x2, 4         # x2 = offset cell addr
    addi x5, x2, 4         # x5 = offset cell addr + 4
    sub x5, x4, x5         # offset = target - (offset_cell + 4)
    sw x5, 0(x2)          # compile offset
    addi x2, x2, 4
    sw x2, 0(x1)          # update HERE; x2 = new HERE (past LOOP)
    # Patch all LEAVE forward branches from var_leave_list
    lui x8, %hi(var_leave_list)
    addi x8, x8, %lo(var_leave_list)
    lw x9, 0(x8)          # x9 = current leave-list head
loop_patch_leaves:
    beq x9, x0, loop_patch_done
    lw x5, 0(x9)          # x5 = next link from placeholder
    addi x6, x9, 4         # x6 = patch_addr + 4
    sub x6, x2, x6         # offset = HERE - (patch_addr + 4)
    sw x6, 0(x9)          # overwrite placeholder with real offset
    add x9, x5, x0         # advance to next
    j loop_patch_leaves
loop_patch_done:
    # Restore outer loop's leave-list
    sw x10, 0(x8)         # var_leave_list = old head
loop_done:
    j next

# Word: +LOOP ( addr -- ) IMMEDIATE compile-time
# Compiles (+LOOP) xt + backward offset to loop-back target
.text
    .p2align 2, 0
head_ploop:
    .word head_loop
    .byte 0x85
    .ascii "+LOOP"
    .p2align 2, 0
xt_ploop:
    .word ploop_word
ploop_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, ploop_done
    lw x4, 0(x28)         # x4 = loop-back target addr
    lw x10, 4(x28)        # x10 = old leave-list head (saved by DO)
    addi x28, x28, 8       # pop both
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)          # x2 = HERE
    lui x3, %hi(xt_ploop_runtime)
    addi x3, x3, %lo(xt_ploop_runtime)
    sw x3, 0(x2)          # compile (+LOOP) xt
    addi x2, x2, 4         # x2 = offset cell addr
    addi x5, x2, 4         # x5 = offset cell addr + 4
    sub x5, x4, x5         # offset = target - (offset_cell + 4)
    sw x5, 0(x2)          # compile offset
    addi x2, x2, 4
    sw x2, 0(x1)          # update HERE; x2 = new HERE (past +LOOP)
    # Patch all LEAVE forward branches from var_leave_list
    lui x8, %hi(var_leave_list)
    addi x8, x8, %lo(var_leave_list)
    lw x9, 0(x8)          # x9 = current leave-list head
ploop_patch_leaves:
    beq x9, x0, ploop_patch_done
    lw x5, 0(x9)          # x5 = next link from placeholder
    addi x6, x9, 4         # x6 = patch_addr + 4
    sub x6, x2, x6         # offset = HERE - (patch_addr + 4)
    sw x6, 0(x9)          # overwrite placeholder with real offset
    add x9, x5, x0         # advance to next
    j ploop_patch_leaves
ploop_patch_done:
    # Restore outer loop's leave-list
    sw x10, 0(x8)         # var_leave_list = old head
ploop_done:
    j next

# Word: LEAVE ( -- ) IMMEDIATE compile-time
# Compiles UNLOOP + BRANCH + placeholder offset, chains into leave-list
.text
    .p2align 2, 0
head_leave:
    .word head_ploop
    .byte 0x85
    .ascii "LEAVE"
    .p2align 2, 0
xt_leave:
    .word leave_word
leave_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, leave_done
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)          # x2 = HERE
    # Compile xt_unloop at HERE
    lui x3, %hi(xt_unloop)
    addi x3, x3, %lo(xt_unloop)
    sw x3, 0(x2)          # [HERE] = xt_unloop
    # Compile xt_branch at HERE+4
    lui x3, %hi(xt_branch)
    addi x3, x3, %lo(xt_branch)
    sw x3, 4(x2)          # [HERE+4] = xt_branch
    # HERE+8 = offset placeholder cell
    addi x4, x2, 8         # x4 = address of placeholder cell
    # Chain into var_leave_list: placeholder stores old head, variable gets new head
    lui x6, %hi(var_leave_list)
    addi x6, x6, %lo(var_leave_list)
    lw x5, 0(x6)          # x5 = old leave-list head
    sw x5, 0(x4)          # [placeholder] = old head (forward link)
    sw x4, 0(x6)          # var_leave_list = this placeholder addr
    # Update HERE past the 3 cells
    addi x2, x2, 12
    sw x2, 0(x1)          # update HERE
leave_done:
    j next

# Word: CREATE ( "name" -- ) parse name, build header with docreate
.text
    .p2align 2, 0
head_create:
    .word head_leave
    .byte 6
    .ascii "CREATE"
    .p2align 2, 0
xt_create:
    .word create_word
create_word:
    # Parse next name (same logic as colon_word)
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)
create_skip:
    bge x4, x2, create_done
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    bne x6, x7, create_found
    addi x4, x4, 1
    j create_skip
create_found:
    add x11, x1, x4
    add x12, x4, x0
create_scan:
    bge x4, x2, create_end
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    beq x6, x7, create_end
    addi x4, x4, 1
    j create_scan
create_end:
    sub x13, x4, x12
    sw x4, 0(x3)          # update >IN
    beq x13, x0, create_done
    # Build counted string in PAD (uppercased)
    lui x14, %hi(pad)
    addi x14, x14, %lo(pad)
    sb x13, 0(x14)
    addi x15, x14, 1
    add x16, x11, x0
    add x17, x0, x0
create_copy_loop:
    bge x17, x13, create_copy_done
    lbu x18, 0(x16)
    addi x19, x0, 97
    blt x18, x19, create_no_upper
    addi x19, x0, 122
    bgt x18, x19, create_no_upper
    addi x18, x18, -32
create_no_upper:
    sb x18, 0(x15)
    addi x15, x15, 1
    addi x16, x16, 1
    addi x17, x17, 1
    j create_copy_loop
create_copy_done:
    # Bounds check for header + codeword + does-cell
    lui x20, %hi(var_here)
    addi x20, x20, %lo(var_here)
    lw x21, 0(x20)         # start HERE
    addi x22, x21, 4        # link cell
    addi x22, x22, 1        # length byte
    add x22, x22, x13       # name bytes
    addi x23, x22, 3
    addi x24, x0, -4
    and x22, x23, x24       # align to 4
    addi x22, x22, 8        # codeword + does-cell
    lui x25, %hi(user_dictionary_end)
    addi x25, x25, %lo(user_dictionary_end)
    bltu x25, x22, create_overflow

    # HERE pointer
    lui x3, %hi(var_here)
    addi x3, x3, %lo(var_here)
    lw x4, 0(x3)
    add x12, x4, x0        # remember header start
    # Link — use compilation wordlist indirection
    lui x5, %hi(var_compilation_wid)
    addi x5, x5, %lo(var_compilation_wid)
    lw x5, 0(x5)          # x5 = wid (pointer to head cell)
    lw x6, 0(x5)          # previous head
    sw x6, 0(x4)
    addi x4, x4, 4
    # Length byte
    addi x7, x0, 0x7F
    and x2, x13, x7
    sb x2, 0(x4)
    addi x4, x4, 1
    # Copy name characters from PAD+1
    addi x8, x14, 1
    add x9, x0, x0
create_header_copy:
    bge x9, x13, create_header_done
    lbu x10, 0(x8)
    sb x10, 0(x4)
    addi x4, x4, 1
    addi x8, x8, 1
    addi x9, x9, 1
    j create_header_copy
create_header_done:
    # Align to 4-byte boundary
    addi x10, x0, 3
    add x4, x4, x10
    addi x10, x0, -4
    and x4, x4, x10
    # Codeword = docreate
    lui x11, %hi(docreate)
    addi x11, x11, %lo(docreate)
    sw x11, 0(x4)
    addi x4, x4, 4
    # Reserve does-cell (initialized to 0)
    sw x0, 0(x4)
    addi x4, x4, 4
    # Update HERE and compilation wordlist head (no compile state change)
    sw x4, 0(x3)          # HERE = after does-cell
    sw x12, 0(x5)         # [wid] = new header
create_done:
    j next
create_overflow:
    j abort_word

# Word: DOES> ( -- ) IMMEDIATE compile-time
# Compiles (DOES>) runtime token into current definition
.text
    .p2align 2, 0
head_does:
    .word head_create
    .byte 0x85
    .ascii "DOES>"
    .p2align 2, 0
xt_does:
    .word does_compile_word
does_compile_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, does_compile_done
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)          # x2 = HERE
    lui x3, %hi(xt_does_runtime)
    addi x3, x3, %lo(xt_does_runtime)
    sw x3, 0(x2)          # compile xt_does_runtime at HERE
    addi x2, x2, 4
    sw x2, 0(x1)          # update HERE
does_compile_done:
    j next

# Word: COUNT ( c-addr -- c-addr+1 u ) convert counted string to addr+len
.text
    .p2align 2, 0
head_count:
    .word head_does
    .byte 5
    .ascii "COUNT"
    .p2align 2, 0
xt_count:
    .word count_word
count_word:
    lw x1, 0(x28)         # c-addr
    lbu x2, 0(x1)         # length byte
    addi x1, x1, 1         # c-addr + 1
    sw x1, 0(x28)         # replace TOS with c-addr+1 (becomes second)
    addi x28, x28, -4
    sw x2, 0(x28)         # push length (new TOS)
    j next

# Word: S" ( -- c-addr u ) IMMEDIATE
# Compile mode: compiles xt_sliteral + length + string data (padded)
# Interpret mode: parses into transient buffer, pushes addr+len
.text
    .p2align 2, 0
head_squote:
    .word head_count
    .byte 0x82             # length 2 + IMMEDIATE flag (0x80)
    .ascii "S\""
    .p2align 2, 0
xt_squote:
    .word squote_word
squote_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, squote_interpret

    # === Compile mode ===
    # Compile xt_sliteral at HERE
    lui x14, %hi(var_here)
    addi x14, x14, %lo(var_here)
    lw x15, 0(x14)        # x15 = HERE
    lui x1, %hi(xt_sliteral)
    addi x1, x1, %lo(xt_sliteral)
    sw x1, 0(x15)         # compile xt_sliteral
    addi x15, x15, 4       # advance past xt
    add x16, x15, x0       # x16 = address where length will go (fill later)
    addi x15, x15, 4       # skip length cell (will fill after parsing)

    # Parse from SOURCE: skip one leading space, then copy chars until "
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)          # x2 = #TIB
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)          # x4 = >IN

    # Skip exactly one leading space (standard: S" <space>string")
    bge x4, x2, squote_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32        # space
    bne x6, x7, squote_parse_loop  # no leading space, start parsing
    addi x4, x4, 1         # skip the space

squote_parse_loop:
    bge x4, x2, squote_end_parse   # end of input
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 34        # '"' (ASCII 34)
    beq x6, x7, squote_found_quote
    # Copy char to HERE
    sb x6, 0(x15)
    addi x15, x15, 1
    addi x4, x4, 1
    j squote_parse_loop

squote_found_quote:
    addi x4, x4, 1         # skip past closing "

squote_end_parse:
    # Update >IN
    sw x4, 0(x3)

    # Calculate string length: x15 (current) - x16 (length cell) - 4
    sub x1, x15, x16
    addi x1, x1, -4        # x1 = string length
    sw x1, 0(x16)         # store length at the reserved cell

    # Pad to 4-byte alignment
    addi x2, x15, 3
    addi x3, x0, -4
    and x15, x2, x3        # x15 = aligned HERE

    # Update HERE
    sw x15, 0(x14)
    j next

    # === Interpret mode ===
squote_interpret:
    # Select transient buffer (alternating between two)
    lui x14, %hi(squote_iwhich)
    addi x14, x14, %lo(squote_iwhich)
    lw x8, 0(x14)
    addi x9, x0, 1
    xor x9, x8, x9         # toggle 0<->1
    sw x9, 0(x14)
    # Select buffer based on x8
    lui x15, %hi(squote_ibuf0)
    addi x15, x15, %lo(squote_ibuf0)
    beq x8, x0, squote_i_buf_ok
    lui x15, %hi(squote_ibuf1)
    addi x15, x15, %lo(squote_ibuf1)
squote_i_buf_ok:
    add x16, x15, x0       # x16 = buffer start

    # Parse from SOURCE
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)

    # Skip one leading space
    bge x4, x2, squote_i_end
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    bne x6, x7, squote_i_loop
    addi x4, x4, 1
squote_i_loop:
    bge x4, x2, squote_i_end
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 34        # '"'
    beq x6, x7, squote_i_quote
    sb x6, 0(x15)
    addi x15, x15, 1
    addi x4, x4, 1
    j squote_i_loop
squote_i_quote:
    addi x4, x4, 1         # skip past "
squote_i_end:
    sw x4, 0(x3)          # update >IN
    sub x1, x15, x16       # x1 = length
    # Push ( c-addr u )
    addi x28, x28, -4
    sw x16, 0(x28)        # push c-addr
    addi x28, x28, -4
    sw x1, 0(x28)         # push length
    j next

# Word: ." ( -- ) IMMEDIATE
# Compile mode: like S" but also compiles xt_type after the string data
# Interpret mode: parse and emit characters immediately
.text
    .p2align 2, 0
head_dotquote:
    .word head_squote
    .byte 0x82             # length 2 + IMMEDIATE flag (0x80)
    .ascii ".\""
    .p2align 2, 0
xt_dotquote:
    .word dotquote_word
dotquote_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, dotquote_interpret

    # === Compile mode ===
    # Compile xt_sliteral at HERE
    lui x14, %hi(var_here)
    addi x14, x14, %lo(var_here)
    lw x15, 0(x14)        # x15 = HERE
    lui x1, %hi(xt_sliteral)
    addi x1, x1, %lo(xt_sliteral)
    sw x1, 0(x15)         # compile xt_sliteral
    addi x15, x15, 4
    add x16, x15, x0       # x16 = length cell address
    addi x15, x15, 4       # skip length cell

    # Parse from SOURCE: skip one leading space, then copy chars until "
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)

    # Skip exactly one leading space
    bge x4, x2, dotquote_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    bne x6, x7, dotquote_parse_loop
    addi x4, x4, 1

dotquote_parse_loop:
    bge x4, x2, dotquote_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 34        # '"'
    beq x6, x7, dotquote_found_quote
    sb x6, 0(x15)
    addi x15, x15, 1
    addi x4, x4, 1
    j dotquote_parse_loop

dotquote_found_quote:
    addi x4, x4, 1

dotquote_end_parse:
    sw x4, 0(x3)          # update >IN

    # Calculate and store string length
    sub x1, x15, x16
    addi x1, x1, -4
    sw x1, 0(x16)

    # Pad to 4-byte alignment
    addi x2, x15, 3
    addi x3, x0, -4
    and x15, x2, x3

    # Also compile xt_type after the string data
    lui x1, %hi(xt_type)
    addi x1, x1, %lo(xt_type)
    sw x1, 0(x15)
    addi x15, x15, 4

    # Update HERE
    sw x15, 0(x14)
    j next

    # === Interpret mode: parse and emit immediately ===
dotquote_interpret:
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)
    lui x12, %hi(var_to_in)
    addi x12, x12, %lo(var_to_in)
    lw x4, 0(x12)         # x12 = ptr to >IN, x4 = >IN value
    # Skip one leading space
    bge x4, x2, dotquote_i_end
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32
    bne x6, x7, dotquote_i_loop
    addi x4, x4, 1
    # Scan for closing quote, then emit all at once
dotquote_i_loop:
    add x14, x1, x4        # x14 = string start address
    add x15, x0, x0        # x15 = string length
dotquote_i_scan:
    bge x4, x2, dotquote_i_emit
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 34        # '"'
    beq x6, x7, dotquote_i_found_q
    addi x15, x15, 1       # length++
    addi x4, x4, 1         # >IN++
    j dotquote_i_scan
dotquote_i_found_q:
    addi x4, x4, 1         # skip past closing "
dotquote_i_emit:
    mv x18, x4             # save final >IN value
    beq x15, x0, .Ldqi_done
    li x10, 1              # fd = stdout
    mv x11, x14            # addr
    mv x12, x15            # len
    li x17, 64             # syscall = write
    ecall
.Ldqi_done:
    la x12, var_to_in
    sw x18, 0(x12)         # update >IN
    j next
dotquote_i_end:
    sw x4, 0(x12)          # x12 = &var_to_in, x4 = current >IN
    j next

# Word: ' ( "name" -- xt )
# Parse the next word and return its execution token
.text
    .p2align 2, 0
head_tick:
    .word head_dotquote
    .byte 1
    .ascii "'"
    .p2align 2, 0
xt_tick:
    .word docol_word
    .word xt_word
    .word xt_find
    .word xt_drop
    .word xt_exit

# Word: ['] ( "name" -- ) IMMEDIATE compile-time
# At compile time: parse next word, find its XT, compile LIT <xt>
.text
    .p2align 2, 0
head_bracket_tick:
    .word head_tick
    .byte 0x83             # length 3 + IMMEDIATE flag (0x80)
    .ascii "[']"
    .p2align 2, 0
xt_bracket_tick:
    .word docol_word
    .word xt_word
    .word xt_find
    .word xt_drop
    .word xt_lit
    .word xt_lit           # pushes xt_lit itself as a literal
    .word xt_comma         # compile xt_lit at HERE
    .word xt_comma         # compile the XT at HERE
    .word xt_exit

# Word: CHAR ( "name" -- char )
# Parse the next word and push its first character (case-preserving)
.text
    .p2align 2, 0
head_char:
    .word head_bracket_tick
    .byte 4
    .ascii "CHAR"
    .p2align 2, 0
xt_char:
    .word docol_word
    .word xt_parse_word    # ( addr u ) - case-preserving, no uppercase
    .word xt_drop          # ( addr )
    .word xt_cfetch        # ( char )
    .word xt_exit

# Word: [CHAR] ( "name" -- ) IMMEDIATE compile-time
# At compile time: parse next word, get first char, compile LIT <char>
.text
    .p2align 2, 0
head_bracket_char:
    .word head_char
    .byte 0x86             # length 6 + IMMEDIATE flag (0x80)
    .ascii "[CHAR]"
    .p2align 2, 0
xt_bracket_char:
    .word docol_word
    .word xt_parse_word    # ( addr u ) - case-preserving
    .word xt_drop          # ( addr )
    .word xt_cfetch        # ( char )
    .word xt_lit
    .word xt_lit           # pushes xt_lit itself
    .word xt_comma         # compile xt_lit at HERE
    .word xt_comma         # compile the char value at HERE
    .word xt_exit

# Word: RECURSE ( -- ) IMMEDIATE compile-time
# Compile a call to the word currently being defined.
# Computes XT from LATEST header: aligned(header + 5 + (len & 0x7F))
.text
    .p2align 2, 0
head_recurse:
    .word head_bracket_char
    .byte 0x87             # length 7 + IMMEDIATE flag (0x80)
    .ascii "RECURSE"
    .p2align 2, 0
xt_recurse:
    .word docol_word
    .word xt_latest        # push &var_latest
    .word xt_fetch         # read header address
    .word xt_dup           # header header
    .word xt_lit
    .word 4
    .word xt_plus          # header (header+4)
    .word xt_cfetch        # header len_byte
    .word xt_lit
    .word 127
    .word xt_and           # header clean_len
    .word xt_plus          # (header + clean_len)
    .word xt_lit
    .word 8                # 5 (link+len) + 3 (alignment round-up)
    .word xt_plus          # (header + clean_len + 8)
    .word xt_lit
    .word -4               # 0xFFFFFFFC alignment mask
    .word xt_and           # aligned XT
    .word xt_comma         # compile XT at HERE
    .word xt_exit

# Word: POSTPONE ( "name" -- ) IMMEDIATE compile-time
# If next word is IMMEDIATE, compile its XT directly.
# If non-immediate, compile code that will compile it later: LIT <xt> ,
.text
    .p2align 2, 0
head_postpone:
    .word head_recurse
    .byte 0x88             # length 8 + IMMEDIATE flag (0x80)
    .ascii "POSTPONE"
    .p2align 2, 0
xt_postpone:
    .word docol_word
    .word xt_word          # parse next token
    .word xt_find          # ( xt flag )
    .word xt_0branch       # if flag=0 (not immediate), jump to else
    .word 12               # skip 3 cells to ELSE
    # IF (immediate word): just compile its XT
    .word xt_comma
    .word xt_branch
    .word 28               # skip 7 cells to EXIT
    # ELSE (non-immediate): compile LIT <xt> ,
    .word xt_lit
    .word xt_lit           # push xt_lit as literal
    .word xt_comma         # compile xt_lit at HERE
    .word xt_comma         # compile the XT at HERE
    .word xt_lit
    .word xt_comma         # push xt_comma as literal
    .word xt_comma         # compile xt_comma at HERE
    # THEN:
    .word xt_exit

# Word: C, ( char -- ) store byte at HERE, advance HERE by 1
.text
    .p2align 2, 0
head_ccomma:
    .word head_postpone
    .byte 2
    .ascii "C,"
    .p2align 2, 0
xt_ccomma:
    .word ccomma_word
ccomma_word:
    lw x1, 0(x28)         # char
    addi x28, x28, 4       # pop
    lui x2, %hi(var_here)
    addi x2, x2, %lo(var_here)
    lw x3, 0(x2)          # HERE
    addi x4, x3, 1
    lui x5, %hi(user_dictionary_end)
    addi x5, x5, %lo(user_dictionary_end)
    bltu x5, x4, ccomma_overflow
    sb x1, 0(x3)          # store byte
    sw x4, 0(x2)          # update HERE
    j next
ccomma_overflow:
    j abort_word

# Word: U< ( u1 u2 -- flag ) unsigned less-than
.text
    .p2align 2, 0
head_ult:
    .word head_ccomma
    .byte 2
    .ascii "U<"
    .p2align 2, 0
xt_ult:
    .word ult_word
ult_word:
    lw x1, 0(x28)         # b (TOS)
    lw x2, 4(x28)         # a (second)
    addi x3, x0, 0
    bltu x2, x1, ult_set
    j ult_push
ult_set:
    addi x3, x0, 1
ult_push:
    addi x28, x28, 4
    sw x3, 0(x28)
    j next

# Word: <# ( -- ) Begin pictured numeric output
.text
    .p2align 2, 0
head_less_sharp:
    .word head_ult
    .byte 2
    .ascii "<#"
    .p2align 2, 0
xt_less_sharp:
    .word less_sharp_word
less_sharp_word:
    lui x1, %hi(pad)
    addi x1, x1, %lo(pad)
    addi x1, x1, 128          # pad + 128 (one past end)
    lui x2, %hi(var_hld)
    addi x2, x2, %lo(var_hld)
    sw x1, 0(x2)             # HLD = pad + 128
    j next

# Word: HOLD ( char -- ) Prepend char to pictured output buffer
.text
    .p2align 2, 0
head_hold:
    .word head_less_sharp
    .byte 4
    .ascii "HOLD"
    .p2align 2, 0
xt_hold:
    .word hold_word
hold_word:
    lw x1, 0(x28)            # char
    addi x28, x28, 4          # pop
    lui x2, %hi(var_hld)
    addi x2, x2, %lo(var_hld)
    lw x3, 0(x2)             # HLD
    addi x3, x3, -1           # --HLD
    sb x1, 0(x3)             # *HLD = char
    sw x3, 0(x2)             # save updated HLD
    j next

# Word: #> ( ud -- addr len ) End pictured numeric output
.text
    .p2align 2, 0
head_sharp_greater:
    .word head_hold
    .byte 2
    .ascii "#>"
    .p2align 2, 0
xt_sharp_greater:
    .word sharp_greater_word
sharp_greater_word:
    lui x2, %hi(var_hld)
    addi x2, x2, %lo(var_hld)
    lw x3, 0(x2)             # HLD = start addr
    lui x4, %hi(pad)
    addi x4, x4, %lo(pad)
    addi x4, x4, 128          # pad + 128 = end
    sub x5, x4, x3            # len = end - HLD
    addi x28, x28, 4          # pop ud_hi (double-cell input)
    sw x3, 0(x28)            # replace ud_lo with addr
    addi x28, x28, -4         # push
    sw x5, 0(x28)            # TOS = len
    j next

# Word: PICK ( n -- x ) Copy n-th stack item (0=DUP, 1=OVER, etc.)
.text
    .p2align 2, 0
head_pick:
    .word head_sharp_greater
    .byte 4
    .ascii "PICK"
    .p2align 2, 0
xt_pick:
    .word pick_word
pick_word:
    lw x1, 0(x28)        # n
    addi x1, x1, 1         # n+1 (skip past n itself)
    add x1, x1, x1         # (n+1) * 2
    add x1, x1, x1         # (n+1) * 4
    add x1, x28, x1        # DSP + (n+1)*4
    lw x2, 0(x1)          # fetch item
    sw x2, 0(x28)         # replace TOS
    j next

# Word: 2! ( x1 x2 addr -- ) Store pair: x2 at addr, x1 at addr+4
.text
    .p2align 2, 0
head_two_store:
    .word head_pick
    .byte 2
    .ascii "2!"
    .p2align 2, 0
xt_two_store:
    .word two_store_word
two_store_word:
    lw x1, 0(x28)        # x1 = addr
    lw x2, 4(x28)        # x2 = x2
    lw x3, 8(x28)        # x3 = x1
    sw x2, 0(x1)         # *addr = x2
    sw x3, 4(x1)         # *(addr+4) = x1
    addi x28, x28, 12     # pop 3 items
    j next

# Word: 2@ ( addr -- x1 x2 ) Fetch pair: x1 from addr+4, x2 from addr
.text
    .p2align 2, 0
head_two_fetch:
    .word head_two_store
    .byte 2
    .ascii "2@"
    .p2align 2, 0
xt_two_fetch:
    .word two_fetch_word
two_fetch_word:
    lw x1, 0(x28)        # x1 = addr
    lw x2, 4(x1)         # x2 = x1 (addr+4)
    lw x3, 0(x1)         # x3 = x2 (addr)
    sw x2, 0(x28)        # replace TOS with x1
    addi x28, x28, -4
    sw x3, 0(x28)        # push x2
    j next

# Word: 2>R ( x1 x2 -- ) (R: -- x1 x2)
.text
    .p2align 2, 0
head_two_to_r:
    .word head_two_fetch
    .byte 3
    .ascii "2>R"
    .p2align 2, 0
xt_two_to_r:
    .word two_to_r_word
two_to_r_word:
    lw x1, 4(x28)        # x1 = x1
    lw x2, 0(x28)        # x2 = x2
    addi x28, x28, 8      # pop 2 from data stack
    addi x27, x27, -4
    sw x1, 0(x27)        # push x1 (deeper)
    addi x27, x27, -4
    sw x2, 0(x27)        # push x2 (on top)
    j next

# Word: 2R> ( -- x1 x2 ) (R: x1 x2 --)
.text
    .p2align 2, 0
head_two_r_from:
    .word head_two_to_r
    .byte 3
    .ascii "2R>"
    .p2align 2, 0
xt_two_r_from:
    .word two_r_from_word
two_r_from_word:
    lw x2, 0(x27)        # x2 = x2 (top of return stack)
    lw x1, 4(x27)        # x1 = x1 (deeper)
    addi x27, x27, 8      # pop 2 from return stack
    addi x28, x28, -4
    sw x1, 0(x28)        # push x1
    addi x28, x28, -4
    sw x2, 0(x28)        # push x2
    j next

# Word: 2R@ ( -- x1 x2 ) (R: x1 x2 -- x1 x2)
.text
    .p2align 2, 0
head_two_r_fetch:
    .word head_two_r_from
    .byte 3
    .ascii "2R@"
    .p2align 2, 0
xt_two_r_fetch:
    .word two_r_fetch_word
two_r_fetch_word:
    lw x2, 0(x27)        # x2 = x2
    lw x1, 4(x27)        # x1 = x1
    addi x28, x28, -4
    sw x1, 0(x28)        # push x1
    addi x28, x28, -4
    sw x2, 0(x28)        # push x2
    j next

# Word: S>D ( n -- d ) Sign-extend single to double
.text
    .p2align 2, 0
head_s_to_d:
    .word head_two_r_fetch
    .byte 3
    .ascii "S>D"
    .p2align 2, 0
xt_s_to_d:
    .word s_to_d_word
s_to_d_word:
    lw x1, 0(x28)        # n
    slt x2, x1, x0        # 1 if n<0, else 0
    sub x2, x0, x2        # 0 or -1 (sign extension)
    addi x28, x28, -4
    sw x2, 0(x28)        # push hi
    j next

# Word: D+ ( d1 d2 -- d3 ) Add doubles with carry
.text
    .p2align 2, 0
head_d_plus:
    .word head_s_to_d
    .byte 2
    .ascii "D+"
    .p2align 2, 0
xt_d_plus:
    .word d_plus_word
d_plus_word:
    lw x4, 0(x28)        # hi2
    lw x3, 4(x28)        # lo2
    lw x2, 8(x28)        # hi1
    lw x1, 12(x28)       # lo1
    add x5, x1, x3        # lo3 = lo1 + lo2
    sltu x6, x5, x1       # carry
    add x7, x2, x4        # hi1 + hi2
    add x7, x7, x6        # hi3 = hi1 + hi2 + carry
    addi x28, x28, 8      # pop 2 (4->2)
    sw x7, 0(x28)        # hi3
    sw x5, 4(x28)        # lo3
    j next

# Word: D- ( d1 d2 -- d3 ) Subtract doubles with borrow
.text
    .p2align 2, 0
head_d_minus:
    .word head_d_plus
    .byte 2
    .ascii "D-"
    .p2align 2, 0
xt_d_minus:
    .word d_minus_word
d_minus_word:
    lw x4, 0(x28)        # hi2
    lw x3, 4(x28)        # lo2
    lw x2, 8(x28)        # hi1
    lw x1, 12(x28)       # lo1
    sltu x6, x1, x3       # borrow = (lo1 < lo2)
    sub x5, x1, x3        # lo3 = lo1 - lo2
    sub x7, x2, x4        # hi1 - hi2
    sub x7, x7, x6        # hi3 = hi1 - hi2 - borrow
    addi x28, x28, 8
    sw x7, 0(x28)
    sw x5, 4(x28)
    j next

# Word: UM/MOD ( ud u -- rem quot ) Unsigned 64/32 division
.text
    .p2align 2, 0
head_um_mod:
    .word head_d_minus
    .byte 6
    .ascii "UM/MOD"
    .p2align 2, 0
xt_um_mod:
    .word um_mod_word
um_mod_word:
    lw x3, 0(x28)        # u (divisor)
    lw x2, 4(x28)        # ud_hi -> remainder
    lw x1, 8(x28)        # ud_lo -> quotient
    addi x4, x0, 32       # counter
    addi x6, x0, 31       # shift constant
um_mod_loop:
    srl x5, x1, x6        # carry = bit 31 of quotient
    add x1, x1, x1        # quotient <<= 1
    add x2, x2, x2        # remainder <<= 1
    add x2, x2, x5        # remainder += carry
    sltu x7, x2, x3       # x7 = (remainder < divisor)
    bne x7, x0, um_mod_skip
    sub x2, x2, x3        # remainder -= divisor
    addi x1, x1, 1        # set quotient bit
um_mod_skip:
    addi x4, x4, -1
    bne x4, x0, um_mod_loop
    addi x28, x28, 4      # pop 1 (3->2)
    sw x1, 0(x28)        # quotient
    sw x2, 4(x28)        # remainder
    j next

# Word: UM* ( u1 u2 -- ud ) Unsigned 32x32->64 multiply
.text
    .p2align 2, 0
head_um_star:
    .word head_um_mod
    .byte 3
    .ascii "UM*"
    .p2align 2, 0
xt_um_star:
    .word um_star_word
um_star_word:
    lw x2, 0(x28)        # u2
    lw x1, 4(x28)        # u1
    mul x3, x1, x2         # lo = low 32 bits
    mulhu x4, x1, x2       # hi = unsigned high
    sw x4, 0(x28)         # hi (TOS)
    sw x3, 4(x28)         # lo (NOS)
    j next

# Word: M* ( n1 n2 -- d ) Signed 32x32->64 multiply
.text
    .p2align 2, 0
head_m_star:
    .word head_um_star
    .byte 2
    .ascii "M*"
    .p2align 2, 0
xt_m_star:
    .word m_star_word
m_star_word:
    lw x2, 0(x28)        # n2
    lw x1, 4(x28)        # n1
    mul x3, x1, x2         # lo (same bits signed or unsigned)
    mulh x4, x1, x2        # hi (signed — MULH is signed on SLOW-32)
    sw x4, 0(x28)         # hi
    sw x3, 4(x28)         # lo
    j next

# Word: EVALUATE ( addr u -- ) Interpret a string
# Saves/restores >IN, SOURCE-PTR, #TIB, interp_saved_ip, interp_resume_target
.text
    .p2align 2, 0
head_evaluate:
    .word head_m_star
    .byte 8
    .ascii "EVALUATE"
    .p2align 2, 0
xt_evaluate:
    .word evaluate_word
evaluate_word:
    # Save interpreter state on return stack
    lui x1, %hi(interp_saved_ip)
    addi x1, x1, %lo(interp_saved_ip)
    lw x2, 0(x1)
    addi x27, x27, -4
    sw x2, 0(x27)             # push interp_saved_ip

    lui x1, %hi(interp_resume_target)
    addi x1, x1, %lo(interp_resume_target)
    lw x2, 0(x1)
    addi x27, x27, -4
    sw x2, 0(x27)             # push interp_resume_target

    lui x1, %hi(var_to_in)
    addi x1, x1, %lo(var_to_in)
    lw x2, 0(x1)
    addi x27, x27, -4
    sw x2, 0(x27)             # push >IN

    lui x1, %hi(var_source_len)
    addi x1, x1, %lo(var_source_len)
    lw x2, 0(x1)
    addi x27, x27, -4
    sw x2, 0(x27)             # push #TIB

    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x2, 0(x1)
    addi x27, x27, -4
    sw x2, 0(x27)             # push SOURCE-PTR

    # Pop ( addr u ) from data stack
    lw x9, 0(x28)             # u = length
    lw x8, 4(x28)             # addr = source
    addi x28, x28, 8

    # Set #TIB = u
    lui x1, %hi(var_source_len)
    addi x1, x1, %lo(var_source_len)
    sw x9, 0(x1)

    # Set SOURCE-PTR = addr
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    sw x8, 0(x1)

    # Set >IN = 0
    lui x1, %hi(var_to_in)
    addi x1, x1, %lo(var_to_in)
    sw x0, 0(x1)

    # Call INTERPRET (threaded)
    # Save current IP, set up mini-thread
    addi x27, x27, -4
    sw x26, 0(x27)            # push IP to return stack

    lui x26, %hi(evaluate_thread)
    addi x26, x26, %lo(evaluate_thread)
    j next

.text
evaluate_thread:
    .word xt_interpret
    .word xt_evaluate_resume

    .p2align 2, 0
xt_evaluate_resume:
    .word evaluate_resume
evaluate_resume:
    # Restore IP
    lw x26, 0(x27)
    addi x27, x27, 4

    # Restore SOURCE-PTR
    lw x2, 0(x27)
    addi x27, x27, 4
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    sw x2, 0(x1)

    # Restore #TIB
    lw x2, 0(x27)
    addi x27, x27, 4
    lui x1, %hi(var_source_len)
    addi x1, x1, %lo(var_source_len)
    sw x2, 0(x1)

    # Restore >IN
    lw x2, 0(x27)
    addi x27, x27, 4
    lui x1, %hi(var_to_in)
    addi x1, x1, %lo(var_to_in)
    sw x2, 0(x1)

    # Restore interp_resume_target
    lw x2, 0(x27)
    addi x27, x27, 4
    lui x1, %hi(interp_resume_target)
    addi x1, x1, %lo(interp_resume_target)
    sw x2, 0(x1)

    # Restore interp_saved_ip
    lw x2, 0(x27)
    addi x27, x27, 4
    lui x1, %hi(interp_saved_ip)
    addi x1, x1, %lo(interp_saved_ip)
    sw x2, 0(x1)

    j next

# Word: ABORT ( -- ) Reset stacks and state, jump to REPL
.text
    .p2align 2, 0
head_abort:
    .word head_evaluate
    .byte 5
    .ascii "ABORT"
    .p2align 2, 0
xt_abort:
    .word abort_word
abort_word:
    lui x28, %hi(dstack_top)
    addi x28, x28, %lo(dstack_top)
    lui x27, %hi(rstack_top)
    addi x27, x27, %lo(rstack_top)
    lui x1, %hi(var_state)
    addi x1, x1, %lo(var_state)
    sw x0, 0(x1)              # STATE = 0
    lui x1, %hi(var_catch_frame)
    addi x1, x1, %lo(var_catch_frame)
    sw x0, 0(x1)              # var_catch_frame = 0
    # Reset search order to defaults
    lui x1, %hi(var_latest)
    addi x1, x1, %lo(var_latest)
    lui x2, %hi(var_compilation_wid)
    addi x2, x2, %lo(var_compilation_wid)
    sw x1, 0(x2)              # var_compilation_wid = &var_latest
    lui x2, %hi(search_order)
    addi x2, x2, %lo(search_order)
    sw x1, 0(x2)              # search_order[0] = &var_latest
    lui x2, %hi(search_order_count)
    addi x2, x2, %lo(search_order_count)
    addi x3, x0, 1
    sw x3, 0(x2)              # search_order_count = 1
    lui x26, %hi(cold_loop)
    addi x26, x26, %lo(cold_loop)
    j next

# Word: (?DO) runtime ( limit start -- ) Conditional loop entry
# If limit == start, skip loop body (branch forward using offset).
# Otherwise push limit/start to return stack and skip the offset cell.
.text
    .p2align 2, 0
head_qdo_runtime:
    .word head_abort
    .byte 5
    .ascii "(?DO)"
    .p2align 2, 0
xt_qdo_runtime:
    .word qdo_runtime_word
qdo_runtime_word:
    lw x1, 4(x28)             # x1 = limit (NOS)
    lw x2, 0(x28)             # x2 = start (TOS)
    addi x28, x28, 8           # pop both
    beq x1, x2, qdo_skip
    # Enter loop: push to return stack
    addi x27, x27, -4
    sw x1, 0(x27)             # push limit
    addi x27, x27, -4
    sw x2, 0(x27)             # push index
    addi x26, x26, 4           # skip offset cell
    j next
qdo_skip:
    # Skip loop body: branch forward
    lw x1, 0(x26)             # load forward offset
    addi x26, x26, 4           # skip offset cell
    add x26, x26, x1           # IP += offset
    j next

# Word: ?DO ( limit start -- ) IMMEDIATE - Conditional DO loop
# Like DO but skips loop body if limit == start.
.text
    .p2align 2, 0
head_qdo_compile:
    .word head_qdo_runtime
    .byte 0x83                 # IMMEDIATE, length 3
    .ascii "?DO"
    .p2align 2, 0
xt_qdo_compile:
    .word qdo_compile_word
qdo_compile_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, qdo_compile_done  # If not compiling, skip

    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)              # x2 = HERE

    # Compile xt_qdo_runtime at HERE
    lui x3, %hi(xt_qdo_runtime)
    addi x3, x3, %lo(xt_qdo_runtime)
    sw x3, 0(x2)              # [HERE] = xt_qdo_runtime
    addi x2, x2, 4

    # Save current leave-list head, reset for this loop
    lui x4, %hi(var_leave_list)
    addi x4, x4, %lo(var_leave_list)
    lw x5, 0(x4)              # x5 = old leave-list head
    sw x0, 0(x4)              # var_leave_list = 0 (empty for new loop)

    # Compile offset placeholder at HERE (will be patched by LOOP/+LOOP)
    # Chain this placeholder into the leave-list so LOOP patches it
    addi x6, x2, 0             # x6 = address of placeholder cell
    sw x0, 0(x6)              # [placeholder] = 0 (no prior LEAVE)
    sw x6, 0(x4)              # var_leave_list = placeholder addr
    addi x2, x2, 4

    # Update HERE
    sw x2, 0(x1)

    # Push old leave-list head and HERE (loop-back target) on data stack
    addi x28, x28, -4
    sw x5, 0(x28)             # push old leave-list head
    addi x28, x28, -4
    sw x2, 0(x28)             # push HERE (loop-back target for LOOP)
qdo_compile_done:
    j next

# Word: PARSE ( char -- addr u ) Parse from >IN until delimiter
# Does NOT skip leading delimiters. Updates >IN past delimiter.
.text
    .p2align 2, 0
head_parse:
    .word head_qdo_compile
    .byte 5
    .ascii "PARSE"
    .p2align 2, 0
xt_parse:
    .word parse_impl_word
parse_impl_word:
    lw x8, 0(x28)             # x8 = delimiter char
    addi x28, x28, 4           # pop delimiter

    # Load SOURCE base
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)

    # Load #TIB
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)              # x2 = #TIB

    # Load >IN
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)              # x4 = >IN

    # x9 = start address (TIB + >IN)
    add x9, x1, x4
    # x10 = count
    add x10, x0, x0            # count = 0

parse_impl_scan:
    # Check if >IN + count >= #TIB
    add x5, x4, x10
    bge x5, x2, parse_impl_end_nodel
    # Load char at TIB[>IN + count]
    add x6, x1, x5
    lbu x7, 0(x6)
    beq x7, x8, parse_impl_end_found
    addi x10, x10, 1
    j parse_impl_scan

parse_impl_end_found:
    # Delimiter found: >IN = start_offset + count + 1 (skip delimiter)
    add x5, x4, x10
    addi x5, x5, 1
    sw x5, 0(x3)              # update >IN
    j parse_impl_push

parse_impl_end_nodel:
    # End of input, no delimiter: >IN = start_offset + count
    add x5, x4, x10
    sw x5, 0(x3)              # update >IN

parse_impl_push:
    # Push (addr count) on data stack
    addi x28, x28, -4
    sw x9, 0(x28)             # push start address
    addi x28, x28, -4
    sw x10, 0(x28)            # push count
    j next

# Word: 2/ ( n -- n/2 ) Arithmetic right shift by 1
.text
    .p2align 2, 0
head_two_div:
    .word head_parse
    .byte 2
    .ascii "2/"
    .p2align 2, 0
xt_two_div:
    .word two_div_word
two_div_word:
    lw x1, 0(x28)
    srai x1, x1, 1             # arithmetic right shift preserves sign
    sw x1, 0(x28)
    j next

# Word: SOURCE-ID ( -- a-addr )
.text
    .p2align 2, 0
head_source_id:
    .word head_two_div
    .byte 9
    .ascii "SOURCE-ID"
    .p2align 2, 0
xt_source_id:
    .word source_id_word
source_id_word:
    lui x1, %hi(var_source_id)
    addi x1, x1, %lo(var_source_id)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: MS ( n -- ) Sleep for n milliseconds
.text
    .p2align 2, 0
head_ms:
    .word head_source_id
    .byte 2
    .ascii "MS"
    .p2align 2, 0
xt_ms:
    .word ms_word
ms_word:
    # No sleep in rv32-run; just DROP
    addi x28, x28, 4   # pop n
    j next

# Word: QUIT ( -- ) Reset return stack and enter interpretation loop
.text
    .p2align 2, 0
head_quit:
    .word head_ms
    .byte 4
    .ascii "QUIT"
    .p2align 2, 0
xt_quit:
    .word quit_word
quit_word:
    # Reset return stack
    lui x27, %hi(rstack_top)
    addi x27, x27, %lo(rstack_top)
    # Reset state to 0 (interpreting)
    lui x1, %hi(var_state)
    addi x1, x1, %lo(var_state)
    sw x0, 0(x1)
    # Reset SOURCE-ID to 0 (Console)
    lui x1, %hi(var_source_id)
    addi x1, x1, %lo(var_source_id)
    sw x0, 0(x1)
    # Jump to cold_loop
    lui x26, %hi(cold_loop)
    addi x26, x26, %lo(cold_loop)
    j next

# Word: LIMIT ( -- a-addr )
.text
    .p2align 2, 0
head_limit:
    .word head_quit
    .byte 5
    .ascii "LIMIT"
    .p2align 2, 0
xt_limit:
    .word limit_word
limit_word:
    lui x1, %hi(user_dictionary_end)
    addi x1, x1, %lo(user_dictionary_end)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: BIN ( fam1 -- fam2 ) Standard: no-op for us as flags are already binary
.text
    .p2align 2, 0
head_bin:
    .word head_limit
    .byte 3
    .ascii "BIN"
    .p2align 2, 0
xt_bin:
    .word bin_word
bin_word:
    j next

# (File I/O words removed)

# Word: :NONAME ( -- xt ) Start anonymous colon definition
.text
    .p2align 2, 0
head_noname:
    .word head_bin
    .byte 7
    .ascii ":NONAME"
    .p2align 2, 0
xt_noname:
    .word noname_word
noname_word:
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)              # x2 = HERE = the XT we'll return
    addi x28, x28, -4
    sw x2, 0(x28)             # push XT onto data stack
    lui x3, %hi(docol_word)
    addi x3, x3, %lo(docol_word)
    sw x3, 0(x2)              # [HERE] = docol_word (codeword)
    addi x2, x2, 4
    sw x2, 0(x1)              # HERE += 4
    lui x4, %hi(var_state)
    addi x4, x4, %lo(var_state)
    addi x5, x0, 1
    sw x5, 0(x4)              # STATE = 1 (compile mode)
    j next

# Word: C" ( "string" -- ) IMMEDIATE - compile counted string literal
# Runtime: pushes c-addr where byte[0]=count, byte[1..n]=chars
.text
    .p2align 2, 0
xt_cliteral:
    .word cliteral_word
cliteral_word:
    add x1, x26, x0            # x1 = c-addr (points to count byte in thread)
    lbu x2, 0(x26)            # x2 = count
    addi x2, x2, 1             # +1 for count byte itself
    addi x2, x2, 3             # round up
    addi x3, x0, -4
    and x2, x2, x3             # padded total size
    add x26, x26, x2           # advance IP past inline data
    addi x28, x28, -4
    sw x1, 0(x28)             # push c-addr
    j next

.text
    .p2align 2, 0
head_cquote:
    .word head_noname
    .byte 0x82                 # IMMEDIATE flag (0x80) + length 2
    .ascii "C\""
    .p2align 2, 0
xt_cquote:
    .word cquote_word
cquote_word:
    # Only works in compile mode
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, cquote_done

    # Compile xt_cliteral at HERE
    lui x14, %hi(var_here)
    addi x14, x14, %lo(var_here)
    lw x15, 0(x14)            # x15 = HERE
    lui x1, %hi(xt_cliteral)
    addi x1, x1, %lo(xt_cliteral)
    sw x1, 0(x15)             # compile xt_cliteral
    addi x15, x15, 4           # advance past xt
    add x16, x15, x0           # x16 = address of count byte (fill later)
    addi x15, x15, 1           # skip past count byte, chars start here

    # Parse from SOURCE: skip one leading space, then copy chars until "
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)              # x2 = #TIB
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)              # x4 = >IN

    # Skip exactly one leading space
    bge x4, x2, cquote_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32            # space
    bne x6, x7, cquote_parse_loop
    addi x4, x4, 1             # skip the space

cquote_parse_loop:
    bge x4, x2, cquote_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 34            # '"'
    beq x6, x7, cquote_found_quote
    # Copy char to HERE
    sb x6, 0(x15)
    addi x15, x15, 1
    addi x4, x4, 1
    j cquote_parse_loop

cquote_found_quote:
    addi x4, x4, 1             # skip past closing "

cquote_end_parse:
    # Update >IN
    sw x4, 0(x3)

    # Calculate string length and store count byte
    sub x1, x15, x16
    addi x1, x1, -1            # x1 = char count (exclude count byte itself)
    sb x1, 0(x16)             # store count byte

    # Pad to 4-byte alignment (count byte + chars)
    # Total inline size = 1 + x1, round up to multiple of 4
    addi x2, x15, 3
    addi x5, x0, -4
    and x15, x2, x5            # x15 = aligned HERE

    # Update HERE
    sw x15, 0(x14)

cquote_done:
    j next

# Word: S\" ( "string" -- ) IMMEDIATE - compile string with escape sequences
# Uses xt_sliteral runtime (same as S"), but parses backslash escapes
.text
    .p2align 2, 0
head_squote_escape:
    .word head_cquote
    .byte 0x83                 # IMMEDIATE flag (0x80) + length 3
    .ascii "S\\\""             # S\"
    .p2align 2, 0
xt_squote_escape:
    .word squote_escape_word
squote_escape_word:
    lui x8, %hi(var_state)
    addi x8, x8, %lo(var_state)
    lw x8, 0(x8)
    beq x8, x0, se_interpret

    # === Compile mode (x12=0) ===
    add x12, x0, x0            # x12 = 0 = compile mode flag

    # Compile xt_sliteral at HERE
    lui x14, %hi(var_here)
    addi x14, x14, %lo(var_here)
    lw x15, 0(x14)            # x15 = HERE
    lui x1, %hi(xt_sliteral)
    addi x1, x1, %lo(xt_sliteral)
    sw x1, 0(x15)             # compile xt_sliteral
    addi x15, x15, 4           # advance past xt
    add x16, x15, x0           # x16 = address of length cell (fill later)
    addi x15, x15, 4           # skip length cell, chars start here
    j se_load_source

    # === Interpret mode (x12=1) ===
se_interpret:
    addi x12, x0, 1            # x12 = 1 = interpret mode flag
    # Select transient buffer (alternating)
    lui x14, %hi(squote_iwhich)
    addi x14, x14, %lo(squote_iwhich)
    lw x8, 0(x14)
    addi x9, x0, 1
    xor x9, x8, x9             # toggle 0<->1
    sw x9, 0(x14)
    lui x15, %hi(squote_ibuf0)
    addi x15, x15, %lo(squote_ibuf0)
    beq x8, x0, se_i_buf_ok
    lui x15, %hi(squote_ibuf1)
    addi x15, x15, %lo(squote_ibuf1)
se_i_buf_ok:
    add x16, x15, x0           # x16 = buffer start

se_load_source:
    # Parse from SOURCE: skip one leading space, then copy chars with escape processing
    lui x1, %hi(var_source_ptr)
    addi x1, x1, %lo(var_source_ptr)
    lw x1, 0(x1)
    lui x2, %hi(var_source_len)
    addi x2, x2, %lo(var_source_len)
    lw x2, 0(x2)              # x2 = #TIB
    lui x3, %hi(var_to_in)
    addi x3, x3, %lo(var_to_in)
    lw x4, 0(x3)              # x4 = >IN

    # Skip exactly one leading space
    bge x4, x2, se_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 32            # space
    bne x6, x7, se_parse_loop
    addi x4, x4, 1             # skip the space

se_parse_loop:
    bge x4, x2, se_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)
    addi x7, x0, 34            # '"'
    beq x6, x7, se_found_quote
    addi x7, x0, 92            # '\'
    beq x6, x7, se_escape
    # Normal char: copy to HERE
    sb x6, 0(x15)
    addi x15, x15, 1
    addi x4, x4, 1
    j se_parse_loop

se_escape:
    # Backslash found, read next char
    addi x4, x4, 1             # skip the backslash
    bge x4, x2, se_end_parse   # end of input after backslash
    add x5, x1, x4
    lbu x6, 0(x5)             # x6 = char after backslash
    addi x4, x4, 1             # consume it

    # Check each escape character
    addi x7, x0, 97            # 'a' -> BEL (7)
    beq x6, x7, se_esc_a
    addi x7, x0, 98            # 'b' -> BS (8)
    beq x6, x7, se_esc_b
    addi x7, x0, 101           # 'e' -> ESC (27)
    beq x6, x7, se_esc_e
    addi x7, x0, 102           # 'f' -> FF (12)
    beq x6, x7, se_esc_f
    addi x7, x0, 108           # 'l' -> LF (10)
    beq x6, x7, se_esc_n
    addi x7, x0, 110           # 'n' -> LF (10)
    beq x6, x7, se_esc_n
    addi x7, x0, 114           # 'r' -> CR (13)
    beq x6, x7, se_esc_r
    addi x7, x0, 116           # 't' -> TAB (9)
    beq x6, x7, se_esc_t
    addi x7, x0, 118           # 'v' -> VT (11)
    beq x6, x7, se_esc_v
    addi x7, x0, 122           # 'z' -> NUL (0)
    beq x6, x7, se_esc_z
    addi x7, x0, 92            # '\\' -> backslash (92)
    beq x6, x7, se_esc_lit
    addi x7, x0, 34            # '"' -> double quote (34)
    beq x6, x7, se_esc_lit
    addi x7, x0, 113           # 'q' -> double quote (34)
    beq x6, x7, se_esc_q
    addi x7, x0, 109           # 'm' -> CR+LF
    beq x6, x7, se_esc_m
    addi x7, x0, 120           # 'x' -> hex byte
    beq x6, x7, se_esc_x
    # Unknown escape: store the char literally
    j se_esc_lit

se_esc_a:
    addi x6, x0, 7
    j se_esc_lit
se_esc_b:
    addi x6, x0, 8
    j se_esc_lit
se_esc_e:
    addi x6, x0, 27
    j se_esc_lit
se_esc_f:
    addi x6, x0, 12
    j se_esc_lit
se_esc_n:
    addi x6, x0, 10
    j se_esc_lit
se_esc_r:
    addi x6, x0, 13
    j se_esc_lit
se_esc_t:
    addi x6, x0, 9
    j se_esc_lit
se_esc_v:
    addi x6, x0, 11
    j se_esc_lit
se_esc_z:
    addi x6, x0, 0
    j se_esc_lit
se_esc_q:
    addi x6, x0, 34
    j se_esc_lit

se_esc_m:
    # CR + LF (two bytes)
    addi x6, x0, 13
    sb x6, 0(x15)
    addi x15, x15, 1
    addi x6, x0, 10
    sb x6, 0(x15)
    addi x15, x15, 1
    j se_parse_loop

se_esc_x:
    # Read two hex digits
    bge x4, x2, se_end_parse
    add x5, x1, x4
    lbu x6, 0(x5)             # first hex digit
    addi x4, x4, 1
    # Convert first hex digit
    addi x7, x0, 48            # '0'
    blt x6, x7, se_hex_bad1
    addi x7, x0, 58            # '9'+1
    blt x6, x7, se_hex_digit1
    addi x7, x0, 65            # 'A'
    blt x6, x7, se_hex_bad1
    addi x7, x0, 71            # 'F'+1
    blt x6, x7, se_hex_upper1
    addi x7, x0, 97            # 'a'
    blt x6, x7, se_hex_bad1
    addi x7, x0, 103           # 'f'+1
    blt x6, x7, se_hex_lower1
se_hex_bad1:
    addi x8, x0, 0             # bad digit, treat as 0
    j se_hex_d2
se_hex_digit1:
    addi x8, x6, -48           # x8 = digit value
    j se_hex_d2
se_hex_upper1:
    addi x8, x6, -55           # 'A'-10 = 55
    j se_hex_d2
se_hex_lower1:
    addi x8, x6, -87           # 'a'-10 = 87

se_hex_d2:
    # x8 = high nibble
    slli x8, x8, 4             # shift to high nibble
    bge x4, x2, se_hex_store   # end of input, use what we have
    add x5, x1, x4
    lbu x6, 0(x5)             # second hex digit
    addi x4, x4, 1
    # Convert second hex digit
    addi x7, x0, 48
    blt x6, x7, se_hex_store
    addi x7, x0, 58
    blt x6, x7, se_hex_digit2
    addi x7, x0, 65
    blt x6, x7, se_hex_store
    addi x7, x0, 71
    blt x6, x7, se_hex_upper2
    addi x7, x0, 97
    blt x6, x7, se_hex_store
    addi x7, x0, 103
    blt x6, x7, se_hex_lower2
    j se_hex_store       # bad second digit
se_hex_digit2:
    addi x6, x6, -48
    or x8, x8, x6
    j se_hex_store
se_hex_upper2:
    addi x6, x6, -55
    or x8, x8, x6
    j se_hex_store
se_hex_lower2:
    addi x6, x6, -87
    or x8, x8, x6

se_hex_store:
    add x6, x8, x0             # x6 = byte value
    j se_esc_lit         # store it

se_esc_lit:
    # Store x6 at HERE and continue
    sb x6, 0(x15)
    addi x15, x15, 1
    j se_parse_loop

se_found_quote:
    addi x4, x4, 1             # skip past closing "

se_end_parse:
    # Update >IN
    sw x4, 0(x3)

    # Branch on mode flag (x12: 0=compile, 1=interpret)
    bne x12, x0, se_interpret_finish

    # === Compile mode finalization ===
    # Calculate string length and store in length cell
    sub x1, x15, x16
    addi x1, x1, -4            # x1 = string length (subtract length cell)
    sw x1, 0(x16)             # store length at the reserved cell

    # Pad to 4-byte alignment
    addi x2, x15, 3
    addi x5, x0, -4
    and x15, x2, x5            # x15 = aligned HERE

    # Update HERE
    sw x15, 0(x14)
    j next

    # === Interpret mode finalization ===
se_interpret_finish:
    sub x1, x15, x16           # x1 = length
    # Push ( c-addr u )
    addi x28, x28, -4
    sw x16, 0(x28)            # push c-addr
    addi x28, x28, -4
    sw x1, 0(x28)             # push length
    j next

# Word: CATCH ( i*x xt -- j*x 0 | i*x n )
# Save exception frame, EXECUTE xt. If xt returns normally, push 0.
# If THROW fires during xt, restore stacks and push throw code n.
.text
    .p2align 2, 0
head_catch:
    .word head_squote_escape
    .byte 5
    .ascii "CATCH"
    .p2align 2, 0
xt_catch:
    .word catch_word
catch_word:
    # Pop xt from data stack
    lw x25, 0(x28)            # x25 = xt (W register)
    addi x28, x28, 4           # DSP++

    # Push exception frame onto return stack:
    #   [RSP+8] = old var_catch_frame
    #   [RSP+4] = saved DSP
    #   [RSP+0] = saved IP (caller's continuation)
    lui x1, %hi(var_catch_frame)
    addi x1, x1, %lo(var_catch_frame)
    lw x2, 0(x1)              # x2 = old catch frame

    addi x27, x27, -4
    sw x2, 0(x27)             # push old_catch_frame
    addi x27, x27, -4
    sw x28, 0(x27)            # push DSP
    addi x27, x27, -4
    sw x26, 0(x27)            # push IP (caller continuation)

    # Set var_catch_frame = RSP (points to saved-IP cell)
    sw x27, 0(x1)             # var_catch_frame = RSP

    # Set IP to catch_resume_thread so that when DOCOL saves IP,
    # EXIT will return here to clean up the exception frame.
    lui x26, %hi(catch_resume_thread)
    addi x26, x26, %lo(catch_resume_thread)

    # EXECUTE the xt: jump into its code pointer
    lw x24, 0(x25)            # code pointer = *xt
    jr x24            # jump to word's code

.text
catch_resume_thread:
    .word xt_catch_resume

    .p2align 2, 0
xt_catch_resume:
    .word catch_resume_word
catch_resume_word:
    # xt returned normally — pop exception frame
    lw x2, 0(x27)             # pop saved IP
    addi x27, x27, 4
    lw x3, 0(x27)             # pop saved DSP (discard — stack is fine)
    addi x27, x27, 4
    lw x4, 0(x27)             # pop old_catch_frame
    addi x27, x27, 4

    # Restore var_catch_frame
    lui x1, %hi(var_catch_frame)
    addi x1, x1, %lo(var_catch_frame)
    sw x4, 0(x1)              # var_catch_frame = old value

    # Restore IP to caller's continuation
    add x26, x2, x0

    # Push 0 (no exception)
    addi x28, x28, -4
    sw x0, 0(x28)             # push 0

    j next

# Word: THROW ( k*x n -- k*x | i*x n )
# If n=0, drop and continue. If n!=0 and a CATCH frame exists, unwind.
# If no frame, ABORT.
.text
    .p2align 2, 0
head_throw:
    .word head_catch
    .byte 5
    .ascii "THROW"
    .p2align 2, 0
xt_throw:
    .word throw_word
throw_word:
    lw x1, 0(x28)             # x1 = n (throw code)
    addi x28, x28, 4           # pop n

    # If n=0, just continue
    beq x1, x0, throw_zero

    # Check for catch frame
    lui x2, %hi(var_catch_frame)
    addi x2, x2, %lo(var_catch_frame)
    lw x3, 0(x2)              # x3 = var_catch_frame
    beq x3, x0, throw_no_catch

    # Unwind to catch frame
    add x27, x3, x0            # RSP = var_catch_frame

    # Pop exception frame
    lw x26, 0(x27)            # IP = saved IP
    addi x27, x27, 4
    lw x28, 0(x27)            # DSP = saved DSP
    addi x27, x27, 4
    lw x4, 0(x27)             # old_catch_frame
    addi x27, x27, 4

    # Restore var_catch_frame
    sw x4, 0(x2)              # var_catch_frame = old value

    # Push throw code onto restored data stack
    addi x28, x28, -4
    sw x1, 0(x28)             # push n

    j next

throw_zero:
    j next

throw_no_catch:
    # No catch frame — ABORT
    j abort_word

# ======================================================================
# Search-Order Word Set
# ======================================================================

# Word: FORTH-WORDLIST ( -- wid )
# Returns the wid for the standard FORTH wordlist (= &var_latest)
.text
    .p2align 2, 0
head_forth_wl:
    .word head_throw
    .byte 14
    .ascii "FORTH-WORDLIST"
    .p2align 2, 0
xt_forth_wl:
    .word forth_wl_word
forth_wl_word:
    lui x1, %hi(var_latest)
    addi x1, x1, %lo(var_latest)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: GET-CURRENT ( -- wid )
# Return the compilation wordlist identifier
.text
    .p2align 2, 0
head_get_current:
    .word head_forth_wl
    .byte 11
    .ascii "GET-CURRENT"
    .p2align 2, 0
xt_get_current:
    .word get_current_word
get_current_word:
    lui x1, %hi(var_compilation_wid)
    addi x1, x1, %lo(var_compilation_wid)
    lw x1, 0(x1)
    addi x28, x28, -4
    sw x1, 0(x28)
    j next

# Word: SET-CURRENT ( wid -- )
# Set the compilation wordlist
.text
    .p2align 2, 0
head_set_current:
    .word head_get_current
    .byte 11
    .ascii "SET-CURRENT"
    .p2align 2, 0
xt_set_current:
    .word set_current_word
set_current_word:
    lw x1, 0(x28)
    addi x28, x28, 4
    lui x2, %hi(var_compilation_wid)
    addi x2, x2, %lo(var_compilation_wid)
    sw x1, 0(x2)
    j next

# Word: GET-ORDER ( -- widn ... wid1 n )
# Return the current search order
.text
    .p2align 2, 0
head_get_order:
    .word head_set_current
    .byte 9
    .ascii "GET-ORDER"
    .p2align 2, 0
xt_get_order:
    .word get_order_word
get_order_word:
    lui x1, %hi(search_order)
    addi x1, x1, %lo(search_order)
    lui x2, %hi(search_order_count)
    addi x2, x2, %lo(search_order_count)
    lw x2, 0(x2)          # x2 = count
    # Push entries in reverse order: search_order[count-1] first, [0] last
    add x3, x2, x0         # x3 = i = count
get_order_loop:
    beq x3, x0, get_order_done
    addi x3, x3, -1
    add x4, x3, x3
    add x4, x4, x4         # x4 = i * 4
    add x4, x1, x4         # x4 = &search_order[i]
    lw x5, 0(x4)          # x5 = wid
    addi x28, x28, -4
    sw x5, 0(x28)
    j get_order_loop
get_order_done:
    # Push count
    addi x28, x28, -4
    sw x2, 0(x28)
    j next

# Word: SET-ORDER ( widn ... wid1 n -- )
# Set the search order. If n = -1, set default (FORTH-WORDLIST only).
.text
    .p2align 2, 0
head_set_order:
    .word head_get_order
    .byte 9
    .ascii "SET-ORDER"
    .p2align 2, 0
xt_set_order:
    .word set_order_word
set_order_word:
    lw x1, 0(x28)         # x1 = n
    addi x28, x28, 4
    # Check for n = -1 (minimum search order)
    addi x2, x0, -1
    bne x1, x2, set_order_normal
    # n = -1: set default order [FORTH-WORDLIST]
    lui x2, %hi(search_order)
    addi x2, x2, %lo(search_order)
    lui x3, %hi(var_latest)
    addi x3, x3, %lo(var_latest)
    sw x3, 0(x2)          # search_order[0] = &var_latest
    lui x2, %hi(search_order_count)
    addi x2, x2, %lo(search_order_count)
    addi x3, x0, 1
    sw x3, 0(x2)          # count = 1
    j next
set_order_normal:
    # Pop n wids from stack into search_order[0..n-1]
    # Stack has: wid1 (top) ... widn (deepest). wid1 goes to [0].
    lui x2, %hi(search_order)
    addi x2, x2, %lo(search_order)
    add x3, x0, x0         # i = 0
set_order_loop:
    bge x3, x1, set_order_done
    lw x4, 0(x28)         # pop wid
    addi x28, x28, 4
    add x5, x3, x3
    add x5, x5, x5         # i * 4
    add x5, x2, x5
    sw x4, 0(x5)          # search_order[i] = wid
    addi x3, x3, 1
    j set_order_loop
set_order_done:
    lui x2, %hi(search_order_count)
    addi x2, x2, %lo(search_order_count)
    sw x1, 0(x2)          # count = n
    j next

# Word: SEARCH-WORDLIST ( c-addr u wid -- 0 | xt 1 | xt -1 )
# Search a single wordlist for name c-addr/u
.text
    .p2align 2, 0
head_search_wl:
    .word head_set_order
    .byte 15
    .ascii "SEARCH-WORDLIST"
    .p2align 2, 0
xt_search_wl:
    .word search_wl_word
search_wl_word:
    lw x1, 0(x28)         # x1 = wid
    lw x2, 4(x28)         # x2 = u (length)
    lw x3, 8(x28)         # x3 = c-addr
    addi x28, x28, 12      # pop 3 items

    lw x5, 0(x1)          # x5 = head of wordlist

search_wl_loop:
    beq x5, x0, search_wl_fail

    # Compare length (mask out IMMEDIATE bit)
    lbu x6, 4(x5)         # dict len/flags byte
    addi x7, x0, 0x7F
    and x8, x6, x7         # dict_len = len & 0x7F
    bne x8, x2, search_wl_next

    # Compare bytes
    add x9, x0, x0         # i = 0
search_wl_str_loop:
    bge x9, x8, search_wl_match

    add x10, x5, x9
    lbu x10, 5(x10)       # dict char (offset 5 + i, already uppercase)

    add x11, x3, x9
    lbu x11, 0(x11)       # search char
    # Uppercase the search char for case-insensitive compare
    addi x12, x0, 97       # 'a'
    blt x11, x12, search_wl_no_upper
    addi x12, x0, 122      # 'z'
    bgt x11, x12, search_wl_no_upper
    addi x11, x11, -32
search_wl_no_upper:

    bne x10, x11, search_wl_next

    addi x9, x9, 1
    j search_wl_str_loop

search_wl_next:
    lw x5, 0(x5)          # follow link
    j search_wl_loop

search_wl_match:
    # Calculate XT address
    addi x5, x5, 5
    add x5, x5, x8
    addi x5, x5, 3
    addi x10, x0, -4
    and x5, x5, x10        # x5 = XT (aligned)

    # Push xt
    addi x28, x28, -4
    sw x5, 0(x28)

    # Determine flag: 1 if IMMEDIATE, -1 if normal
    addi x10, x0, 0x80
    and x6, x6, x10
    beq x6, x0, search_wl_not_imm
    addi x6, x0, 1         # IMMEDIATE
    j search_wl_push_flag
search_wl_not_imm:
    addi x6, x0, -1        # normal (non-immediate)
search_wl_push_flag:
    addi x28, x28, -4
    sw x6, 0(x28)
    j next

search_wl_fail:
    addi x28, x28, -4
    sw x0, 0(x28)         # push 0 (not found)
    j next

# Word: WORDLIST ( -- wid )
# Create a new empty wordlist. Allocates a cell at HERE, inits to 0.
.text
    .p2align 2, 0
head_wordlist:
    .word head_search_wl
    .byte 8
    .ascii "WORDLIST"
    .p2align 2, 0
xt_wordlist:
    .word wordlist_word
wordlist_word:
    lui x1, %hi(var_here)
    addi x1, x1, %lo(var_here)
    lw x2, 0(x1)          # x2 = HERE
    sw x0, 0(x2)          # [HERE] = 0 (empty wordlist)
    addi x3, x2, 4
    sw x3, 0(x1)          # HERE += 4
    addi x28, x28, -4
    sw x2, 0(x28)         # push wid (= old HERE)
    j next

# Word: MOVE ( src dest u -- ) Copy u bytes; handles overlapping regions
.text
    .p2align 2, 0
head_move:
    .word head_wordlist
    .byte 4
    .ascii "MOVE"
    .p2align 2, 0
xt_move:
    .word move_word
move_word:
    lw x5, 0(x28)        # x5 = u (count)
    lw x3, 4(x28)        # x3 = dest
    lw x4, 8(x28)        # x4 = src
    addi x28, x28, 12     # pop 3 items
    beq x5, x0, .Lmove_done
    # Byte-by-byte copy handling overlap
    bltu x3, x4, .Lmove_fwd  # if dest < src, copy forward
    # Copy backward (dest >= src)
    add x4, x4, x5       # src_end
    add x3, x3, x5       # dest_end
.Lmove_bwd:
    addi x4, x4, -1
    addi x3, x3, -1
    lbu x1, 0(x4)
    sb x1, 0(x3)
    addi x5, x5, -1
    bne x5, x0, .Lmove_bwd
    j .Lmove_done
.Lmove_fwd:
    lbu x1, 0(x4)
    sb x1, 0(x3)
    addi x4, x4, 1
    addi x3, x3, 1
    addi x5, x5, -1
    bne x5, x0, .Lmove_fwd
.Lmove_done:
    j next

# ----------------------------------------------------------------------
# Variables
# ----------------------------------------------------------------------
.data
    .p2align 2, 0
var_state:      .word 0
var_base:       .word 10
var_here:       .word user_dictionary
var_latest:
    .word head_move                # Point to last defined word
var_to_in:      .word 0
var_source_id:  .word 0            # 0 = Console
var_source_ptr: .word tib
var_source_len: .word 0
var_prompt_enabled: .word 0        # 0 = suppress prompts (prelude), 1 = show prompts
var_leave_list:     .word 0        # compile-time leave-list head for DO...LOOP
var_hld:            .word 0            # Pictured numeric output pointer into PAD
var_catch_frame:    .word 0            # Exception frame pointer (0 = none)
var_compilation_wid: .word var_latest   # Current compilation wordlist (initially FORTH-WORDLIST)
search_order_count: .word 1            # Number of active search order entries
search_order:       .word var_latest   # Search order slot 0 (FORTH-WORDLIST)
                    .word 0            # Search order slot 1
                    .word 0            # Search order slot 2
                    .word 0            # Search order slot 3
                    .word 0            # Search order slot 4
                    .word 0            # Search order slot 5
                    .word 0            # Search order slot 6
                    .word 0            # Search order slot 7

    .p2align 2, 0
interp_exec_thread:
    .word 0                  # Placeholder for XT
    .word interp_resume_xt

interp_saved_ip: .word 0     # Saved caller IP for interpreter dispatch
interp_resume_target: .word interpret_loop

interp_resume_xt:
    .word interpret_resume

# Zero-filled buffers go in .bss to avoid bloating the binary
.bss
    .p2align 2, 0
tib:            .space 256         # Terminal Input Buffer
user_dictionary: .space 2097152    # 2MB space for new words
user_dictionary_end:
pad:            .space 128         # Scratch pad for strings/numbers
squote_ibuf0:    .space 256        # S" interpret-mode transient buffer 0
squote_ibuf1:    .space 256        # S" interpret-mode transient buffer 1
squote_iwhich:   .space 4          # Toggle: which transient buffer to use next


# ----------------------------------------------------------------------
# Boot Program
# ----------------------------------------------------------------------
.data
str_prompt:
    .ascii "ok> "
str_banner:
    .ascii "RV32IM Forth\n"
emit_buf:
    .byte 0
key_buf:
    .byte 0
    .balign 4
newline_buf:
    .byte 10
    .balign 4
str_error_suffix:
    .ascii " ?\n"
    .balign 4

.text
cold_start_body:
    # Initialize BASE to 10
    .word xt_lit
    .word 10
    .word xt_base
    .word xt_store

    # Print banner
    .word xt_lit
    .word str_banner
    .word xt_lit
    .word 13
    .word xt_type

    # REPL loop
cold_loop:
    # Conditional prompt: check var_prompt_enabled
    .word xt_lit
    .word var_prompt_enabled
    .word xt_fetch
    .word xt_0branch
    .word 20
.Lcp1:
    .word xt_lit
    .word str_prompt
    .word xt_lit
    .word 4
    .word xt_type

cold_after_prompt:
    .word xt_tib       # TIB address
    .word xt_lit
    .word 256  # Max length
    .word xt_accept    # Returns count (-1 = EOF)

    # Check EOF: count == -1?
    .word xt_dup
    .word xt_lit
    .word -1
    .word xt_equals
    .word xt_0branch
    .word 8
.Lcp2:
    .word xt_drop
    .word xt_bye  # EOF: exit

.Lcp3:
    # Check blank line: count == 0?
    .word xt_dup
    .word xt_zero_equal
    .word xt_0branch
    .word 12
.Lcp4:
    .word xt_drop
    .word xt_branch
    .word -116
.Lcp5:

.Lcp6:
    # Normal line: interpret
    .word xt_num_tib   # #TIB address
    .word xt_store     # #TIB !
    .word xt_lit
    .word 0
    .word xt_to_in
    .word xt_store     # >IN !
    .word xt_interpret
    .word xt_branch
    .word -152
.Lcp7:


.data
    .p2align 2, 0
dstack_bottom:
    .space 4096
dstack_top:

    .p2align 2, 0
rstack_bottom:
    .space 4096
rstack_top:

\ Test Core Extension words: COMPILE, :NONAME C" S\" CATCH THROW

\ --- COMPILE, ---
\ Use COMPILE, to manually compile a word into a definition
: DOUBLE  ['] DUP COMPILE, ; IMMEDIATE
: TEST-CC  5 DOUBLE . . CR ;
TEST-CC

\ --- :NONAME ---
\ Execute an anonymous word directly
:NONAME 42 . CR ; EXECUTE

\ Use :NONAME with DEFER
DEFER FOO
:NONAME 99 . CR ; IS FOO
FOO

\ :NONAME returning a value
:NONAME 7 3 + ; EXECUTE . CR

\ --- C" ---
\ Counted string: byte[0] = length
: TC1  C" hello" COUNT TYPE CR ;
TC1

\ Verify count byte
: TC2  C" abc" C@ . CR ;
TC2

\ Empty string
: TC3  C" " C@ . CR ;
TC3

\ --- S\" (escape sequences) ---
\ Simple string (no escapes)
: TS0  S\" hello" TYPE CR ;
TS0

\ Newline escape
: TS1  S\" AB\nCD" TYPE CR ;
TS1

\ Tab escape
: TS2  S\" tab\there" TYPE CR ;
TS2

\ Backslash escape
: TS3  S\" a\\b" TYPE CR ;
TS3

\ Quote escapes \" and \q
: TS4  S\" say\qhi\q" TYPE CR ;
TS4

\ Length check with \z (NUL)
: TS5  S\" ab\zcd" NIP . CR ;
TS5

\ Hex escape
: TS6  S\" \x41\x42\x43" TYPE CR ;
TS6

\ --- CATCH/THROW ---
\ CATCH with no exception
: OK-WORD  42 . ;
' OK-WORD CATCH . CR

\ CATCH with THROW
: BAD-WORD  99 THROW ;
' BAD-WORD CATCH . CR

\ THROW 0 does nothing
: ZERO-THROW  0 THROW 55 . ;
' ZERO-THROW CATCH . CR

\ Stack restored to depth at CATCH entry
10 20 30 ' BAD-WORD CATCH . . . . CR

\ Nested CATCH — inner catches
: INNER  -1 THROW ;
: MID  ['] INNER CATCH ;
' MID CATCH . CR

\ Nested CATCH — inner doesn't catch, outer does
: PASS-THRU  77 THROW ;
: NO-CATCH  PASS-THRU ;
' NO-CATCH CATCH . CR

BYE

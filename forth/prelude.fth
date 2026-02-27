: \ NTIB @ TOIN ! ; IMMEDIATE
\ First definition MUST be backslash-comment.
\ Everything after this can use \ for comments.

\ --- Stack manipulation ---
: ROT    >R SWAP R> SWAP ;
: -ROT   SWAP >R SWAP R> ;
: NIP    SWAP DROP ;
: TUCK   SWAP OVER ;
: 2DUP   OVER OVER ;
: 2DROP  DROP DROP ;
: 2SWAP  >R -ROT R> -ROT ;
: 2OVER  >R >R 2DUP R> R> 2SWAP ;
: ?DUP   DUP IF DUP THEN ;

\ --- Arithmetic ---
: ABS    DUP 0< IF NEGATE THEN ;
: MIN    2DUP > IF SWAP THEN DROP ;
: MAX    2DUP < IF SWAP THEN DROP ;

\ --- Constants ---
: TRUE   -1 ;
: FALSE  0 ;
: BL     32 ;

\ --- Cell operations (32-bit, cell = 4 bytes) ---
: CELLS  4 * ;
: CELL+  4 + ;
: CHARS  ;
: CHAR+  1+ ;

\ --- Output ---
: SPACE   BL EMIT ;
: SPACES  BEGIN DUP 0 > WHILE SPACE 1- REPEAT DROP ;

\ --- Number base ---
: DECIMAL  10 BASE ! ;
: HEX      16 BASE ! ;

\ --- Memory ---
: +!  DUP @ ROT + SWAP ! ;

\ --- Comparison extensions ---
: <=  > 0= ;
: >=  < 0= ;

\ --- Defining words ---
: VARIABLE  CREATE 0 , ;
: CONSTANT  CREATE , DOES> @ ;
: 2VARIABLE  CREATE 0 , 0 , ;
: 2CONSTANT  CREATE , , DOES> 2@ ;

\ --- CASE/OF/ENDOF/ENDCASE ---
\ Usage: ( n ) CASE  1 OF ." one" ENDOF  2 OF ." two" ENDOF  ." default" ENDCASE
: CASE    0 ; IMMEDIATE
: OF      POSTPONE OVER POSTPONE = POSTPONE IF POSTPONE DROP ; IMMEDIATE
: ENDOF   POSTPONE ELSE ; IMMEDIATE
: ENDCASE POSTPONE DROP  BEGIN ?DUP WHILE POSTPONE THEN REPEAT ; IMMEDIATE

\ --- VALUE / TO ---
\ Usage: 42 VALUE X   X .   99 TO X   X .
\ Body is at XT+8 for all CREATE/DOES> words (skip codeword + does-cell).
: VALUE   CREATE , DOES> @ ;
: TO  ' 8 +  STATE @ IF POSTPONE LIT , POSTPONE !  ELSE !  THEN ; IMMEDIATE

\ --- Deferred words ---
\ Usage: DEFER GREET   : HI ." hi" CR ;   ' HI IS GREET   GREET
: DEFER      CREATE 0 , DOES> @ EXECUTE ;
: IS         ' 8 +  STATE @ IF POSTPONE LIT , POSTPONE !  ELSE !  THEN ; IMMEDIATE
: ACTION-OF  ' 8 +  STATE @ IF POSTPONE LIT , POSTPONE @  ELSE @  THEN ; IMMEDIATE

\ --- Paren comments ---
: (  BEGIN
       TOIN @ NTIB @ < WHILE
       TIB TOIN @ + C@   1 TOIN +!
       41 = IF EXIT THEN
     REPEAT ; IMMEDIATE

\ --- Compile-time literal ---
: LITERAL  ['] LIT , , ; IMMEDIATE

\ --- Comparison extensions ---
: 0>   0 > ;
: 0<>  0 <> ;

\ --- Memory operations ---
: FILL  ( addr u char -- )
  -ROT  0 DO 2DUP I + C! LOOP 2DROP ;

: ERASE  ( addr u -- )  0 FILL ;

\ MOVE is now a kernel primitive (calls memmove)

\ --- Pictured Numeric Output (double-cell) ---
: MU/MOD  ( ud u -- rem udquot )
  >R 0 R@ UM/MOD -ROT R> UM/MOD ROT ;

: #  ( ud -- ud' )
  BASE @ MU/MOD ROT
  DUP 10 < IF 48 + ELSE 10 - 65 + THEN
  HOLD ;

: #S  ( ud -- 0 0 )
  BEGIN # 2DUP OR 0= UNTIL ;

: SIGN  ( n -- )
  0< IF 45 HOLD THEN ;

: U.  ( u -- )
  0 <# #S #> TYPE SPACE ;

: .R  ( n width -- )
  SWAP DUP >R ABS 0
  <# #S R> SIGN #>
  ROT OVER - SPACES TYPE ;

: U.R  ( u width -- )
  >R 0 <# #S #> R> OVER - SPACES TYPE ;

\ --- String Utilities ---
: CMOVE  ( src dest u -- )
  DUP 0 > IF 0 DO  OVER I + C@  OVER I + C!  LOOP ELSE DROP THEN 2DROP ;

: CMOVE>  ( src dest u -- )
  BEGIN DUP 0 > WHILE
    1- >R  OVER R@ + C@  OVER R@ + C!  R>
  REPEAT DROP 2DROP ;

: /STRING  ( addr u n -- addr+n u-n )
  ROT OVER + ROT ROT - ;

: COMPARE  ( a1 u1 a2 u2 -- n )
  ROT 2DUP SWAP - >R MIN    ( a1 a2 min-len  R: u1-u2 )
  DUP 0 > IF
    0 DO
      OVER I + C@  OVER I + C@  -
      ?DUP IF
        0< IF 2DROP R> DROP -1 ELSE 2DROP R> DROP 1 THEN
        UNLOOP EXIT
      THEN
    LOOP
  ELSE DROP THEN
  2DROP
  R> DUP IF 0< IF -1 ELSE 1 THEN THEN ;

: (STREQ)  ( a1 a2 u -- flag )
  DUP 0 > IF
    0 DO
      OVER I + C@  OVER I + C@ <> IF 2DROP 0 UNLOOP EXIT THEN
    LOOP
  ELSE DROP THEN
  2DROP 1 ;

: SEARCH  ( a1 u1 a2 u2 -- a3 u3 flag )
  DUP 0= IF 2DROP 1 EXIT THEN
  ROT >R ROT R>                ( a2 u2 a1 u1 )
  DUP 3 PICK - 1+ 0 MAX       ( a2 u2 a1 u1 limit )
  DUP 0 > IF
    0 DO
      OVER I +                   ( a2 u2 a1 u1 a1+i )
      4 PICK 4 PICK              ( a2 u2 a1 u1 a1+i a2 u2 )
      (STREQ) IF
        SWAP I + SWAP I -        ( a2 u2 a1+i u1-i )
        2SWAP 2DROP 1
        UNLOOP EXIT
      THEN
    LOOP
  ELSE DROP THEN
  2SWAP 2DROP 0 ;

: PLACE  ( addr u dest -- )
  2DUP C!  1+ SWAP CMOVE ;

: -TRAILING  ( addr u -- addr u' )
  BEGIN DUP 0 > WHILE
    2DUP + 1- C@ BL <> IF EXIT THEN 1-
  REPEAT ;

\ --- Double-Number Arithmetic ---
: D>S  ( d -- n )  DROP ;
: DNEGATE  ( d -- -d )  INVERT SWAP INVERT SWAP 1 0 D+ ;
: DABS  ( d -- |d| )  DUP 0< IF DNEGATE THEN ;
: D0=  ( d -- flag )  OR 0= ;
: D0<  ( d -- flag )  NIP 0< ;
: D=  ( d1 d2 -- flag )  D- D0= ;
: D<  ( d1 d2 -- flag )  ROT 2DUP = IF 2DROP U< ELSE > NIP NIP THEN ;
: M+  ( d n -- d )  S>D D+ ;
: D.  ( d -- )  DUP >R DABS <# #S R> SIGN #> TYPE SPACE ;
: D.R  ( d width -- )  >R DUP >R DABS <# #S R> SIGN #> R> OVER - SPACES TYPE ;

\ --- Core Arithmetic & System ---

: SM/REM  ( d n -- rem quot )
  2DUP XOR >R              \ R: sign of quotient
  OVER >R                   \ R: sign of remainder (= sign of dividend hi)
  ABS >R DABS R>            \ make all positive: ( ud |n| )
  UM/MOD                    \ ( urem uquot )
  R> 0< IF SWAP NEGATE SWAP THEN   \ rem gets dividend sign
  R> 0< IF NEGATE THEN ;           \ quot gets combined sign

: FM/MOD  ( d n -- rem quot )
  DUP >R                    \ save divisor
  SM/REM
  DUP 0< IF                 \ quotient negative?
    OVER IF                  \ remainder nonzero?
      1- SWAP R> + SWAP EXIT
    THEN
  THEN
  R> DROP ;

: */MOD  ( n1 n2 n3 -- rem quot )  >R M* R> FM/MOD ;
: */     ( n1 n2 n3 -- n4 )  */MOD NIP ;

: >BODY  ( xt -- addr )  8 + ;

: SOURCE  ( -- addr u )  SOURCE-PTR @ NTIB @ ;

: WITHIN  ( n lo hi -- flag )  OVER - >R - R> U< ;

: ALIGNED  ( addr -- a-addr )  3 + -4 AND ;

: ALIGN  ( -- )  HERE ALIGNED HERE - ALLOT ;

: ABORT"  ( flag "text" -- )
  POSTPONE IF
  POSTPONE S" POSTPONE TYPE
  POSTPONE ABORT
  POSTPONE THEN ; IMMEDIATE

: >IN  ( -- addr )  TOIN ;

: PAD  ( -- addr )  HERE 128 + ;

: >NUMBER  ( ud1 c-addr1 u1 -- ud2 c-addr2 u2 )
  BEGIN
    DUP
  WHILE
    OVER C@
    DUP 48 58 WITHIN IF 48 - ELSE
    DUP 65 91 WITHIN IF 65 - 10 + ELSE
    DUP 97 123 WITHIN IF 97 - 10 + ELSE
    DROP EXIT THEN THEN THEN
    DUP BASE @ >= IF DROP EXIT THEN
    >R 2SWAP SWAP BASE @ UM* ROT BASE @ * + R> 0 D+
    2SWAP SWAP 1+ SWAP 1-
  REPEAT ;

\ --- Utilities, Loops & Parsing ---

: 2*  ( n -- n*2 )  DUP + ;
: U>  ( u1 u2 -- flag )  SWAP U< ;

: REFILL  ( -- flag )  TIB 128 ACCEPT -1 <> ;

: MARKER  ( "name" -- )
  LATEST @ HERE @
  CREATE , ,
  DOES>
    DUP @ HERE !
    CELL+ @ LATEST ! ;

: .(  ( "text)" -- )  41 PARSE TYPE ; IMMEDIATE

: BUFFER:  ( u "name" -- )  CREATE ALLOT ;

: NOOP  ( -- ) ;

: ROLL  ( xu..x0 u -- xu-1..x0 xu )
  ?DUP IF SWAP >R 1- RECURSE R> SWAP THEN ;

: HOLDS  ( addr u -- )
  BEGIN DUP WHILE 1- 2DUP + C@ HOLD REPEAT 2DROP ;

: [DEFINED]  ( "name" -- flag )  WORD FIND DROP 0<> ; IMMEDIATE
: [UNDEFINED]  ( "name" -- flag )  WORD FIND DROP 0= ; IMMEDIATE

: BLANK  ( addr u -- )  BL FILL ;

\ --- Search-Order Words ---
: DEFINITIONS  ( -- )  GET-ORDER OVER SET-CURRENT 0 ?DO DROP LOOP ;
: ALSO         ( -- )  GET-ORDER OVER SWAP 1+ SET-ORDER ;
: PREVIOUS     ( -- )  GET-ORDER SWAP DROP 1- SET-ORDER ;
: FORTH        ( -- )  GET-ORDER NIP FORTH-WORDLIST SWAP SET-ORDER ;
: ONLY         ( -- )  -1 SET-ORDER ;
: ORDER        ( -- )  ." Search:" GET-ORDER 0 ?DO SPACE . LOOP
                       ."  Comp:" SPACE GET-CURRENT . CR ;

\ --- Missing Core / Utility Words ---

: WORDS
  LATEST @
  BEGIN
    DUP
  WHILE
    DUP 4 + C@ 127 AND >R
    DUP 5 + R@ TYPE SPACE
    R> DROP @
  REPEAT
  DROP CR ;

: UNUSED ( -- u )
  LIMIT HERE - ;

\ --- File I/O words removed for RV32IM port ---

: ENVIRONMENT? ( c-addr u -- false | i*x true )
  2DUP S" /COUNTED-STRING" COMPARE 0= IF 2DROP 255 TRUE EXIT THEN
  2DUP S" /HOLD" COMPARE 0= IF 2DROP 128 TRUE EXIT THEN
  2DUP S" /PAD" COMPARE 0= IF 2DROP 128 TRUE EXIT THEN
  2DUP S" ADDRESS-UNIT-BITS" COMPARE 0= IF 2DROP 8 TRUE EXIT THEN
  2DUP S" CORE" COMPARE 0= IF 2DROP TRUE TRUE EXIT THEN
  2DUP S" FLOATING" COMPARE 0= IF 2DROP FALSE TRUE EXIT THEN
  2DROP FALSE ;

\ --- Compile-time ---
: COMPILE,  ( xt -- )  , ;

\ Enable interactive prompts
PROMPTS-ON

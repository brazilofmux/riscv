\ Test compiler words

\ Basic colon definition
: DOUBLE  2 * ;
5 DOUBLE . CR

\ ' (tick) and EXECUTE
: HELLO  42 . CR ;
' HELLO EXECUTE

\ ['] (bracket-tick) in a definition
: T1  ['] HELLO EXECUTE ;
T1

\ ' pushes nonzero XT
' DUP 0 > . CR

\ CHAR
CHAR A . CR
CHAR Z . CR
CHAR 0 . CR

\ [CHAR] in a definition
: T2  [CHAR] * EMIT CR ;
T2

\ RECURSE
: FACTORIAL  DUP 1 > IF DUP 1- RECURSE * ELSE DROP 1 THEN ;
5 FACTORIAL . CR
1 FACTORIAL . CR
0 FACTORIAL . CR

\ POSTPONE (non-immediate word)
: MY-DUP  POSTPONE DUP ; IMMEDIATE
: T3  7 MY-DUP + . CR ;
T3

\ POSTPONE (immediate word)
: MY-IF  POSTPONE IF ; IMMEDIATE
: T4  1 MY-IF ." ok" CR THEN ;
T4

\ IMMEDIATE
: ALWAYS-42  42 ; IMMEDIATE
: T5  ALWAYS-42 . CR ;
T5

\ LIT is findable
' LIT 0 > . CR

BYE

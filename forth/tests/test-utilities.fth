\ Test utility words: C, U< ( LITERAL FILL ERASE MOVE 0> 0<>

\ --- C, (byte compile) ---
CREATE BUF1
65 C, 66 C, 67 C,
BUF1 C@ . CR
BUF1 1+ C@ . CR
BUF1 2 + C@ . CR

\ --- U< (unsigned less-than) ---
1 2 U< . CR
2 1 U< . CR
5 5 U< . CR
-1 1 U< . CR
1 -1 U< . CR

\ --- ( paren comments ---
( This is a comment )
42 ( inline comment ) . CR

\ --- LITERAL in a definition ---
: LIT-TEST  [ 10 20 + ] LITERAL ;
LIT-TEST . CR

\ --- 0> ---
5 0> . CR
0 0> . CR
-3 0> . CR

\ --- 0<> ---
5 0<> . CR
0 0<> . CR
-1 0<> . CR

\ --- FILL ---
CREATE BUF2 8 ALLOT
BUF2 8 42 FILL
BUF2 C@ . CR
BUF2 3 + C@ . CR
BUF2 7 + C@ . CR

\ --- ERASE ---
BUF2 8 ERASE
BUF2 C@ . CR
BUF2 4 + C@ . CR

\ --- MOVE (forward, no overlap) ---
CREATE SRC 8 ALLOT
CREATE DST 8 ALLOT
SRC 4 65 FILL
SRC DST 4 MOVE
DST C@ . CR
DST 3 + C@ . CR

\ --- MOVE (backward, overlap test) ---
\ Fill a buffer with 1 2 3 4 5 0 0 0
CREATE OVL 8 ALLOT
OVL 8 ERASE
1 OVL C!  2 OVL 1+ C!  3 OVL 2 + C!  4 OVL 3 + C!  5 OVL 4 + C!
\ Move 5 bytes from OVL to OVL+2 (dest > src, overlap)
OVL OVL 2 + 5 MOVE
\ Should be: 1 2 1 2 3 4 5 0
OVL C@ . CR
OVL 1+ C@ . CR
OVL 2 + C@ . CR
OVL 3 + C@ . CR
OVL 4 + C@ . CR
OVL 5 + C@ . CR
OVL 6 + C@ . CR

BYE

\ Test stack operations
\ DUP DROP SWAP OVER >R R> R@ DEPTH
\ Prelude: ROT -ROT NIP TUCK 2DUP 2DROP 2SWAP ?DUP

\ DUP
1 DUP . . CR

\ DROP
1 2 DROP . CR

\ SWAP
1 2 SWAP . . CR

\ OVER
1 2 OVER . . . CR

\ >R R> (round-trip)
42 >R R> . CR

\ R@ (peek without pop)
99 >R R@ . R> DROP CR

\ DEPTH
DEPTH . CR
1 2 3 DEPTH . DROP DROP DROP CR

\ ROT
1 2 3 ROT . . . CR

\ -ROT
1 2 3 -ROT . . . CR

\ NIP
1 2 NIP . CR

\ TUCK
1 2 TUCK . . . CR

\ 2DUP
3 4 2DUP . . . . CR

\ 2DROP
1 2 3 4 2DROP . . CR

\ 2SWAP
1 2 3 4 2SWAP . . . . CR

\ ?DUP (nonzero)
5 ?DUP . . CR

\ ?DUP (zero)
0 ?DUP . CR

BYE

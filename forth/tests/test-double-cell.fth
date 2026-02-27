\ Test double-cell operations

\ 2! and 2@
2VARIABLE DBL1
100 200 DBL1 2!
DBL1 2@ . . CR

999 777 DBL1 2!
DBL1 2@ . . CR

\ 2>R and 2R>
11 22 2>R 2R> . . CR

\ 2R@
33 44 2>R 2R@ . . CR
2R> DROP DROP

\ 2OVER
1 2 3 4 2OVER . . . . . . CR

\ 2VARIABLE default
2VARIABLE DBL2
DBL2 2@ . . CR

\ 2CONSTANT
55 66 2CONSTANT PAIR1
PAIR1 . . CR
PAIR1 . . CR

\ 2>R/2R> with interleaved work
10 20 30 40 2>R . . 2R> . . CR

BYE

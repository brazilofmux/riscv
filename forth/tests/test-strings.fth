\ Test string operations

\ ." (dot-quote)
: T1  ." Hello World" CR ;
T1

\ S" and TYPE
: T2  S" Forth" TYPE CR ;
T2

\ Read first byte and print the rest
: T3  S" test" DROP DUP C@ . 1+ 3 TYPE CR ;
T3

\ Concatenated output
: T4  ." abc" ." def" CR ;
T4

\ Empty string
: T5  ." " ." ok" CR ;
T5

BYE

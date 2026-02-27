\ Test memory operations

\ ! and @ (store and fetch)
VARIABLE A
42 A !
A @ . CR

99 A !
A @ . CR

\ C! and C@ (byte store and fetch)
VARIABLE B
0 B !
65 B C!
B C@ . CR

\ +! (add to memory)
VARIABLE C
10 C !
5 C +!
C @ . CR

\ VARIABLE (initial value is 0)
VARIABLE D
D @ . CR

\ CONSTANT
42 CONSTANT LIFE
LIFE . CR

\ VALUE and TO (interpret mode)
100 VALUE V1
V1 . CR
200 TO V1
V1 . CR

\ VALUE and TO (compile mode)
: SET-V1  300 TO V1 ;
SET-V1
V1 . CR

BYE

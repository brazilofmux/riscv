\ Test control flow

\ IF THEN (true)
: T1  1 IF ." yes" THEN CR ;
T1

\ IF THEN (false)
: T2  0 IF ." yes" THEN ." done" CR ;
T2

\ IF ELSE THEN (true)
: T3  1 IF ." true" ELSE ." false" THEN CR ;
T3

\ IF ELSE THEN (false)
: T4  0 IF ." true" ELSE ." false" THEN CR ;
T4

\ Nested IF
: T5  1 IF 1 IF ." both" THEN THEN CR ;
T5

\ BEGIN UNTIL (count down)
: T6  3 BEGIN DUP . 1- DUP 0= UNTIL DROP CR ;
T6

\ BEGIN WHILE REPEAT
: T7  3 BEGIN DUP 0 > WHILE DUP . 1- REPEAT DROP CR ;
T7

\ BEGIN AGAIN with EXIT
: T8  3 BEGIN DUP 0= IF DROP EXIT THEN DUP . 1- AGAIN ;
T8 CR

BYE

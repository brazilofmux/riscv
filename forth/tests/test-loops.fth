\ Test DO LOOP

\ Basic DO LOOP
: T1  5 0 DO I . LOOP CR ;
T1

\ DO LOOP with step (backwards from limit)
: T2  0 10 DO I . -2 +LOOP CR ;
T2

\ Nested loops with I and J
: T3  3 0 DO 3 0 DO I J * . LOOP LOOP CR ;
T3

\ LEAVE (early exit)
: T4  10 0 DO I DUP 5 = IF DROP LEAVE THEN . LOOP ." done" CR ;
T4

\ Single iteration
: T5  1 0 DO ." once" LOOP CR ;
T5

\ Loop body executes once then exits
: T6  ." x" 2 1 DO ." y" LOOP ." z" CR ;
T6

BYE

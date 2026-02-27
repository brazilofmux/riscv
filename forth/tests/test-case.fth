\ Test CASE/OF/ENDOF/ENDCASE

\ Basic CASE
: DAY  CASE
    1 OF ." Mon" ENDOF
    2 OF ." Tue" ENDOF
    3 OF ." Wed" ENDOF
    4 OF ." Thu" ENDOF
    5 OF ." Fri" ENDOF
    ." ?"
  ENDCASE ;

1 DAY CR
2 DAY CR
3 DAY CR
5 DAY CR
99 DAY CR

\ CASE with numeric output
: CLASSIFY  CASE
    0 OF ." zero" ENDOF
    1 OF ." one" ENDOF
    ." other"
  ENDCASE CR ;

0 CLASSIFY
1 CLASSIFY
42 CLASSIFY

\ CASE in a loop
: T3  5 1 DO I CASE
    1 OF ." a" ENDOF
    2 OF ." b" ENDOF
    3 OF ." c" ENDOF
    4 OF ." d" ENDOF
  ENDCASE LOOP CR ;
T3

BYE

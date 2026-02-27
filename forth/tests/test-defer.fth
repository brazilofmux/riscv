\ Test DEFER, IS, ACTION-OF

\ Basic DEFER and IS
DEFER GREET
: HI  ." hi" ;
: YO  ." yo" ;

' HI IS GREET
GREET CR

' YO IS GREET
GREET CR

\ IS inside a compiled definition
: SET-HI  ['] HI IS GREET ;
SET-HI
GREET CR

\ ACTION-OF (interpret mode)
ACTION-OF GREET ' HI = . CR

\ DEFER with numeric output
DEFER GETVAL
: FORTY-TWO  42 ;
: NINETY-NINE  99 ;

' FORTY-TWO IS GETVAL
GETVAL . CR

' NINETY-NINE IS GETVAL
GETVAL . CR

BYE

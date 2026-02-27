\ Test number base operations

\ Default base is 10
BASE @ . CR

\ HEX mode
HEX
FF . CR
10 . CR

\ Back to DECIMAL
DECIMAL
255 . CR
16 . CR

\ BASE ! directly
8 BASE !
77 . CR

\ Restore
DECIMAL
63 . CR

BYE

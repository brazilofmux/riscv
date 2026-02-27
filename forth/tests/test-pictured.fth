\ Test pictured numeric output

\ Basic U.
42 U. CR

\ U. zero
0 U. CR

\ U. large number
12345 U. CR

\ HOLD for custom prefix: $2A (hex 42)
HEX
: TEST-PREFIX  0 <# #S 24 HOLD #> TYPE ;
2A TEST-PREFIX CR
DECIMAL

\ SIGN positive (should not add minus)
<# 42 0 #S 42 SIGN #> TYPE CR

\ SIGN negative (should add minus)
<# 42 0 #S -1 SIGN #> TYPE CR

\ .R wider than number
42 6 .R CR

\ .R exact width
42 2 .R CR

\ .R narrower than number (no truncation)
12345 2 .R CR

\ .R negative
-42 6 .R CR

\ U.R
42 6 U.R CR

\ HEX mode
255 HEX U. CR

\ Back to decimal
DECIMAL 255 U. CR

BYE

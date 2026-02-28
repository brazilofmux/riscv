REM === DEFTYPE test ===

REM Without DEFTYPE, bare variables default to DOUBLE
X = 3
PRINT X

REM DEFINT makes bare variables starting with I-N default to INTEGER
DEFINT I-N
I = 7
J = 3
PRINT I; " "; J
REM Integer division truncates
K = 7 / 2
PRINT K

REM Variables outside the DEFINT range still default to DOUBLE
A = 7 / 2
PRINT A

REM DEFSTR makes bare variables starting with S default to STRING
DEFSTR S
S = "hello"
PRINT S

REM Explicit suffix overrides DEFTYPE
I# = 2.5
PRINT I#

REM DEFINT in a wider range
DEFINT A-Z
B = 9 / 2
PRINT B

PRINT "Done"

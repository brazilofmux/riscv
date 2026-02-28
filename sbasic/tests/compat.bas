' Test QB compatibility features
' BIN$
PRINT "BIN$(0)="; BIN$(0)
PRINT "BIN$(10)="; BIN$(10)
PRINT "BIN$(255)="; BIN$(255)

' DIM AS SINGLE / AS LONG
DIM x AS SINGLE
x = 3.14
PRINT "Single:"; x

DIM n AS LONG
n = 42
PRINT "Long:"; n

' DIM array AS SINGLE / AS LONG
DIM a(3) AS SINGLE
a(0) = 1.5
a(1) = 2.5
PRINT "Arr single:"; a(0); a(1)

DIM b(3) AS LONG
b(0) = 100
b(1) = 200
PRINT "Arr long:"; b(0); b(1)

' DEFSNG
DEFSNG A-Z

' ? as PRINT
? "Question mark print"

' ! suffix on variable (treated as # / double)
x! = 9.99
PRINT "Bang suffix:"; x!

' ! suffix on number literal
PRINT "Literal:"; 3.14!

PRINT "Done"

' Test QB compatibility features, round 2

' 1. AS type in SUB/FUNCTION params
SUB AddNums(a AS INTEGER, b AS DOUBLE)
    PRINT "Sum:"; a + b
END SUB

FUNCTION Greet$(nm AS STRING)
    Greet$ = "Hello, " + nm
END FUNCTION

CALL AddNums(10, 3.5)
PRINT Greet$("World")

' 2. CLEAR statement
DIM x AS INTEGER
x = 42
DIM a(5) AS INTEGER
a(0) = 99
CLEAR
' After CLEAR, x should be default (0) and array gone
PRINT "After CLEAR x="; x

' 3. MKS$ / CVS
DIM f AS DOUBLE
f = 3.25
DIM s$ AS STRING
s$ = MKS$(f)
PRINT "MKS$ len:"; LEN(s$)
PRINT "CVS:"; CVS(s$)

' 4. LPOS
PRINT "LPOS:"; LPOS(0)

' 5. VIEW PRINT (no-op, should not error)
VIEW PRINT
VIEW PRINT 1 TO 25

' 6. ENVIRON statement (no-op, should not error)
ENVIRON "TEST=VALUE"

PRINT "Done"

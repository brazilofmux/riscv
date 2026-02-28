REM === OPTION EXPLICIT test ===

REM --- Without OPTION EXPLICIT, undeclared vars work ---
x = 42
PRINT "Before explicit:"; x

REM --- Enable OPTION EXPLICIT ---
OPTION EXPLICIT

REM --- Declared variables work ---
DIM y AS INTEGER
y = 100
PRINT "Declared:"; y

DIM name$ AS STRING
name$ = "hello"
PRINT "String:"; " "; name$

DIM pi AS DOUBLE
pi = 3.14159
PRINT "Double:"; pi

REM --- Undeclared variable should error ---
ON ERROR GOTO handler
z = 999
PRINT "FAIL: should not reach"
END

handler:
PRINT "Caught error:"; ERR
PRINT "Explicit OK"

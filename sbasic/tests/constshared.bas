' Test CONST and SHARED

' CONST
CONST PI = 3.14159
CONST MAX% = 100
CONST GREETING$ = "Hello"

PRINT GREETING$; ", PI ="; PI; ", MAX ="; MAX%

' SHARED in SUB
counter% = 0

SUB Increment()
    SHARED counter%
    counter% = counter% + 1
END SUB

SUB AddAmount(n%)
    SHARED counter%
    counter% = counter% + n%
END SUB

PRINT "Counter:"; counter%
CALL Increment()
PRINT "After increment:"; counter%
CALL Increment()
CALL Increment()
PRINT "After 2 more:"; counter%
CALL AddAmount(10)
PRINT "After add 10:"; counter%

' SHARED with string
msg$ = "original"

SUB ChangeMsg()
    SHARED msg$
    msg$ = "modified"
END SUB

PRINT msg$
CALL ChangeMsg()
PRINT msg$

' Labels with colons
start:
PRINT "Labels work"

' Test SUB and FUNCTION definitions
SUB Greet(name$)
    PRINT "Hello, "; name$
END SUB

FUNCTION Square%(n%)
    Square% = n% * n%
END FUNCTION

FUNCTION Factorial%(n%)
    IF n% <= 1 THEN
        Factorial% = 1
    ELSE
        Factorial% = n% * Factorial%(n% - 1)
    END IF
END FUNCTION

FUNCTION Max%(a%, b%)
    IF a% > b% THEN
        Max% = a%
    ELSE
        Max% = b%
    END IF
END FUNCTION

' Test SUB call
CALL Greet("World")
Greet "BASIC"

' Test FUNCTION calls
PRINT "4^2 ="; Square%(4)
PRINT "5! ="; Factorial%(5)
PRINT "Max(3,7) ="; Max%(3, 7)

' Test EXIT SUB
SUB EarlyExit(n%)
    IF n% < 0 THEN EXIT SUB
    PRINT "Positive:"; n%
END SUB

CALL EarlyExit(5)
CALL EarlyExit(-1)

' Test EXIT FUNCTION
FUNCTION SafeDiv(a, b)
    IF b = 0 THEN
        SafeDiv = 0
        EXIT FUNCTION
    END IF
    SafeDiv = a / b
END FUNCTION

PRINT "10/3 ="; SafeDiv(10, 3)
PRINT "5/0 ="; SafeDiv(5, 0)

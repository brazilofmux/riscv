' Test STATIC variables in SUB and FUNCTION

SUB Counter
    STATIC count%
    count% = count% + 1
    PRINT count%
END SUB

CALL Counter
CALL Counter
CALL Counter

' Test STATIC with multiple variables
SUB Accum(x%)
    STATIC total%, calls%
    total% = total% + x%
    calls% = calls% + 1
    PRINT total%; calls%
END SUB

CALL Accum(10)
CALL Accum(20)
CALL Accum(5)

' Test STATIC in FUNCTION
FUNCTION NextID%
    STATIC id%
    id% = id% + 1
    NextID% = id%
END FUNCTION

PRINT NextID%()
PRINT NextID%()
PRINT NextID%()

' Test STATIC string
SUB AppendLog(msg$)
    STATIC log$
    IF LEN(log$) > 0 THEN
        log$ = log$ + "," + msg$
    ELSE
        log$ = msg$
    END IF
    PRINT log$
END SUB

CALL AppendLog("a")
CALL AppendLog("b")
CALL AppendLog("c")

' Test that non-STATIC locals reset each call
SUB Resetter
    STATIC s%
    s% = s% + 1
    PRINT s%
END SUB

CALL Resetter
CALL Resetter

PRINT "DONE"

' Test single-line IF
x% = 10
IF x% > 5 THEN PRINT "x > 5"
IF x% < 5 THEN PRINT "WRONG" ELSE PRINT "x >= 5"

' Test block IF
IF x% = 10 THEN
    PRINT "x is 10"
    PRINT "correct"
END IF

' Test IF/ELSE block
IF x% = 5 THEN
    PRINT "WRONG"
ELSE
    PRINT "x is not 5"
END IF

' Test nested IF
y% = 20
IF x% = 10 THEN
    IF y% = 20 THEN
        PRINT "x=10, y=20"
    ELSE
        PRINT "WRONG"
    END IF
END IF

END

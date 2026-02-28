ON ERROR GOTO Handler
F$ = "test_trap.txt"
OPEN F$ FOR INPUT AS #1
PRINT "PASS: Successfully opened file"
CLOSE #1
KILL F$
END

Handler:
PRINT "In Handler, ERR="; ERR
IF ERR <> 39 THEN
    PRINT "Unexpected error "; ERR
    END
END IF
PRINT "Caught File Not Found, creating file and RESUME"
OPEN F$ FOR OUTPUT AS #2
PRINT #2, "test"
CLOSE #2
RESUME

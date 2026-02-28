REM === ON ERROR GOTO test ===

ON ERROR GOTO handler

REM Trigger a division by zero
x = 1 / 0
PRINT "After error"
GOTO test2

handler:
PRINT "Error trapped: "; ERR
PRINT "At line: "; ERL
RESUME NEXT

test2:
REM Test ERROR statement
ON ERROR GOTO handler2
ERROR 42
PRINT "After ERROR 42"
GOTO test3

handler2:
PRINT "Custom error: "; ERR
RESUME NEXT

test3:
REM Disable error handler
ON ERROR GOTO 0
PRINT "Done"

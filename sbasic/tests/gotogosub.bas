' Test GOTO and GOSUB/RETURN

' GOTO test
PRINT "Before goto"
GOTO SkipThis
PRINT "This should not print"
SkipThis:
PRINT "After goto"

' GOSUB/RETURN test
PRINT "Before gosub"
GOSUB MySub
PRINT "After gosub"
GOTO Done

MySub:
    PRINT "In subroutine"
    RETURN

Done:
PRINT "All done"

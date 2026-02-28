' Test EXIT FOR, EXIT WHILE

' EXIT FOR
PRINT "EXIT FOR:"
FOR i% = 1 TO 10
    IF i% > 3 THEN EXIT FOR
    PRINT i%;
NEXT
PRINT ""

' EXIT WHILE
PRINT "EXIT WHILE:"
x% = 1
WHILE x% <= 10
    IF x% > 4 THEN EXIT WHILE
    PRINT x%;
    x% = x% + 1
WEND
PRINT ""

' Nested loops with EXIT
PRINT "Nested EXIT:"
FOR i% = 1 TO 3
    FOR j% = 1 TO 3
        IF j% = 2 THEN EXIT FOR
        PRINT i%; j%;
    NEXT
NEXT
PRINT ""

' Multi-statement lines with colon
x% = 1 : y% = 2 : PRINT "x="; x%; "y="; y%

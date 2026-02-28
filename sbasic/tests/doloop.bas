' Test DO/LOOP variants

' DO WHILE ... LOOP
PRINT "DO WHILE:"
x% = 1
DO WHILE x% <= 5
    PRINT x%;
    x% = x% + 1
LOOP
PRINT ""

' DO UNTIL ... LOOP
PRINT "DO UNTIL:"
x% = 1
DO UNTIL x% > 5
    PRINT x%;
    x% = x% + 1
LOOP
PRINT ""

' DO ... LOOP WHILE
PRINT "LOOP WHILE:"
x% = 1
DO
    PRINT x%;
    x% = x% + 1
LOOP WHILE x% <= 5
PRINT ""

' DO ... LOOP UNTIL
PRINT "LOOP UNTIL:"
x% = 1
DO
    PRINT x%;
    x% = x% + 1
LOOP UNTIL x% > 5
PRINT ""

' EXIT DO
PRINT "EXIT DO:"
x% = 1
DO
    IF x% > 3 THEN EXIT DO
    PRINT x%;
    x% = x% + 1
LOOP
PRINT ""

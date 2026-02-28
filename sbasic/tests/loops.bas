' Test FOR loop
PRINT "FOR loop:"
sum% = 0
FOR i% = 1 TO 10
    sum% = sum% + i%
NEXT
PRINT "Sum 1-10 ="; sum%

' Test STEP
PRINT "Counting by 2:"
FOR i% = 0 TO 10 STEP 2
    PRINT i%;
NEXT
PRINT

' Test negative STEP
PRINT "Countdown:"
FOR i% = 5 TO 1 STEP -1
    PRINT i%;
NEXT
PRINT

' Test WHILE loop
PRINT "WHILE loop:"
n% = 1
WHILE n% <= 5
    PRINT n%;
    n% = n% * 2
WEND
PRINT

' Test nested FOR
PRINT "Nested:"
FOR i% = 1 TO 3
    FOR j% = 1 TO 3
        PRINT i% * 10 + j%;
    NEXT
    PRINT
NEXT

END

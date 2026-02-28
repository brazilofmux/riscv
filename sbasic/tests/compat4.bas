' compat4.bas - PRINT #n USING, RETURN label, COMMON, BYVAL/BYREF, array params

' Test 1: RETURN with label
GOSUB DoWork
PRINT "After gosub"
GOTO SkipTarget
ReturnHere:
PRINT "Returned to label"
GOTO Done1
DoWork:
PRINT "In subroutine"
RETURN ReturnHere
SkipTarget:
PRINT "ERROR: should not reach here"
Done1:

' Test 2: PRINT #n, USING (formatted file output)
OPEN "test_using.tmp" FOR OUTPUT AS #1
PRINT #1, USING "###.##"; 3.14
PRINT #1, USING "####"; 42
CLOSE #1

' Read back and verify
OPEN "test_using.tmp" FOR INPUT AS #1
DIM ln1 AS STRING
DIM ln2 AS STRING
LINE INPUT #1, ln1
LINE INPUT #1, ln2
CLOSE #1
KILL "test_using.tmp"
PRINT "Line1:"; ln1
PRINT "Line2:"; ln2

' Test 3: RETURN with line number label
GOSUB Work2
PRINT "After gosub2"
GOTO Done3
10 PRINT "At line 10"
GOTO Done3
Work2:
PRINT "In sub2"
RETURN 10
Done3:

' Test 4: COMMON (no-op, just parses)
COMMON x%, y#, z$
COMMON SHARED a%, b$

' Test 5: BYVAL/BYREF in SUB params
SUB TestByval(BYVAL n AS INTEGER)
    PRINT "Byval:"; n
END SUB
CALL TestByval(99)

' Test 6: Array param syntax in FUNCTION
FUNCTION SumTwo(a() AS INTEGER, b AS INTEGER)
    SumTwo = b * 2
END FUNCTION
PRINT "SumTwo:"; SumTwo(0, 7)

PRINT "Done"

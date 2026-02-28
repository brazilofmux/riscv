REM === File I/O Tests ===

REM FREEFILE
PRINT FREEFILE()

REM OPEN FOR OUTPUT / PRINT # / CLOSE
OPEN "test1.txt" FOR OUTPUT AS #1
PRINT #1, "Hello, File"
PRINT #1, "Line two"
PRINT #1, 42; " "; 3.14
CLOSE #1

REM OPEN FOR INPUT / LINE INPUT # / EOF / CLOSE
OPEN "test1.txt" FOR INPUT AS #1
LINE INPUT #1, a$
PRINT a$
LINE INPUT #1, b$
PRINT b$
LINE INPUT #1, c$
PRINT c$
CLOSE #1

REM WRITE # / INPUT # round trip
OPEN "test2.txt" FOR OUTPUT AS #2
WRITE #2, "hello", 42, 3.14
WRITE #2, "world", 99
CLOSE #2

OPEN "test2.txt" FOR INPUT AS #2
INPUT #2, x$, y%, z#
PRINT x$; " "; y%; " "; z#
INPUT #2, p$, q%
PRINT p$; " "; q%
CLOSE #2

REM OPEN FOR APPEND
OPEN "test1.txt" FOR APPEND AS #1
PRINT #1, "Appended line"
CLOSE #1

OPEN "test1.txt" FOR INPUT AS #1
FOR i% = 1 TO 4
  LINE INPUT #1, ln$
  PRINT ln$
NEXT
CLOSE #1

REM Clean up
KILL "test1.txt"
KILL "test2.txt"
PRINT "Done"

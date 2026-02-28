' Test QB compatibility features, round 3

' 1. RESUME with label
ON ERROR GOTO handler
ERROR 5
PRINT "Should not reach"
GOTO skip_handler
handler:
    PRINT "Error:"; ERR
    RESUME skip_handler
skip_handler:
ON ERROR GOTO 0
PRINT "After resume-label"

' 2. Fixed-length string syntax (parsed, treated as regular string)
DIM fixed AS STRING * 20
fixed = "Hello"
PRINT "Fixed:"; fixed

' 3. PALETTE (no-op)
PALETTE
PALETTE 1, 2

' 4. ON TIMER / ON KEY (no-op stubs)
ON TIMER(60) GOSUB 999
ON KEY(1) GOSUB 999

' 5. WAIT / OUT (no-op)
WAIT 888, 1
OUT 888, 0

' 6. INP function (stub)
PRINT "INP:"; INP(888)

' 7. VARPTR / SADD (stubs)
DIM dummy AS INTEGER
PRINT "VARPTR:"; VARPTR(dummy)
PRINT "SADD:"; SADD("test")

' 8. SEEK function (file position read)
OPEN "test_seek.tmp" FOR OUTPUT AS #1
PRINT #1, "Hello"
CLOSE #1
OPEN "test_seek.tmp" FOR BINARY AS #1
PRINT "SEEK:"; SEEK(#1)
CLOSE #1
KILL "test_seek.tmp"

PRINT "Done"

REM === TAB() absolute column test ===

REM TAB(10) from column 0 should emit 9 spaces (column 10 is 1-based)
PRINT "A"; TAB(10); "B"

REM TAB after some text: "Hello" is 5 chars, TAB(10) should pad to col 10
PRINT "Hello"; TAB(10); "World"

REM TAB already past target: should not go backwards
PRINT "0123456789AB"; TAB(5); "X"

REM Multiple TABs on one line
PRINT "A"; TAB(5); "B"; TAB(10); "C"

REM SPC still works (relative spacing)
PRINT "X"; SPC(5); "Y"

PRINT "Done"

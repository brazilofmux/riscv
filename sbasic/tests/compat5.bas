' compat5.bas - MIN/MAX, FLOOR/CEIL, REVERSE$, LPRINT USING, SHELL stub

' Test 1: MIN and MAX
PRINT "MIN(3,7)="; MIN(3, 7)
PRINT "MIN(-5,2)="; MIN(-5, 2)
PRINT "MAX(3,7)="; MAX(3, 7)
PRINT "MAX(-5,2)="; MAX(-5, 2)
PRINT "MIN(1.5,1.2)="; MIN(1.5, 1.2)
PRINT "MAX(1.5,1.2)="; MAX(1.5, 1.2)

' Test 2: FLOOR and CEIL
PRINT "FLOOR(3.7)="; FLOOR(3.7)
PRINT "FLOOR(-3.7)="; FLOOR(-3.7)
PRINT "FLOOR(5)="; FLOOR(5)
PRINT "CEIL(3.2)="; CEIL(3.2)
PRINT "CEIL(-3.7)="; CEIL(-3.7)
PRINT "CEIL(5)="; CEIL(5)

' Test 3: REVERSE$
PRINT "REVERSE$="; REVERSE$("HELLO")
PRINT "REVERSE$ empty="; REVERSE$("")
PRINT "REVERSE$ 1="; REVERSE$("A")

' Test 4: SHELL (stub - prints message to stderr, no stdout output)
' We don't test SHELL output since it goes to stderr

' Test 5: CHDIR/MKDIR/RMDIR (already functional)
MKDIR "test_dir_tmp"
CHDIR "test_dir_tmp"
DIM d$ AS STRING
d$ = CURDIR$
' Just check it doesn't crash - path will vary
PRINT "CHDIR OK"
CHDIR ".."
RMDIR "test_dir_tmp"

PRINT "Done"

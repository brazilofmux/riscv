REM === Advanced features test ===

REM ON GOTO
x% = 2
ON x% GOTO lab1, lab2, lab3
PRINT "Fell through"
GOTO done_ongoto
lab1:
PRINT "Branch 1"
GOTO done_ongoto
lab2:
PRINT "Branch 2"
GOTO done_ongoto
lab3:
PRINT "Branch 3"
GOTO done_ongoto
done_ongoto:

REM ON GOSUB
x% = 1
ON x% GOSUB sub1, sub2
PRINT "After ON GOSUB"
GOTO done_ongosub
sub1:
PRINT "Subroutine 1"
RETURN
sub2:
PRINT "Subroutine 2"
RETURN
done_ongosub:

REM ON GOTO with out-of-range (should just continue)
x% = 5
ON x% GOTO lab1, lab2, lab3
PRINT "Out of range continues"

REM Line continuation
x% = 1 + _
  2 + _
  3
PRINT x%

REM ERROR statement with ON ERROR
ON ERROR GOTO errhandler
ERROR 42
PRINT "After ERROR 42"
GOTO finish

errhandler:
PRINT "Custom error: "; ERR
RESUME NEXT

finish:
PRINT "Done"

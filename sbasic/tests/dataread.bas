REM === DATA/READ/RESTORE Tests ===

REM Basic DATA/READ
data1:
DATA 10, 20, 30
READ a, b, c
PRINT a; " "; b; " "; c

REM String DATA
DATA "hello", "world"
READ x$, y$
PRINT x$; " "; y$

REM Mixed types
DATA 42, "answer", 3.14
READ n%, s$, d#
PRINT n%; " "; s$; " "; d#

REM RESTORE and re-read from beginning
RESTORE data1
READ a, b, c
PRINT a; " "; b; " "; c

REM Continue reading past the restored position
READ x$, y$
PRINT x$; " "; y$

REM Multiple DATA lines are sequential
multi:
DATA 100, 200
DATA 300, 400
RESTORE multi
READ p, q, r, s
PRINT p; " "; q; " "; r; " "; s

REM DATA with negative numbers
neg:
DATA -5, -10, -15
RESTORE neg
READ x, y, z
PRINT x; " "; y; " "; z

REM RESTORE to named labels
start_data:
DATA 1, 2, 3
more_data:
DATA 4, 5, 6

RESTORE start_data
READ a, b, c
PRINT a; " "; b; " "; c

RESTORE more_data
READ a, b, c
PRINT a; " "; b; " "; c

REM READ into loop (via temp variable)
loop_data:
DATA 5, 10, 15, 20, 25
RESTORE loop_data
DIM vals(4)
FOR i% = 0 TO 4
  READ tmp
  vals(i%) = tmp
NEXT
PRINT vals(0); " "; vals(2); " "; vals(4)

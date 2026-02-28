REM === Array Tests ===

REM Basic 1D array
DIM a(5)
a(0) = 10
a(1) = 20
a(2) = 30
a(3) = 40
a(4) = 50
a(5) = 60
PRINT a(0); " "; a(3); " "; a(5)

REM String array
DIM names$(3)
names$(0) = "Alice"
names$(1) = "Bob"
names$(2) = "Charlie"
names$(3) = "Diana"
PRINT names$(1); " and "; names$(3)

REM Integer array
DIM counts%(4) AS INTEGER
counts%(0) = 100
counts%(1) = 200
counts%(2) = 300
PRINT counts%(0) + counts%(2)

REM Array in expressions
DIM x(3)
x(0) = 1.5
x(1) = 2.5
x(2) = 3.5
x(3) = 4.5
PRINT x(0) + x(1) + x(2) + x(3)

REM 2D array
DIM grid(2, 3)
grid(0, 0) = 1
grid(0, 1) = 2
grid(0, 2) = 3
grid(0, 3) = 4
grid(1, 0) = 5
grid(1, 1) = 6
grid(1, 2) = 7
grid(1, 3) = 8
grid(2, 0) = 9
grid(2, 1) = 10
grid(2, 2) = 11
grid(2, 3) = 12
PRINT grid(0, 0); " "; grid(1, 2); " "; grid(2, 3)

REM LBOUND and UBOUND
DIM arr(10)
PRINT LBOUND(arr); " "; UBOUND(arr)

REM OPTION BASE test
OPTION BASE 1
DIM b(5)
b(1) = 111
b(5) = 555
PRINT LBOUND(b); " "; UBOUND(b)
PRINT b(1); " "; b(5)

REM ERASE and re-DIM
ERASE a
OPTION BASE 0
DIM a(3)
a(0) = 99
PRINT a(0)

REM REDIM
REDIM a(5)
PRINT a(0)
a(5) = 77
PRINT a(5)

REM REDIM PRESERVE
DIM c(3)
c(0) = 10
c(1) = 20
c(2) = 30
c(3) = 40
REDIM PRESERVE c(5)
PRINT c(0); " "; c(1); " "; c(3)
PRINT c(5)

REM Array in FOR loop
DIM nums(5)
FOR i% = 0 TO 5
  nums(i%) = i% * i%
NEXT
FOR i% = 0 TO 5
  PRINT nums(i%);
  IF i% < 5 THEN PRINT " ";
NEXT
PRINT

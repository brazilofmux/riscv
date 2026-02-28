REM === SWAP Tests ===

REM Basic variable swap
a = 10
b = 20
SWAP a, b
PRINT a; " "; b

REM String variable swap
a$ = "hello"
b$ = "world"
SWAP a$, b$
PRINT a$; " "; b$

REM Array element swap
DIM arr(5)
arr(0) = 100
arr(1) = 200
arr(2) = 300
SWAP arr(0), arr(2)
PRINT arr(0); " "; arr(1); " "; arr(2)

REM Swap array element with variable
x = 999
SWAP x, arr(1)
PRINT x; " "; arr(1)

REM String array swap
DIM names$(3)
names$(0) = "Alice"
names$(1) = "Bob"
names$(2) = "Charlie"
SWAP names$(0), names$(2)
PRINT names$(0); " "; names$(2)

REM 2D array element swap
DIM grid(2, 2)
grid(0, 0) = 1
grid(1, 1) = 9
SWAP grid(0, 0), grid(1, 1)
PRINT grid(0, 0); " "; grid(1, 1)

REM Record field swap
TYPE point
  x AS INTEGER
  y AS INTEGER
END TYPE
DIM p AS point
p.x = 50
p.y = 75
SWAP p.x, p.y
PRINT p.x; " "; p.y

REM Swap record field with variable
z = 42
SWAP z, p.x
PRINT z; " "; p.x

PRINT "Done"

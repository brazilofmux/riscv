REM === TYPE records test ===

TYPE Point
  X AS DOUBLE
  Y AS DOUBLE
END TYPE

TYPE Person
  PNAME AS STRING
  AGE AS INTEGER
  SCORE AS DOUBLE
END TYPE

DIM p AS Point
p.X = 3.5
p.Y = 7.2
PRINT p.X; " "; p.Y

DIM who AS Person
who.PNAME = "Alice"
who.AGE = 30
who.SCORE = 95.5
PRINT who.PNAME; " age "; who.AGE; " score "; who.SCORE

REM Modify a field
who.AGE = who.AGE + 1
PRINT "After birthday: "; who.AGE

REM Second instance of same type
DIM p2 AS Point
p2.X = -1.0
p2.Y = 0.0
PRINT p2.X; " "; p2.Y

REM TYPE field used in expression
PRINT "Sum: "; p.X + p.Y

REM === Scoping test: TYPE var in SUB is local ===
SUB SetPoint()
  DIM pt AS Point
  pt.X = 100.0
  pt.Y = 200.0
  PRINT "In SUB: "; pt.X; " "; pt.Y
END SUB

CALL SetPoint()
REM p should still be 3.5, 7.2 (SUB's pt is local)
PRINT "After SUB: "; p.X; " "; p.Y

REM === Re-DIM in loop: should not leak ===
DIM i% AS INTEGER
FOR i% = 1 TO 200
  DIM lp AS Point
  lp.X = i%
  lp.Y = i% * 2
NEXT i%
PRINT "Loop done, last: "; lp.X; " "; lp.Y

REM === SHARED TYPE variable ===
DIM shared_pt AS Point
shared_pt.X = 42.0
shared_pt.Y = 84.0

SUB ModifyShared()
  SHARED shared_pt
  shared_pt.X = shared_pt.X + 1.0
  shared_pt.Y = shared_pt.Y + 1.0
  PRINT "In ModifyShared: "; shared_pt.X; " "; shared_pt.Y
END SUB

CALL ModifyShared()
PRINT "After ModifyShared: "; shared_pt.X; " "; shared_pt.Y

REM === Multiple SUB calls don't exhaust slots ===
SUB MakePoint()
  DIM tmp AS Point
  tmp.X = 1.0
  tmp.Y = 2.0
END SUB

DIM j% AS INTEGER
FOR j% = 1 TO 200
  CALL MakePoint()
NEXT j%
PRINT "200 calls OK"

PRINT "Done"

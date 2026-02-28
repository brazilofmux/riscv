* SCAN FOR test (AST compiled once per SCAN, evaluated per record)
n = 0
SCAN FOR SALARY > 50000
  n = n + 1
ENDSCAN
? "SCAN matches:", n

RETURN

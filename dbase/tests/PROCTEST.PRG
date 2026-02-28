* Test PROCEDURE, PARAMETERS, RETURN, PRIVATE
x = 100
? "Before call: x=" + STR(x,3)
DO greet
? "After call: x=" + STR(x,3)
DO addnums
? "After addnums: result=" + STR(result,3)
RETURN

PROCEDURE greet
  PRIVATE x
  x = 999
  ? "In greet: x=" + STR(x,3)
RETURN

PROCEDURE addnums
  PARAMETERS a, b
  a = 10
  b = 20
  PUBLIC result
  result = a + b
  ? "In addnums: " + STR(a,2) + " + " + STR(b,2) + " = " + STR(result,2)
RETURN

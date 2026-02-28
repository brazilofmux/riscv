* Test DO...WITH parameter passing
DO addvals WITH 10, 20
? "Sum:" + STR(total, 4)
DO sayhi WITH "Alice"
DO multiply WITH 3, 7
? "Product:" + STR(answer, 3)
RETURN

PROCEDURE addvals
  PARAMETERS a, b
  PUBLIC total
  total = a + b
  ? "Adding:" + STR(a, 3) + " +" + STR(b, 3)
RETURN

PROCEDURE sayhi
  PARAMETERS name
  ? "Hello, " + name
RETURN

PROCEDURE multiply
  PARAMETERS x, y
  PUBLIC answer
  answer = x * y
RETURN

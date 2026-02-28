* AST expression test program
* Tests that AST-compiled expressions produce same results as string eval

* Test 1: DO WHILE loop (condition compiled to AST once, evaluated many times)
n = 0
DO WHILE n < 5
  n = n + 1
ENDDO
? "DO WHILE result:", n

* Test 2: Nested expression in DO WHILE
x = 1
total = 0
DO WHILE x <= 10 .AND. total < 30
  total = total + x
  x = x + 1
ENDDO
? "Nested WHILE:", total, x

RETURN

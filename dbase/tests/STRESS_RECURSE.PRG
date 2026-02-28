* Recursion and nesting stress test

* Test 1: Recursive factorial at max safe depth (5)
? "Factorial 5:", FACT(5)

* Test 2: Recursive Fibonacci at safe depth
? "Fib 4:", FIB(4)
? "Fib 5:", FIB(5)

* Test 3: Iterative factorial for large value
x = 1
FOR i = 1 TO 12
  x = x * i
NEXT
? "Iterative 12!:", x

* Test 4: Iterative Fibonacci to F(20) = 6765
a = 0
b = 1
FOR i = 1 TO 20
  c = a + b
  a = b
  b = c
NEXT
? "Iterative Fib 20:", a

* Test 5: 10-deep nested FOR loops
DO STRESS_LOOPS

RETURN

FUNCTION FACT
PARAMETERS n
IF n <= 1
  RETURN 1
ENDIF
RETURN n * FACT(n - 1)

FUNCTION FIB
PARAMETERS n
IF n <= 1
  RETURN n
ENDIF
RETURN FIB(n-1) + FIB(n-2)

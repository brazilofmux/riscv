* FOR/NEXT test program
* Basic FOR loop
FOR I = 1 TO 5
  ? I
NEXT
* STEP clause
FOR J = 10 TO 2 STEP -2
  ? J
NEXT
* Nested FOR loops
FOR I = 1 TO 3
  FOR J = 1 TO 2
    ? I * 10 + J
  NEXT
NEXT
* Empty loop (start > end with positive step)
FOR K = 5 TO 1
  ? "Should not print"
NEXT
? "After empty loop"
* EXIT in FOR
FOR I = 1 TO 100
  IF I > 3
    EXIT
  ENDIF
  ? I
NEXT
? "Exited"
* LOOP in FOR (skip I=3)
FOR I = 1 TO 5
  IF I = 3
    LOOP
  ENDIF
  ? I
NEXT
? "Done"
RETURN

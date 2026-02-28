* Test DO WHILE/ENDDO, LOOP, EXIT
i = 1
DO WHILE i <= 5
  ? i
  i = i + 1
ENDDO
? "After first loop"
* Test EXIT
i = 1
DO WHILE .T.
  IF i > 3
    EXIT
  ENDIF
  ? "Loop " + STR(i,1)
  i = i + 1
ENDDO
? "After exit loop"
* Test LOOP (skip even numbers)
i = 0
DO WHILE i < 6
  i = i + 1
  IF MOD(i,2) = 0
    LOOP
  ENDIF
  ? "Odd: " + STR(i,1)
ENDDO
? "Done"
RETURN

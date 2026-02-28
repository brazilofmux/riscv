* Test IF/ELSE/ENDIF
x = 10
IF x > 5
  ? "x is greater than 5"
ENDIF
IF x > 20
  ? "x is greater than 20"
ELSE
  ? "x is not greater than 20"
ENDIF
* Nested IF
IF x > 0
  IF x > 5
    ? "x is between 6 and infinity"
  ELSE
    ? "x is between 1 and 5"
  ENDIF
ELSE
  ? "x is zero or negative"
ENDIF
RETURN

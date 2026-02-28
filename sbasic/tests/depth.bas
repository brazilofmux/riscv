REM === Recursion depth limit test ===

REM Normal recursion should work
SUB countdown(n)
  IF n > 0 THEN
    CALL countdown(n - 1)
  ELSE
    PRINT "reached bottom"
  END IF
END SUB
CALL countdown(10)

REM Excessive recursion should trigger trappable error
ON ERROR GOTO trap
SUB infinite(n)
  CALL infinite(n + 1)
END SUB
CALL infinite(1)
PRINT "FAIL: should not reach"
END

trap:
PRINT "Caught depth error:"; ERR
PRINT "Done"

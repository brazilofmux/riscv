* Test BEGIN SEQUENCE / RECOVER and LOCAL
x = "outer"
BEGIN SEQUENCE
   x = "inside"
   ? x
   y = badfunction()
   ? "Should not reach here"
RECOVER
   ? "Recovered"
   ? x
END SEQUENCE
? "After sequence"
* Test LOCAL scoping
DO T3LOCAL.PRG
RETURN

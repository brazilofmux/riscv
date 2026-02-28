* Test DO CASE/CASE/OTHERWISE/ENDCASE
x = 2
DO CASE
  CASE x = 1
    ? "One"
  CASE x = 2
    ? "Two"
  CASE x = 3
    ? "Three"
  OTHERWISE
    ? "Other"
ENDCASE
* Test OTHERWISE branch
x = 99
DO CASE
  CASE x = 1
    ? "One"
  CASE x = 2
    ? "Two"
  OTHERWISE
    ? "Default: " + STR(x,2)
ENDCASE
* Test first CASE matches
x = 1
DO CASE
  CASE x = 1
    ? "First"
  CASE x = 1
    ? "Second (should not print)"
ENDCASE
? "Done"
RETURN

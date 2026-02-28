* Test SET FILTER TO - assumes PEOPLE.DBF already exists with data
? "All records:"
GO TOP
DO WHILE .NOT. EOF()
  ? TRIM(NAME), AGE
  SKIP
ENDDO
SET FILTER TO AGE >= 30
? "Filtered (AGE >= 30):"
GO TOP
DO WHILE .NOT. EOF()
  ? TRIM(NAME), AGE
  SKIP
ENDDO
? "Count:"
COUNT
SET FILTER TO
? "After removing filter:"
GO TOP
? TRIM(NAME), AGE
SKIP
? TRIM(NAME), AGE
RETURN

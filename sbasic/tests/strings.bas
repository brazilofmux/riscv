name$ = "Hello"
greeting$ = name$ + ", World"
PRINT greeting$
PRINT "Length:"; LEN(greeting$)
PRINT "Left 5: "; LEFT$(greeting$, 5)
PRINT "Right 6: "; RIGHT$(greeting$, 6)
PRINT "Mid 3,4: "; MID$(greeting$, 3, 4)
PRINT "CHR$(65) = "; CHR$(65)
PRINT "ASC(A) ="; ASC("A")
PRINT "STR$(42) = "; STR$(42)
PRINT "VAL(123) ="; VAL("123")
END

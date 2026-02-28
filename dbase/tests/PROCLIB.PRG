* Procedure library for SET PROCEDURE TO test
PROCEDURE GREET
PARAMETERS whom
? "Hello, " + TRIM(whom) + "!"
RETURN

PROCEDURE ADD3
PARAMETERS a, b, c
STORE a + b + c TO result
? "Sum:", result
RETURN

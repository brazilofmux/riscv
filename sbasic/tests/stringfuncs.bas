REM === String Function Tests ===

REM INSTR
PRINT INSTR("hello world", "world")
PRINT INSTR("hello world", "xyz")
PRINT INSTR(7, "hello world", "orld")

REM UCASE$ / LCASE$
PRINT UCASE$("hello World")
PRINT LCASE$("HELLO World")

REM LTRIM$ / RTRIM$ / TRIM$
PRINT "["; LTRIM$("   hello"); "]"
PRINT "["; RTRIM$("hello   "); "]"
PRINT "["; TRIM$("  hello  "); "]"

REM SPACE$ / STRING$
PRINT "["; SPACE$(5); "]"
PRINT "["; STRING$(5, "*"); "]"
PRINT "["; STRING$(3, "AB"); "]"

REM HEX$ / OCT$
PRINT HEX$(255)
PRINT HEX$(16)
PRINT OCT$(8)
PRINT OCT$(255)

REM REPLACE$
PRINT REPLACE$("hello world", "world", "BASIC")
PRINT REPLACE$("aaa", "a", "bb")
PRINT REPLACE$("no match", "xyz", "abc")

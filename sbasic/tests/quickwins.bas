REM === Quick Wins Tests: BEEP, TRACE, SPLIT, JOIN$ ===

REM --- BEEP (no-op, should not error) ---
BEEP
PRINT "BEEP: OK"

REM --- TRACE ON/OFF ---
TRACE ON
PRINT "traced line"
TRACE OFF
PRINT "untraced line"

REM --- SPLIT basic ---
DIM words$(0)
SPLIT "hello world foo", " ", words$()
PRINT "SPLIT count:"; UBOUND(words$) - LBOUND(words$) + 1
PRINT words$(0); "|"; words$(1); "|"; words$(2)

REM --- SPLIT with comma delimiter ---
ERASE words$
DIM words$(0)
SPLIT "a,b,c,d", ",", words$()
PRINT words$(0); "|"; words$(1); "|"; words$(2); "|"; words$(3)

REM --- SPLIT single element (no delimiter found) ---
ERASE words$
DIM words$(0)
SPLIT "nodashes", "-", words$()
PRINT "Single:"; words$(0)

REM --- JOIN$ basic ---
DIM arr$(3)
arr$(0) = "one"
arr$(1) = "two"
arr$(2) = "three"
arr$(3) = "four"
PRINT JOIN$(arr$(), ", ")

REM --- JOIN$ with no delimiter ---
DIM nums$(3)
nums$(0) = "a"
nums$(1) = "b"
nums$(2) = "c"
nums$(3) = "d"
PRINT JOIN$(nums$())

REM --- SPLIT then JOIN$ round-trip ---
ERASE words$
DIM words$(0)
SPLIT "the quick brown fox", " ", words$()
PRINT JOIN$(words$(), "-")

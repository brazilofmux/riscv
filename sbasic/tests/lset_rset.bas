' Test LSET/RSET, WIDTH, and no-op statements
DIM a$ AS STRING
a$ = "1234567890"
LSET a$ = "Hi"
PRINT "LSET: ["; a$; "]"
a$ = "1234567890"
RSET a$ = "Hi"
PRINT "RSET: ["; a$; "]"
a$ = "12345"
LSET a$ = "HelloWorld"
PRINT "LSET trunc: ["; a$; "]"
a$ = "12345"
RSET a$ = "XY"
PRINT "RSET short: ["; a$; "]"
WIDTH 80
SCREEN 0
FIELD #1, 20 AS n$, 10 AS c$
CHAIN "test.bas"
PCOPY 0, 1
KEY 1, "HELP"
SOUND 440, 1
PLAY "CDE"
PRINT "Done"

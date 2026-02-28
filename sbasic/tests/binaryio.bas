' Test binary file I/O: GET, PUT, SEEK
OPEN "test_bin.tmp" FOR BINARY AS #1
x% = 12345
PUT #1, 1, x%
y# = 3.14159
PUT #1, 5, y#
CLOSE #1

OPEN "test_bin.tmp" FOR BINARY AS #1
a% = 0
GET #1, 1, a%
PRINT a%
b# = 0
GET #1, 5, b#
PRINT b#
PRINT LOF(1)
CLOSE #1

' Test SEEK and positional GET/PUT
OPEN "test_bin.tmp" FOR BINARY AS #1
c% = 100
PUT #1, 1, c%
d% = 200
PUT #1, , d%
SEEK #1, 1
e% = 0
GET #1, , e%
PRINT e%
f% = 0
GET #1, , f%
PRINT f%
CLOSE #1

' Test string binary I/O
OPEN "test_str.tmp" FOR BINARY AS #1
s$ = "Hello"
PUT #1, 1, s$
CLOSE #1

OPEN "test_str.tmp" FOR BINARY AS #1
r$ = SPACE$(5)
GET #1, 1, r$
PRINT r$
CLOSE #1

' Test RANDOM mode
OPEN "test_rnd.tmp" FOR RANDOM AS #2 LEN = 4
a% = 111
PUT #2, 1, a%
b% = 222
PUT #2, 2, b%
c% = 333
PUT #2, 3, c%
CLOSE #2

OPEN "test_rnd.tmp" FOR RANDOM AS #2 LEN = 4
x% = 0
GET #2, 2, x%
PRINT x%
GET #2, 1, x%
PRINT x%
GET #2, 3, x%
PRINT x%
CLOSE #2

KILL "test_bin.tmp"
KILL "test_str.tmp"
KILL "test_rnd.tmp"
PRINT "DONE"

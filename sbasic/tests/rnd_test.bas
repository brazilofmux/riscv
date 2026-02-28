R1 = RND(1)
R2 = RND(0)
R3 = RND(0)
R4 = RND(1)
PRINT "R1 (new): "; R1
PRINT "R2 (last): "; R2
PRINT "R3 (last): "; R3
PRINT "R4 (new): "; R4
IF R1 = R2 AND R2 = R3 THEN PRINT "PASS: RND(0) matches RND(1)" ELSE PRINT "FAIL: RND(0) mismatch"
IF R4 <> R1 THEN PRINT "PASS: RND(1) moved forward" ELSE PRINT "FAIL: RND(1) stuck"
X = RND(-100)
S1 = RND(1)
X = RND(-100)
S2 = RND(1)
IF S1 = S2 THEN PRINT "PASS: RND(-n) resets sequence" ELSE PRINT "FAIL: RND(-n) sequence mismatch"

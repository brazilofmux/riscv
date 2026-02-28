* Index integrity stress test
SET TALK OFF

* Test 1: Build index, verify order
? "--- Index order ---"
FOR i = 1 TO 20
  APPEND BLANK
  REPLACE NAME WITH "NAME" + STR(21 - i, 3), VAL WITH i
NEXT
INDEX ON NAME TO IDXNAME
GO TOP
? "First:", TRIM(NAME), VAL
GO BOTTOM
? "Last:", TRIM(NAME), VAL

* Test 2: REPLACE indexed key, verify index updates
? "--- REPLACE indexed key ---"
SET ORDER TO 0
GO TOP
DO WHILE .NOT. EOF()
  oldval = VAL
  REPLACE NAME WITH "RN" + STR(oldval, 3)
  SKIP
ENDDO
SET ORDER TO 1
GO TOP
? "New first:", TRIM(NAME)
GO BOTTOM
? "New last:", TRIM(NAME)
SEEK "RN  1"
? "Seek RN  1:", FOUND()
SEEK "RN 20"
? "Seek RN 20:", FOUND()

* Test 3: Multiple indexes and SET ORDER
? "--- Multiple indexes ---"
INDEX ON STR(VAL, 5) TO IDXVAL
INDEX ON NAME TO IDXNAME
SET ORDER TO 1
GO TOP
? "Order 1 top:", TRIM(NAME)
SET ORDER TO 2
GO TOP
? "Order 2 top:", TRIM(NAME)

* Test 4: UNIQUE index
? "--- Unique index ---"
USE
USE IDXTST
ZAP
APPEND BLANK
REPLACE NAME WITH "AAA", VAL WITH 1
APPEND BLANK
REPLACE NAME WITH "BBB", VAL WITH 2
APPEND BLANK
REPLACE NAME WITH "CCC", VAL WITH 3
INDEX ON NAME TO UNIQIDX UNIQUE
? "Count:", RECCOUNT()
APPEND BLANK
REPLACE NAME WITH "AAA", VAL WITH 4
? "After dup:", RECCOUNT()
SEEK "AAA"
? "AAA val:", FOUND(), VAL

* Test 5: UNIQUE REPLACE rejection
? "--- Unique REPLACE ---"
SEEK "BBB"
? "Before:", TRIM(NAME), VAL
SET TALK ON
REPLACE NAME WITH "AAA"
SET TALK OFF
? "After:", TRIM(NAME), VAL

* Test 6: DELETE + PACK + REINDEX
? "--- Delete pack reindex ---"
DELETE FOR VAL = 2 .OR. VAL = 4
PACK
? "After pack:", RECCOUNT()
REINDEX
SEEK "AAA"
? "AAA:", FOUND(), VAL
SEEK "CCC"
? "CCC:", FOUND(), VAL
SEEK "BBB"
? "BBB:", FOUND()

USE
RETURN

* Big database stress test - 1000 records
SET TALK OFF

* Insert 1000 records
FOR i = 1 TO 1000
  APPEND BLANK
  REPLACE ITEM WITH "ITEM" + STR(i, 5), QTY WITH MOD(i, 100), PRICE WITH i * 0.01
NEXT
? "Inserted:", RECCOUNT()

* Count, Sum, Average
COUNT
SUM QTY
AVERAGE QTY

* Create index and seek
INDEX ON ITEM TO BIGIDX
SEEK "ITEM  500"
? "Found 500:", FOUND(), TRIM(ITEM), QTY

SEEK "ITEM    1"
? "Found 1:", FOUND(), TRIM(ITEM), QTY

SEEK "ITEM 9999"
? "Not found:", FOUND()

* Batch delete every other record
DELETE FOR MOD(RECNO(), 2) = 0
SET DELETED ON
COUNT
SET DELETED OFF

* Pack
PACK
? "After pack:", RECCOUNT()

* Verify index still works after pack
SEEK "ITEM    1"
? "Seek after pack:", FOUND(), TRIM(ITEM)

* Sort remaining records
SORT TO BIGSORT ON QTY /A
USE BIGSORT
GO TOP
? "First sorted QTY:", QTY
GO BOTTOM
? "Last sorted QTY:", QTY
? "Sorted count:", RECCOUNT()

USE
RETURN

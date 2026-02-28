* ON ERROR test: verify runtime errors are catchable
PRIVATE errcode, errmsg

* --- Test 1: UNIQUE violation caught by ON ERROR ---
? "--- Test 1: UNIQUE violation ---"
USE ONERR2.DBF
APPEND BLANK
REPLACE NAME WITH "Alice", NUM WITH 1
APPEND BLANK
REPLACE NAME WITH "Bob", NUM WITH 2
INDEX ON NAME TO ONERR2U.NDX UNIQUE

errcode = 0
errmsg = ""
ON ERROR DO ERRHAND2
APPEND BLANK
REPLACE NAME WITH "Alice", NUM WITH 3
ON ERROR
? "Error code:", errcode
? "Has Uniqueness:", IIF("Uniqueness" $ errmsg, "YES", "NO")
? "Record count:", RECCOUNT()

* --- Test 2: Division by zero (sanity) ---
? "--- Test 2: Division by zero ---"
errcode = 0
errmsg = ""
ON ERROR DO ERRHAND2
x = 1/0
ON ERROR
? "Error code:", errcode
? "Has division:", IIF("ivision" $ errmsg, "YES", "NO")

* --- Test 3: Record range (GO past end) ---
? "--- Test 3: Record range ---"
errcode = 0
errmsg = ""
ON ERROR DO ERRHAND2
GO 999
ON ERROR
? "Error code:", errcode

USE
? "Done"
RETURN

PROCEDURE ERRHAND2
errcode = ERROR()
errmsg = MESSAGE()
RETURN

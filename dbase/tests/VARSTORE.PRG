* Fill variable store to capacity (MEMVAR_MAX = 256)
* The loop uses 2 vars (i, name) so we can create 254 more (V1..V254)

? "--- Filling variable store ---"
i = 1
DO WHILE i <= 254
    name = "V" + LTRIM(STR(i, 4))
    STORE i TO &name
    i = i + 1
ENDDO
? "Created 254 + 2 loop vars = 256 total"

? "--- 257th variable (should fail) ---"
STORE 999 TO OVERFLOW1

? "--- DECLARE array when store is full ---"
DECLARE full_arr[3]
? "After full DECLARE (should not have succeeded)"

? "--- Release vars, then DECLARE ---"
RELEASE V1
RELEASE V2
RELEASE V3
DECLARE small_arr[3]
small_arr[1] = 42
small_arr[2] = 99
? "Array[1]:", small_arr[1]
? "Array[2]:", small_arr[2]

? "--- Done ---"
RETURN

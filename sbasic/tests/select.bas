' Test SELECT CASE

' Value matching
PRINT "Value match:"
FOR i% = 1 TO 5
    SELECT CASE i%
        CASE 1
            PRINT "one";
        CASE 2
            PRINT "two";
        CASE 3
            PRINT "three";
        CASE ELSE
            PRINT "other";
    END SELECT
NEXT

PRINT ""

' Range matching
PRINT "Range match:"
FOR score% = 0 TO 100 STEP 25
    SELECT CASE score%
        CASE 90 TO 100
            PRINT "A";
        CASE 80 TO 89
            PRINT "B";
        CASE 70 TO 79
            PRINT "C";
        CASE ELSE
            PRINT "F";
    END SELECT
NEXT

PRINT ""

' IS comparison
PRINT "IS comparison:"
x% = 42
SELECT CASE x%
    CASE IS < 10
        PRINT "small"
    CASE IS < 50
        PRINT "medium"
    CASE IS >= 50
        PRINT "large"
END SELECT

' Multiple values per CASE
PRINT "Multi-value:"
day% = 6
SELECT CASE day%
    CASE 1, 7
        PRINT "weekend"
    CASE 2, 3, 4, 5, 6
        PRINT "weekday"
END SELECT

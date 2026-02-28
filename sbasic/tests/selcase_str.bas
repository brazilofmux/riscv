' SELECT CASE with strings
name$ = ""

name$ = "HELLO"
SELECT CASE name$
    CASE "HELLO"
        PRINT "Greeting"
    CASE "BYE"
        PRINT "Farewell"
    CASE ELSE
        PRINT "Unknown"
END SELECT

name$ = "BYE"
SELECT CASE name$
    CASE "HELLO"
        PRINT "Greeting"
    CASE "BYE"
        PRINT "Farewell"
    CASE ELSE
        PRINT "Unknown"
END SELECT

name$ = "OTHER"
SELECT CASE name$
    CASE "HELLO"
        PRINT "Greeting"
    CASE "BYE"
        PRINT "Farewell"
    CASE ELSE
        PRINT "Unknown"
END SELECT

' String ranges (alphabetical)
letter$ = ""
letter$ = "C"
SELECT CASE letter$
    CASE "A" TO "M"
        PRINT "First half"
    CASE "N" TO "Z"
        PRINT "Second half"
END SELECT

letter$ = "X"
SELECT CASE letter$
    CASE "A" TO "M"
        PRINT "First half"
    CASE "N" TO "Z"
        PRINT "Second half"
END SELECT

' IS comparisons with strings
word$ = ""
word$ = "APPLE"
SELECT CASE word$
    CASE IS < "BANANA"
        PRINT "Before banana"
    CASE IS >= "BANANA"
        PRINT "Banana or after"
END SELECT

word$ = "CHERRY"
SELECT CASE word$
    CASE IS < "BANANA"
        PRINT "Before banana"
    CASE IS >= "BANANA"
        PRINT "Banana or after"
END SELECT

' Multiple values in one CASE
fruit$ = ""
fruit$ = "PEAR"
SELECT CASE fruit$
    CASE "APPLE", "PEAR", "PLUM"
        PRINT "Common fruit"
    CASE ELSE
        PRINT "Exotic fruit"
END SELECT

fruit$ = "MANGO"
SELECT CASE fruit$
    CASE "APPLE", "PEAR", "PLUM"
        PRINT "Common fruit"
    CASE ELSE
        PRINT "Exotic fruit"
END SELECT

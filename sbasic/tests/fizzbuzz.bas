FOR i% = 1 TO 20
    IF i% MOD 15 = 0 THEN
        PRINT "FizzBuzz"
    ELSE
        IF i% MOD 3 = 0 THEN
            PRINT "Fizz"
        ELSE
            IF i% MOD 5 = 0 THEN
                PRINT "Buzz"
            ELSE
                PRINT i%
            END IF
        END IF
    END IF
NEXT
END

* 10-deep nested FOR loop stress test
total = 0
FOR a = 1 TO 2
FOR b = 1 TO 2
FOR c = 1 TO 2
FOR d = 1 TO 2
FOR e = 1 TO 2
FOR f = 1 TO 2
FOR g = 1 TO 2
FOR h = 1 TO 2
FOR i = 1 TO 2
FOR j = 1 TO 2
total = total + 1
NEXT
NEXT
NEXT
NEXT
NEXT
NEXT
NEXT
NEXT
NEXT
NEXT
? "10-deep loops total:", total
RETURN

' Test LPRINT, FRE(), ERR, ERL
PRINT "FRE(0) ="; FRE(0)
PRINT "FRE(str) ="; FRE("")
LPRINT "LPRINT output"
PRINT "ERR before ="; ERR
PRINT "ERL before ="; ERL

ON ERROR GOTO errhandler
ERROR 5
PRINT "After error"
END

errhandler:
PRINT "ERR ="; ERR
PRINT "ERL ="; ERL
RESUME NEXT

* TEXT...ENDTEXT test program
? "Before TEXT block"
TEXT
This is line 1
  This has leading spaces
Line 3 with special chars: @#$%
ENDTEXT
? "After TEXT block"
* Test TEXT inside IF (true branch)
IF .T.
TEXT
IF-true text
ENDTEXT
ENDIF
* Test TEXT inside IF (false branch - should be skipped)
IF .F.
TEXT
This should NOT appear
ENDTEXT
ENDIF
? "Done"
RETURN

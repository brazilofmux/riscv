* Test ON SELECTION POPUP ... DO <proc>
SET TALK OFF
DEFINE POPUP testpop FROM 5,10
DEFINE BAR 1 OF testpop PROMPT "Alpha"
DEFINE BAR 2 OF testpop PROMPT "Beta"
DEFINE BAR 3 OF testpop PROMPT "Gamma"
ON SELECTION POPUP testpop DO sel_handler
ACTIVATE POPUP testpop
* Non-terminal: auto-selects bar 1, handler fires, deactivates
? "After popup, BAR():", BAR()
? "After popup, PROMPT():", PROMPT()
RELEASE POPUPS
RETURN

PROCEDURE sel_handler
? "Handler: BAR()=" + STR(BAR(),1) + " PROMPT()=" + PROMPT()
DEACTIVATE POPUP
RETURN

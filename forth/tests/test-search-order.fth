\ Test Search-Order Word Set
: .HEADER S" Search-Order Tests" TYPE CR ;
.HEADER

\ Helper: drop n items from stack
: NDROP ( xn...x1 n -- ) 0 ?DO DROP LOOP ;

\ Test 1: FORTH-WORDLIST returns a valid wid
: T1 FORTH-WORDLIST 0<> . CR ;
T1

\ Test 2: GET-CURRENT returns FORTH-WORDLIST initially
: T2 GET-CURRENT FORTH-WORDLIST = . CR ;
T2

\ Test 3: GET-ORDER returns count=1 and wid1=FORTH-WORDLIST
: T3 GET-ORDER 1 = . FORTH-WORDLIST = . CR ;
T3

\ Test 4: SET-CURRENT / GET-CURRENT round-trip
WORDLIST CONSTANT WL1
: T4 WL1 SET-CURRENT GET-CURRENT WL1 = . CR FORTH-WORDLIST SET-CURRENT ;
T4

\ Test 5: Define word into private wordlist, not visible in FORTH
WL1 SET-CURRENT
: SECRET-WORD 42 ;
FORTH-WORDLIST SET-CURRENT
: T5 S" SECRET-WORD" FORTH-WORDLIST SEARCH-WORDLIST . CR ;
T5

\ Test 6: Word IS visible via SEARCH-WORDLIST in its own wordlist
: T6 S" SECRET-WORD" WL1 SEARCH-WORDLIST DROP EXECUTE . CR ;
T6

\ Test 7: WORDLIST creates distinct wordlists
WORDLIST CONSTANT WL2
: T7 WL1 WL2 <> . CR ;
T7

\ Test 8: SET-ORDER with 2 wordlists, verify count and entries
: T8 FORTH-WORDLIST WL1 2 SET-ORDER
     GET-ORDER
     2 = .               \ count = 2?
     WL1 = .             \ wid1 = WL1?
     FORTH-WORDLIST = .   \ wid2 = FORTH-WORDLIST?
     CR ONLY ;
T8

\ Test 9: ONLY resets to minimum order
: T9 ALSO ONLY GET-ORDER 1 = . FORTH-WORDLIST = . CR ;
T9

\ Test 10: ALSO duplicates top of search order (1 -> 2)
: T10 ALSO
      GET-ORDER 2 = .
      FORTH-WORDLIST = .
      FORTH-WORDLIST = .
      CR ONLY ;
T10

\ Test 11: DEFINITIONS sets compilation wid to first in search order
: T11
  FORTH-WORDLIST WL1 2 SET-ORDER
  DEFINITIONS
  GET-CURRENT WL1 = . CR
  ONLY FORTH-WORDLIST SET-CURRENT ;
T11

\ Test 12: Define into WL1 via DEFINITIONS, then find it
FORTH-WORDLIST WL1 2 SET-ORDER
DEFINITIONS
: DEFS-TEST 99 ;
ONLY
FORTH-WORDLIST SET-CURRENT
: T12 S" DEFS-TEST" WL1 SEARCH-WORDLIST DROP EXECUTE . CR ;
T12

\ Test 13: SEARCH-WORDLIST returns 1 for IMMEDIATE words
: T13 S" IF" FORTH-WORDLIST SEARCH-WORDLIST NIP . CR ;
T13

\ Test 14: SEARCH-WORDLIST is case-insensitive
: T14 S" dup" FORTH-WORDLIST SEARCH-WORDLIST NIP . CR ;
T14

\ Test 15: Shadowing — same name in two wordlists
WORDLIST CONSTANT WL3
WL3 SET-CURRENT
: SHADOW-TEST 777 ;
FORTH-WORDLIST SET-CURRENT
: SHADOW-TEST 888 ;
: T15 S" SHADOW-TEST" WL3 SEARCH-WORDLIST DROP EXECUTE .
      S" SHADOW-TEST" FORTH-WORDLIST SEARCH-WORDLIST DROP EXECUTE .
      CR ;
T15

\ Test 16: PREVIOUS removes top of search order
: T16
  ALSO ALSO
  GET-ORDER DUP >R NDROP R> .
  PREVIOUS
  GET-ORDER DUP >R NDROP R> .
  CR ONLY ;
T16

\ Test 17: FORTH replaces top with FORTH-WORDLIST
: T17
  WL1 1 SET-ORDER
  FORTH
  GET-ORDER 1 = . FORTH-WORDLIST = . CR
  ONLY ;
T17

: .DONE S" All search-order tests done" TYPE CR ;
.DONE
BYE

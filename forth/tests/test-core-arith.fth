\ Test core arithmetic & system words

\ --- UM* ---
3 4 UM* . . CR
0 1 UM* . . CR
65535 65535 UM* . . CR
0 0 UM* . . CR

\ --- M* ---
3 4 M* . . CR
-3 4 M* . . CR
-3 -4 M* . . CR
32767 32767 M* . . CR

\ --- SM/REM ---
7 0 3 SM/REM . . CR
-7 -1 3 SM/REM . . CR
7 0 -3 SM/REM . . CR
-7 -1 -3 SM/REM . . CR

\ --- FM/MOD ---
7 0 3 FM/MOD . . CR
-7 -1 3 FM/MOD . . CR
7 0 -3 FM/MOD . . CR
-7 -1 -3 FM/MOD . . CR

\ --- */MOD ---
10 7 3 */MOD . . CR

\ --- */ ---
1000 2000 3 */ . CR
-10 7 3 */ . CR

\ --- WITHIN ---
5 3 8 WITHIN . CR
2 3 8 WITHIN . CR
8 3 8 WITHIN . CR
3 3 8 WITHIN . CR

\ --- ALIGNED ---
0 ALIGNED . CR
1 ALIGNED . CR
4 ALIGNED . CR
7 ALIGNED . CR

\ --- >BODY ---
VARIABLE XTTEST
' XTTEST >BODY XTTEST = . CR

\ --- SOURCE ---
: TEST-SOURCE SOURCE NIP 0> . ;
TEST-SOURCE CR

\ --- >IN ---
>IN TOIN = . CR

\ --- PAD ---
PAD HERE 128 + = . CR

\ --- EVALUATE ---
: TEST-EVAL S" 3 4 + ." EVALUATE CR ;
TEST-EVAL

BYE

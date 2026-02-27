\ Test double-number arithmetic

\ --- S>D ---
42 S>D . . CR
-1 S>D . . CR
0 S>D . . CR

\ --- D+ basic ---
1 0 2 0 D+ . . CR
\ D+ with carry: 0xFFFFFFFF + 1 = 0x100000000
-1 0 1 0 D+ . . CR

\ --- D- basic ---
5 0 3 0 D- . . CR
\ D- with borrow: 0 1 - 1 0 = 0xFFFFFFFF 0
0 1 1 0 D- . . CR

\ --- DNEGATE ---
1 0 DNEGATE . . CR
0 1 DNEGATE . . CR
0 0 DNEGATE . . CR

\ --- DABS ---
0 -1 DABS . . CR
1 0 DABS . . CR
0 0 DABS . . CR

\ --- D0= ---
0 0 D0= . CR
1 0 D0= . CR
0 1 D0= . CR

\ --- D0< ---
0 -1 D0< . CR
0 0 D0< . CR
0 1 D0< . CR

\ --- D= ---
1 2 1 2 D= . CR
1 2 1 3 D= . CR
1 2 2 2 D= . CR

\ --- D< ---
\ Compare hi cells differ
0 0 0 1 D< . CR
0 1 0 0 D< . CR
\ Compare hi cells equal, lo differs
1 5 2 5 D< . CR
2 5 1 5 D< . CR
\ Equal
3 3 3 3 D< . CR

\ --- M+ ---
10 0 5 M+ . . CR
10 0 -3 M+ . . CR

\ --- UM/MOD ---
7 0 3 UM/MOD . . CR
100 0 7 UM/MOD . . CR

\ --- D. ---
42 0 D. CR
0 0 D. CR
\ -1 as double = lo=-1 hi=-1 (two's complement)
-1 -1 D. CR

\ --- D.R ---
42 0 8 D.R CR
-1 -1 8 D.R CR

\ --- Verify single-cell output still works ---
42 U. CR
42 6 .R CR
42 6 U.R CR
-42 6 .R CR

BYE

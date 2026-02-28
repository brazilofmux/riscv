parent(tom, bob).
parent(tom, liz).
parent(bob, ann).
parent(bob, pat).

grandparent(X, Z) :- parent(X, Y), parent(Y, Z).

?- parent(tom, X), write(X), nl, fail ; true.
?- grandparent(tom, X), write(X), nl, fail ; true.

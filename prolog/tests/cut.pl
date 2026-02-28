max(X, Y, X) :- X >= Y, !.
max(_, Y, Y).

?- max(3, 5, M), write(M), nl.
?- max(7, 2, M), write(M), nl.

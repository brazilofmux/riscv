myappend([], L, L).
myappend([H|T], L, [H|R]) :- myappend(T, L, R).

mylength([], 0).
mylength([_|T], N) :- mylength(T, N1), N is N1 + 1.

myreverse([], []).
myreverse([H|T], R) :- myreverse(T, RT), myappend(RT, [H], R).

mymember(X, [X|_]).
mymember(X, [_|T]) :- mymember(X, T).

?- myappend([1,2], [3,4], X), write(X), nl.
?- mylength([a,b,c,d], N), write(N), nl.
?- myreverse([1,2,3], R), write(R), nl.
?- mymember(2, [1,2,3]), write(yes), nl.

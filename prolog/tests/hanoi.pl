hanoi(1, From, To, _) :-
    write('Move disk 1 from '), write(From), write(' to '), write(To), nl.
hanoi(N, From, To, Aux) :-
    N > 1,
    N1 is N - 1,
    hanoi(N1, From, Aux, To),
    write('Move disk '), write(N), write(' from '), write(From), write(' to '), write(To), nl,
    hanoi(N1, Aux, To, From).

?- hanoi(3, left, right, center).

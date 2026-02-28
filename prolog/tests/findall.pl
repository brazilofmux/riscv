age(peter, 7).
age(ann, 11).
age(pat, 8).

?- findall(X, age(X, _), L), write(L), nl.
?- findall(X-A, age(X, A), L), write(L), nl.

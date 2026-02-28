likes(mary, food).
likes(mary, wine).
likes(john, wine).
likes(john, mary).

?- \+ likes(mary, beer), write(yes), nl.
?- \+ likes(mary, food), write(yes), nl ; write(no), nl.

?- assert(animal(cat)), assert(animal(dog)), assert(animal(bird)).
?- animal(X), write(X), nl, fail ; true.
?- retract(animal(dog)).
?- animal(X), write(X), nl, fail ; true.

(define (square x) (* x x))
(display (map square (list 1 2 3 4 5))) (newline)

(display (map (lambda (x) (+ x 10)) (list 1 2 3))) (newline)

(display (apply + (list 1 2 3 4 5))) (newline)

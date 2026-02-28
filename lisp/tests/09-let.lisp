(display (let ((x 10) (y 20)) (+ x y))) (newline)

(display (let* ((x 5) (y (* x 2))) (+ x y))) (newline)

(define (f x)
  (let ((a (+ x 1))
        (b (+ x 2)))
    (* a b)))
(display (f 10)) (newline)

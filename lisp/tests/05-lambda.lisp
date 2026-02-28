(define (make-adder n)
  (lambda (x) (+ n x)))

(define add5 (make-adder 5))
(display (add5 10)) (newline)
(display (add5 20)) (newline)

(define (compose f g)
  (lambda (x) (f (g x))))

(define (double x) (* x 2))
(define (inc x) (+ x 1))
(define double-then-inc (compose inc double))
(display (double-then-inc 5)) (newline)

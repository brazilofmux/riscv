(define (factorial n)
  (if (= n 0) 1
      (* n (factorial (- n 1)))))

(display (factorial 0)) (newline)
(display (factorial 1)) (newline)
(display (factorial 5)) (newline)
(display (factorial 10)) (newline)

(define (fib n)
  (if (< n 2) n
      (+ (fib (- n 1)) (fib (- n 2)))))

(display (fib 0)) (newline)
(display (fib 1)) (newline)
(display (fib 10)) (newline)

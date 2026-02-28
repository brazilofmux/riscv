(display (if #t "yes" "no")) (newline)
(display (if #f "yes" "no")) (newline)
(display (if (> 5 3) "bigger" "smaller")) (newline)

(define (classify n)
  (cond
    ((< n 0) "negative")
    ((= n 0) "zero")
    (else "positive")))

(display (classify -5)) (newline)
(display (classify 0)) (newline)
(display (classify 10)) (newline)

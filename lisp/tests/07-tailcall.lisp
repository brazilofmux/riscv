(define (loop-sum n acc)
  (if (= n 0) acc
      (loop-sum (- n 1) (+ acc n))))

(display (loop-sum 10000 0)) (newline)

(define (count-down n)
  (if (= n 0) (display "done")
      (count-down (- n 1))))
(count-down 50000)
(newline)

(define (fizzbuzz n)
  (define (fb i)
    (if (> i n) ()
        (begin
          (cond
            ((= (modulo i 15) 0) (display "FizzBuzz"))
            ((= (modulo i 3) 0) (display "Fizz"))
            ((= (modulo i 5) 0) (display "Buzz"))
            (else (display i)))
          (newline)
          (fb (+ i 1)))))
  (fb 1))

(fizzbuzz 20)

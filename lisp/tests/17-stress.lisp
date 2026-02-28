; Deep non-TCO recursion to exercise GC root stack
; Each non-TCO eval frame uses several root slots
(define (deep-add n)
  (if (= n 0) 0
      (+ 1 (deep-add (- n 1)))))
(display (deep-add 60)) (newline)

; Build a deep cons chain using tail-recursive accumulator
; This exercises the GC mark stack (iterative marker must walk 900-deep chain)
(define (make-list-acc n acc)
  (if (= n 0) acc
      (make-list-acc (- n 1) (cons n acc))))
(define big (make-list-acc 900 ()))
(display (car big)) (newline)
(display (length big)) (newline)

; Allocate garbage to trigger GC while big is live
(define (alloc-garbage n)
  (if (= n 0) ()
      (begin (cons 0 0) (alloc-garbage (- n 1)))))
(define _g (alloc-garbage 500))

; Verify big survived GC intact
(display (car big)) (newline)
(display (length big)) (newline)

; Deep tail recursion (should NOT blow root stack due to TCO)
(define (tail-deep n)
  (if (= n 0) "ok"
      (tail-deep (- n 1))))
(display (tail-deep 100000)) (newline)

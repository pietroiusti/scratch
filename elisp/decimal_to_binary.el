;; Function to turn a (decimal) number into a binary number (as a string)
(defun dec-to-bin (n)
  (let ((result ""))
    (while (> n 0)
      (progn
        (setq result (concat (number-to-string (% n 2)) result))
        (setq n (/ n 2))))
    (if (eq result "")
        (setq result "0"))
    result))

(dec-to-bin 0) ;; => "0"
(dec-to-bin 1) ;; => "1"
(dec-to-bin 2) ;; => "10"
(dec-to-bin 3) ;; => "11"
(dec-to-bin 4) ;; => "100"
(dec-to-bin 128) ;; => "10000000"
(dec-to-bin 129) ;; => "10000001"
(dec-to-bin 256) ;; => "100000000"
(dec-to-bin 12093847) ;; => "101110001000100110010111"


/*
  UOS uLisp Operating System for the Lilygo T-Deck
    by hasn0life - July 2025

  based on or featuring code from: 
  LispBox LispLibrary - Hartmut Grawe - github.com/ersatzmoco
  Extensible T-Deck Lisp Editor - David Johnson-Davies http://forum.ulisp.com/t/extensible-t-deck-lisp-editor/1322

  This uLisp version licensed under the MIT license: https://opensource.org/licenses/MIT
*/

const char LispLibrary[] PROGMEM = R"lisplibrary(

;;;;; Globals


(defvar SCR-W 320)
(defvar SCR-H 240)

(defvar tscale 1)
(defvar leading (* 10 tscale))
(defvar cwidth (* 6 tscale))
	
(defun rgb (r g b) (logior (ash (logand r #xf8) 8) (ash (logand g #xfc) 3) (ash b -3)))
(defvar code_col   (rgb 200  200  200))
(defvar line_col   (rgb 90  90  90))
(defvar header_col (rgb 160  160  160))
(defvar border_col (rgb 63  40  0))
(defvar bg_col     (rgb 10  10  10))
(defvar bg_col2    (rgb 50  50  20))
(defvar cursor_col (rgb 160  60  0))
(defvar highlight_col (rgb 10 110 10))

(defun temp-fun () )

;;;; Window class

(defun uos:window (x y w h &optional title)
  (let* (	
         (set-pos_ (lambda (x_ y_) (setf x x_ y y_)))
         (set-size_ (lambda (w_ h_) (setf w w_ h h_)))
         )
    (lambda (&rest msgs)
      (case (car msgs)
        (x x) 
        (y y)
        (w w)
        (h h)
        (title (if (cadr msgs) 
                   (setf title (cadr msgs))
                   title))
        (in-x (+ x 2))
        (in-y (if title (+ y 2 3 leading) (+ y 2)))
        (in-w (- w 2))
        (in-h (if title (- h 2 3 leading) (- h 2)))
        (set-pos (apply set-pos_ (cdr msgs)))
        (set-size (apply set-size_ (cdr msgs)))
        )  
      )))

(defun draw-window-border (win)
  (let ((x (win 'x)) (y (win 'y)) (w (win 'w)) (h (win 'h)))

    (fill-rect x y w h bg_col )	
    (draw-rect x y w h border_col )
	
    (when (win 'title )
      (draw-rect x y w (+ 3 leading) border_col  )
      (set-text-color header_col bg_col )
      (set-cursor (+ x 2)  (+ y 3))
      (write-text (win 'title )))
    ))
		
(defun tmax-x (win) (- (truncate (win 'in-w) cwidth) 1))
(defun tmax-y (win) (truncate (win 'in-h) leading))

#| scroll bars dont work yet |#
(defun draw-scroll-h (win scrollpos-h scrolltotal-h)
  (let* ((x (win 'x)) (y (win 'y)) (h (win 'h)) (w (win 'w))
                      (barlen (max 4 (truncate (win 'in-h) scrolltotal-h))))
    (fill-rect (+ x (- w 2)) 
               (+  (win 'in-y)
                   (truncate (* (- h barlen (if (win 'title ) (+ 2 leading) 0)) 
                                (/ scrollpos-h scrolltotal-h)))) 
               2  barlen
               header_col)))

;;; Window Layout

(defun windows-split-v (win1 win2 &optional (ratio .5) container )
  (if container
      (progn 
       (win1 'set-pos (container 'in-x) (container 'in-y))
       (win1 'set-size (round (* (container 'in-w) ratio)) (container 'in-h))
       (win2 'set-pos (+ (container 'in-x) (round (* (container 'in-w) ratio))) (container 'in-y))
       (win2 'set-size (round (* (container 'in-w) (- 1 ratio))) (container 'in-h)))
      (progn
       (win1 'set-pos 0 0)
       (win1 'set-size (round (* SCR-W ratio)) SCR-H)
       (win2 'set-pos (round (* SCR-W ratio)) 0)
       (win2 'set-size (round (* SCR-W (- 1 ratio))) SCR-H))
      ))

(defun windows-split-h (win1 win2 &optional (ratio .5) container )
  (if container
      (progn 
       (win1 'set-pos (container 'in-x) (container 'in-y))
       (win1 'set-size (container 'in-w) (round (* (container 'in-h) ratio)) )
       (win2 'set-pos (container 'in-x) (+ (container 'in-y) (round (* (container 'in-h) ratio))))
       (win2 'set-size (container 'in-w) (round (* (container 'in-h) (- 1 ratio)))))
      (progn
       (win1 'set-pos 0 0)
       (win1 'set-size  SCR-W (round (* SCR-H ratio)))
       (win2 'set-pos 0 (round (* SCR-H ratio))) 
       (win2 'set-size SCR-W (round (* SCR-H (- 1 ratio)))))
      ))

;;; Text display functions

(defun disp-line (win line y &optional is_selected)
  (let ((ypos (+ (win 'in-y) (* y leading)))  (myl " "))
    (when line (setf myl (concatenate 'string line myl)))
    (set-cursor (win 'in-x) ypos)
    (when (> (length myl) 0)
      (if is_selected 
          (set-text-color code_col cursor_col) 
          (set-text-color code_col bg_col ))
      (write-text (subseq myl 0 (min (length myl) (+ (tmax-x win) 1))))
      )))
	
(defun disp-line-hilite (win line y)
  (let* ((ypos (+ (win 'in-y) (* y leading)))  
		 (myl (if line (concatenate 'string line " ") " "))
         (len (min (length myl) (+ (tmax-x win) 1)))
		)
    (set-cursor (win 'in-x) ypos)
    (set-text-color code_col bg_col )
    (dotimes (i len)
             (let ((c (char myl i)))
               (cond ((eq c #\STX)(set-text-color code_col cursor_col))
                     ((eq c #\ETX)(set-text-color code_col bg_col ))
                     #| no idea why its 239 |#
                     ((logbitp 7 (char-code c)) (set-text-color code_col cursor_col) 
                                                (write-text " ") (set-text-color code_col bg_col ))
                     (t (write-text c)))))))

(defun show-text (textobj)
  (let* ((i 0) 
         (ymax (min (tmax-y (textobj 'win)) (- (length (textobj 'lines)) (textobj 'scroll)))))
    (draw-window-border (textobj 'win))
    (loop
     (disp-line (textobj 'win) (nth (+ (textobj 'scroll) i) (textobj 'lines)) i)
     (incf i)
     (when (>= i ymax) (return))
     )
    ))

(defun show-text-hilite (textobj)
  (let* ((i 0) 
         (ymax (min (tmax-y (textobj 'win)) (- (length (textobj 'lines)) (textobj 'scroll)))))
    (draw-window-border (textobj 'win))
    (loop
     (disp-line-hilite (textobj 'win) (nth (+ (textobj 'scroll) i) (textobj 'lines)) i)
     (incf i)
     (when (>= i ymax) (return))
     )))

(defun show-menu (menuobj &optional (show_selected t))
  (let* ((scroll (menuobj 'scroll)) 
         (i 0) 
         (ymax (min (tmax-y (menuobj 'win)) (- (length  (menuobj 'opts)) scroll))))
    (draw-window-border (menuobj 'win))
    (loop
     (disp-line (menuobj 'win) (princ-to-string (menuobj 'nth-car (+ scroll i))) i  
                (if show_selected (= (- (menuobj 'selected) scroll)  i) nil))
     (incf i)
     (when (>= i ymax) (return))
     )))


;;;;; Menu Class

;; takes a list of cons pairs or lists with the option name string in the car and payload in the cdr
(defun uos:menu (opts win)
  (let ((scroll 0)
        (selected 0)
        )
	
    (lambda (&rest msgs)
      (case (car msgs)
        (down (when (< selected (- (length opts) 1)) 
                (incf selected)
                (setf scroll (max (- selected (tmax-y win) -1) scroll))		
                selected))
        (up (when (> selected 0) 
              (decf selected)
              (when (< selected scroll) (setf scroll selected)) 
              selected))
        (scroll scroll)
        #|get options without selecting them |#
        (nth (when (> (length opts) (cadr msgs)) (nth (cadr msgs) opts)))
        (nth-car (when (> (length opts) (cadr msgs)) (car (nth (cadr msgs) opts))))
        (nth-cdr (when (> (length opts) (cadr msgs)) (cdr (nth (cadr msgs) opts))))
        (selected selected)
        #| if we give it a list with no cons pairs it should still work? |#
        (select  (nth selected opts))
        (select-car (let ((sel (nth selected opts))) (when sel (car sel))))
        (select-cdr (let ((sel (nth selected opts))) (when sel (cdr sel))))
        (opts opts)
        (set-opts (setf opts (cadr msgs))
                  (setf scroll 0) (setf selected 0))
        (push-opts (push (cadr msgs) opts )
                   (setf scroll 0) (setf selected 0))
        (win win)
        (set-win (setf win (cadr msgs)))
        (print (format t "scroll: ~a selected: ~a" scroll  selected ))
        (e (apply eval (cdr msgs))) 
        )
      )
    ))


;;; Textdisplay Class

(defun wrap-lines (text maxlen) (mapcan (lambda (x) (split-line x maxlen)) text))

(defun uos:textdisplay (lines win)
  (let* ((scroll 0)
         )
    (setf lines (wrap-lines lines (tmax-x win)))
    (lambda (&rest msgs)
      (case (car msgs)
        (lines lines)
        (set-lines (setf lines (wrap-lines (cadr msgs) (tmax-x win))) (setf scroll 0))
        (up (when (> scroll 0) (decf scroll)))
        (down (when (< scroll (length lines) ) (incf scroll)))
        (scroll scroll)
			
        (win win)
        (set-win (setf win (cadr msgs)))
        (print (format t "scroll: ~a " scroll))
        ))))


;
;
; Message Display popup
;
; takes a list of lines ex: (list "line 1" " " "line 3")

(defun display-message (text-lst &optional isprompt)
  (defvar outer-w (uos:window (- 60 5) (- 80 5) (+ 200 10) (+ 60 10)))
  (defvar w (uos:textdisplay text-lst (uos:window 60 80 200 60) ))
  (draw-window-border outer-w)
  (draw-window-border (w 'win))
  (show-text w)
  (when isprompt
    (get-key)))
;
;; Documentation Browser Application
;

(defun get-doc-text (keyword)
  (let ((doc-str (documentation keyword )))
    (if doc-str
        (split-string-to-list (string #\Newline)  
                              (format nil "~a~%~%" doc-str)) 
        (list (concatenate 'string "No doc for " (string keyword)))
        )
    ))

(defun update-doc (doc menu win)
  (doc 'set-lines (get-doc-text (menu 'select-car)))
  (win 'title (menu 'select-car))
  (show-text doc))
	
(defun update-menu (menu win)
  (menu 'set-opts (mapcar (lambda (x) (list x)) (apropos-list search)))
  (win 'title search)
  (show-menu menu))

(defun uos:doc-browser (&optional args (win (uos:window 0 0 SCR-W SCR-H "Function Browser")) )
  (let* ((menu-win (uos:window 0 0 100 100 ""))
         (menu (uos:menu (mapcar (lambda (x) (list x)) (apropos-list "")) menu-win)) 
         (doc-win  (uos:window 0 0 100 100 (string (menu 'select-car))))
         (doc nil)
         (search "")
         )
	
    (windows-split-v menu-win doc-win .33 win)
    (setf doc (uos:textdisplay  (get-doc-text (menu 'select-car)) doc-win))
	
    (lambda (&rest msgs)
      (case (car msgs)
        (up (menu 'up) (show-menu menu) (update-doc doc menu doc-win))
        (down (menu 'down) (show-menu menu) (update-doc doc menu doc-win))
        (right (doc 'down)(show-text doc))
        (left (doc 'up) (show-text doc))
        (enter (wm 'push-result (list (menu 'select-car) 'symbol)) 
               (setf current-window wm) (current-window 'show))
        (del (when (> (length search) 0) 
               (setf search (subseq search 0 (- (length search) 1)))
               (update-menu menu menu-win)
               (update-doc doc menu doc-win)))
				
        (show (draw-window-border win) (show-menu menu) (show-text doc))
        (win win)
        (select (string  (menu 'select-car)))
        (title (concatenate 'string "Function Browser " search))
        (t (when (printable (code-char lastkey))
             (setf search (concatenate 'string search (string (code-char lastkey))))
             (update-menu menu menu-win)
             (update-doc doc menu doc-win)))))))

;
; Text Viewer Application
;

(defun uos:text-viewer (&optional args (win (uos:window 0 0 SCR-W SCR-H "Text Viewer")) )
  (let* ( (path (when (eq (cadr args) 'path) (car args)))
          (text (cond 
                  ((eq (cadr args) 'text) (list (car args)))
                  ((eq (cadr args) 'symbol) 
					(let ((txt (with-output-to-string (str2) (pprint (eval (car args)) str2))))
						(split-string-to-list (string #\Newline) 
						(with-output-to-string (str) 
							(dotimes (x (length txt))	
								(let ((c (char txt x))) 
									(when (not (or (eq c #\ETX) (eq c #\STX)))
										(princ c str))))))))))
          (lines (if path (read-file path) (if text text (list "no file selected"))))
          (txt (uos:textdisplay lines win))
          )
    (win 'title (concatenate 'string "TextViewer: " 
								(if path path "") 
								(when (eq (cadr args) 'symbol) (princ-to-string (car args)))))
    (lambda (&rest msgs)
      (case (car msgs)
        (up (txt 'up) (show-text txt))
        (down (txt 'down) (show-text txt))
		(ppage (dotimes (x 5) (txt 'up)) (show-text txt))
        (npage (dotimes (x 5) (txt 'down)) (show-text txt))
        (show (draw-window-border (txt 'win)) (show-text txt))
        (title (win 'title))
        ))))



;
; Text Editor Application
;

(defun set-to-eol ()
  (let ((len (length (nth txtpos-y lines)))) 
    (when (> txtpos-x len) (setf txtpos-x len))))
		
(defun move-window ()
  (when (> txtpos-x (+ scroll-x (1- (tmax-x win))))
    (setf scroll-x (- txtpos-x (1- (tmax-x win)))))
  (when (> txtpos-y (+ scroll-y (1- (tmax-y win))))
    (setf scroll-y (- txtpos-y (1- (tmax-y win)))))
  (when (< txtpos-x scroll-x)
    (setf scroll-x txtpos-x))
  (when (< txtpos-y scroll-y)
    (setf scroll-y txtpos-y)))

(defun uos:texteditdisplay (lines win)
  (let* ((scroll-x 0) 
		 (scroll-y 0)
	     (txtpos-x 0) (txtpos-y 0)
	     (len (length lines))
	     )
    (lambda (&rest msgs)
      (case (car msgs)
        (lines lines)
        (up (when (> txtpos-y 0) (decf txtpos-y) (set-to-eol) (move-window) ))
        (down (when (< txtpos-y (1- len)) (incf txtpos-y) (set-to-eol) (move-window)))
        (left (cond ((> txtpos-x 0) (decf txtpos-x))
                    ((> txtpos-y 0)  (decf txtpos-y) (setf txtpos-x  (length (nth txtpos-y lines)))))
              (move-window))
        (right (cond ((< txtpos-x (length (nth txtpos-y lines))) (incf txtpos-x)) 
                     ((< txtpos-y (1- len)) (incf txtpos-y) (setf txtpos-x 0)))
               (move-window))
        (scroll-x scroll-x)
        (scroll-y scroll-y)
        (txtpos-x txtpos-x)
        (txtpos-y txtpos-y)
        (win win)
        (set-win (setf win (cadr msgs)))
        (enter (let* ((line (nth txtpos-y lines)) 
                      (firstlines (subseq lines 0 txtpos-y))
                      (lastlines (subseq lines (1+ txtpos-y))))
                 (setf lines (append firstlines (list (subseq line 0 txtpos-x) (subseq line txtpos-x)) lastlines)))
               (setf len (length lines))
               (setf txtpos-x 0) (incf txtpos-y) 
               (move-window))
        (del (let* ((line (nth txtpos-y lines)))
               (if (> txtpos-x 0)
                   (progn (setf (nth txtpos-y lines) 
                                (concatenate 'string (subseq line 0 (1- txtpos-x)) (subseq line txtpos-x)))
                          (decf txtpos-x))
                   (when (> txtpos-y 0)
                     (setf lines (remove-nth txtpos-y lines))
                     (decf txtpos-y)
                     (let ((prevline (nth txtpos-y lines)))
                       (if prevline
                           (progn (setf (nth txtpos-y lines) (concatenate 'string prevline line))
                                  (setf txtpos-x (length prevline)))
                           (progn (setf (nth txtpos-y lines)  line)
                                  (setf txtpos-x 0))))))
               (move-window)))
        (t (let* ((line (nth txtpos-y lines)))
             (setf (nth txtpos-y lines) 
                   (concatenate 'string (subseq line 0 txtpos-x) 
                                (string (car msgs)) (subseq line txtpos-x)))
             (incf txtpos-x))
           (move-window))
					
        (print (format t "scroll: ~a " scroll))))))
	
(defun show-edittext (textobj)
  (let* ((ymax (min (tmax-y (textobj 'win)) (- (length (textobj 'lines)) (textobj 'scroll-y)))))
    (draw-window-border (textobj 'win))
    (dotimes (i ymax)
             (let* ((line  (nth (+ (textobj 'scroll-y) i) (textobj 'lines)))
                    (scroll-x (textobj 'scroll-x)) )
               (setq line (if (< scroll-x (length line))  (subseq line scroll-x) " "))
               (disp-line (textobj 'win) line i)))
    ))

(defun show-cursor (textobj show)
  (let* ( (x (- (textobj 'txtpos-x) (textobj 'scroll-x) ))
          (y  (- (textobj 'txtpos-y) (textobj 'scroll-y) ))
          (line (nth (textobj 'txtpos-y) (textobj 'lines)))
          (w (textobj 'win)))
    (set-cursor (+ (w 'in-x) (* x cwidth))  (+ (w 'in-y) (* y leading)))
    (setf line (if line (concatenate 'string line " ") " "))
    (if show 
        (set-text-color code_col cursor_col) 
        (set-text-color code_col bg_col ))
    (write-text (string (char line (textobj 'txtpos-x))))))


(defun uos:teditor (&optional (args nil) (win (uos:window 0 0 SCR-W SCR-H "Text Editor")) )
  (let* ( (path (when (eq (cadr args) 'path) (car args)))
          (text (cond 
                  ((eq (cadr args) 'text) (list (car args)))
                  ((eq (cadr args) 'symbol) 
					(let ((txt (with-output-to-string (str2) (pprint (eval (car args)) str2))))
						(split-string-to-list (string #\Newline) 
						(with-output-to-string (str) 
							(dotimes (x (length txt))	
								(let ((c (char txt x))) 
									(when (not (or (eq c #\ETX) (eq c #\STX)))
										(princ c str))))))))))
          (lines (if path (read-file path) (if text text (list " "))))
          (txt (uos:texteditdisplay lines win))
          (edittext-disp (lambda () (show-edittext txt) (show-cursor txt t)))
          )
    (win 'title (concatenate 'string "Text Editor: " 
					(cond (path path) 
					((eq (cadr args) 'symbol) (princ-to-string (car args))) 
					(t ""))))
    (lambda (&rest msgs)
      (case (car msgs)
        (up (txt 'up) (edittext-disp))
        (down (txt 'down) (edittext-disp))
        (left (txt 'left) (edittext-disp))
        (right (txt 'right) (edittext-disp))
        (ppage (dotimes (x 5) (txt 'up)) (edittext-disp))
        (npage (dotimes (x 5) (txt 'down)) (edittext-disp))
        (lstart (dotimes (x 5) (txt 'left)) (edittext-disp))
        (lend (dotimes (x 5) (txt 'right)) (edittext-disp))
        (enter (txt 'enter) (edittext-disp))
        (del (txt 'del) (edittext-disp))
        (show (draw-window-border (txt 'win)) (edittext-disp))
        (title (win 'title))
        (save (cond
				(path 
					(when (eq #\y (display-message (list "Press y to save to " path) t)) 
						(with-sd-card (str (subseq path 1) 2) #| remove the forward slash on the path |#
						(dolist (line (txt 'lines)) (princ line str) (terpri str)))))
				((eq (cadr args) 'symbol) 
					(when (eq #\y (display-message (list " Press y to bind to " (string (car args))) t))
						(eval  (read-from-string (with-output-to-string (str) 
						(princ "(defvar " str) (princ (car args) str) (princ " " str)  
						(mapc (lambda (x) (princ x str)) (txt 'lines)) (princ ")" str))))) ))
			(edittext-disp))
        (t (when (printable (code-char lastkey)) (txt (code-char lastkey)) (edittext-disp)))
        ))))

;
; Directory Browser Application
;

#| maybe make dir2 output everything as cons pairs so we dont have to map it |#
(defun map-dir (&optional (dir "")) (mapcar (lambda (x) (if (consp x) x (cons x nil))) (dir2 dir)))

(defun concat-dir (lst) (let ((rev-lst (reverse lst)))  (concat-string-list (cdr rev-lst) "/" (car rev-lst))))

(defun uos:dir-browser (&optional args (win (uos:window 0 0 SCR-W SCR-H "Directory Browser")) )
  (let* ((menu (uos:menu (map-dir) win)) 
         (prev (list ""))
         (search "")
         (state t)
         )
    (lambda (&rest msgs)
      (case state 
        #| save and new input|#
        ((new save folder rename appenddata) 
         (case (car msgs)
           (enter 
            (case state 
              (folder (mkdir (concatenate 'string (concat-dir prev) "/" search)))
              (new (with-sd-card (str (concatenate 'string (concat-dir prev) "/" search) 2) (princ "" str)))
              (rename (rename-file  
						(concatenate 'string (concat-dir prev) "/" (menu 'select-car))
						(concatenate 'string (concat-dir prev) "/" search)))
			  (save nil)
              (appenddata nil))
            (funcall (menu 'win ) 'title (concat-dir prev))
            (menu 'set-opts (map-dir (concat-dir prev)) ) 
            (setf state t))
           (del (setf search (subseq search 0 (- (length search) 1)))
                (win 'title (concatenate 'string (string state) ": " search))
                )
           (t (when (printable (code-char lastkey))
                (setf search (concatenate 'string search (string (code-char lastkey))))
                (win 'title (concatenate 'string (string state) ": " search))
                ))
           )
         (draw-window-border (menu 'win))
         (show-menu menu)	
         )
        #| navigation |#
        (t
         (case (car msgs)
           (up (menu 'up) (show-menu menu) )
           (down (menu 'down) (show-menu menu) )
           (right (when (and (not (eq nil (menu 'select-car))) (eq nil (menu 'select-cdr))) 
                    (push (menu 'select-car) prev)
                    (menu 'set-opts (map-dir (concat-dir prev))) 
                    (funcall (menu 'win ) 'title (concat-dir prev))
                    #|show-menu does't draw the backgroud for whatever reason so we draw it first |#
                    (draw-window-border (menu 'win))					
                    (show-menu menu)))
           (left (when (not (string= (car prev) "")) 
                   (pop prev) 
                   (menu 'set-opts (map-dir (concat-dir prev)) ) 
                   (funcall (menu 'win ) 'title (concat-dir prev))
                   (draw-window-border (menu 'win))					
                   (show-menu menu)))
           (enter (unless (eq nil (menu 'select-cdr))  
                    (progn (wm 'push-result 
                               (list (concatenate 'string (concat-dir prev) "/" (menu 'select-car)) 'path)) 
                           (setf current-window wm) (current-window 'show))))
           (del 
            (when (eq #\y (display-message 
                           (list (concatenate 'string "Press y to delete " (menu 'select-car)) ) t))
              (if (if (eq nil (menu 'select-cdr))
                      (rmdir (concatenate 'string (concat-dir prev) "/" (menu 'select-car)))
                      (sd-file-remove (concatenate 'string (concat-dir prev) "/" (menu 'select-car))))
                  (display-message (list "Deleted!" "" "Press any key to continue") t)
                  (display-message (list "Didnt delete!" "" "Press any key to continue") t)))
            (menu 'set-opts (map-dir (concat-dir prev)) ) 
            (draw-window-border (menu 'win))					
            (show-menu menu))
           (show (draw-window-border (menu 'win)) (show-menu menu))
           (title "Directory Browser")
           (win win)
           (t (when (printable (code-char lastkey))
                (let ((c (code-char lastkey)))
                  (cond 
                    ((eq c #\n) (setf state 'new))
                    ((eq c #\s) (setf state 'save))
                    ((eq c #\f) (setf state 'folder))
                    ((eq c #\a) (setf state 'appenddata))
					((eq c #\r) (setf state 'rename))
                    )
                  (setf search "")
                  (win 'title (concatenate 'string (string state) ": " search))
                  (draw-window-border (menu 'win))
                  (show-menu menu)
                  )))))))))

;
; ulisp Editor Application
;

(defun butlast (lst) (subseq lst 0 (1- (length lst))))
(defun define-atomic-cmd (char name cmd) (push (cons char cmd) *atomic-cmds*))
(defun define-binary-cmd (char name cmd) (push (cons char cmd) *binary-cmds*))

(defun uos:editor (&optional args (win (uos:window 0 0 SCR-W SCR-H "Edit ")) )
  (let* ( *cmds* 
          *atomic-cmds* 
          *binary-cmds* 
		  *temp*
          processed 
          (disp (uos:textdisplay (list "") win))
          cc
          (scroll 0)
          (%edit (lambda (fun)
                   (let ((cmd (car *cmds*)))
                     (cond
                       ((null *cmds*) fun)
                       ((eq cmd #\h) (pop *cmds*) (%edit (cons 'highlight (list fun))))
                       ((consp cmd) (funcall (cdr (assoc (car cmd) *binary-cmds*)) (cdr cmd) fun))
                       ((assoc cmd *atomic-cmds*) (funcall (cdr (assoc cmd *atomic-cmds*)) fun))
                       (t fun)))))

          (name (when (eq (cadr args) 'symbol) (car args) ))
          (fun (if name (eval name) '(lambda ())))
		  (set-name (lambda ()  
						(write-byte 11) (princ "set name:") 
						(setq name (eval 
							(read-from-string (concatenate 'string "(defvar " (string (read)) ")")))) 
						(win 'title (concatenate 'string "Edit: " (string name)))))
          (%show (lambda ()  
                   (setq cc (append cc (list #\h)))
                   (setq *cmds* cc)
                   (setq scroll (disp 'scroll))
                   (disp 'set-lines  (split-string-to-list (string #\Newline)
                                                           (with-output-to-string (str) (pprint (%edit fun) str))))
                   (dotimes (i scroll) (disp 'down))
                   (draw-window-border (disp 'win)) (show-text-hilite disp)
                   (setq cc (butlast cc))))
          )
		
    (win 'title (concatenate 'string "Edit: " (string name)))
		
    (define-atomic-cmd #\b "back"
      #'(lambda (fun) (pop *cmds*) fun))

    (define-atomic-cmd #\d "cdr"
      #'(lambda (fun) (pop *cmds*) (if (atom fun) (%edit fun) (%edit (cons (car fun) (%edit (cdr fun)))))))

    (define-atomic-cmd #\a "car"
      #'(lambda (fun) (pop *cmds*) (if (atom fun) (%edit fun) (%edit (cons (%edit (car fun)) (cdr fun))))))

    (define-atomic-cmd #\x "delete"
      #'(lambda (fun) (pop *cmds*) (if (atom fun) (%edit fun) (%edit (cdr fun)))))

	(define-atomic-cmd #\k "copy"
      #'(lambda (fun) (pop *cmds*) (setf *temp* (if (atom fun) fun (copy-list fun))) (%edit fun)))
	  
	(define-atomic-cmd #\v "paste"
      #'(lambda (fun) (pop *cmds*) (%edit (cons *temp* fun))))
	  
    (define-binary-cmd #\r "replace"
      #'(lambda (val fun) (pop *cmds*) (if (atom fun) (%edit val) (%edit fun))))

    (define-binary-cmd #\c "cons"
      #'(lambda (val fun) (pop *cmds*) (%edit (cons val fun))))
	  
    (define-binary-cmd #\i "insert"
      #'(lambda (val fun) (pop *cmds*) (%edit (cons val fun))))

    (define-binary-cmd #\f "find"
      #'(lambda (val fun)
          (cond
            ((null fun) nil)
            ((equal val fun) (pop *cmds*) (%edit fun))
            ((atom fun) fun)
            (t (cons (%edit (car fun)) (%edit (cdr fun)))))))
		
    (lambda (&rest msgs)
      (case (car msgs)
        (show (%show))
        (up (dotimes (x 3) (disp 'up))(show-text-hilite disp))
        (down (dotimes (x 3) (disp 'down)) (show-text-hilite disp))
        (right (dotimes (x 8) (disp 'down)) (show-text-hilite disp))
        (left (dotimes (x 8) (disp 'up)) (show-text-hilite disp))
        (title (win 'title))
        (t (let ((c (code-char lastkey)))
             (when (printable c)
               (cond
				 ((eq c #\n) (set-name))
                 ((eq c #\q) (setf current-window wm) (current-window 'show))
                 ((eq c #\s) 
					(when (eq #\y (display-message (list "Press y to bind to " (string name)) t))
						(when (not name) (set-name))
						(setq *cmds* cc) (set name (%edit fun))))
                 ((eq c #\z) (when cc (setq cc (butlast cc))))
                 ((assoc c *binary-cmds*)
                  (write-byte 11) (princ c) (princ #\:)
                  (setq cc (append cc (list (cons c (read))))))
                 ((assoc c *atomic-cmds*) (setq cc (append cc (list c))))
                 (t (write-byte 7)))
               (when (not (eq c #\q)) (%show) ))))))))



;
; Window Browser Application
;
;; results can take in a list of lists that represents the previous results			
(defun uos:win-browser (&optional results (win (uos:window 0 0 SCR-W SCR-H "App Browser")) )
  (let* ( (window-index 0)
          (app-win (uos:window 0 0 100 100 "Apps"))
          (app-menu (uos:menu (list 
                               (cons "Directory" uos:dir-browser)
                               (cons "Text Editor" uos:teditor)
                               (cons "FunctionBrowser" uos:doc-browser) 
                               (cons "Edit" uos:editor) 
                               (cons "Text Viewer" uos:text-viewer)
                               (cons "Exit" (lambda (&rest x) (setf exit t) (setf lastkey nil)))
                               ) 
                              app-win))
          (open-win  (uos:window 0 0 100 100 "Open"))
          (open-menu (uos:menu nil open-win))
          (container-win (uos:window  0 0 100 100))
          (results-win (uos:window 0 0 100 100 "Results"))
          (results-menu (uos:menu (cons (when results (car results)) nil) results-win))
          (selected-menu app-menu) 
          )
	
    (windows-split-v app-win container-win .3)
    (windows-split-v results-win open-win .5 container-win)
	
    (lambda (&rest msgs)
      (case (car msgs)
        (up (selected-menu 'up) (show-menu selected-menu))
        (down (selected-menu 'down) (show-menu selected-menu))
        (right (cond 
                 ((eq selected-menu results-menu) 
                  (setf selected-menu open-menu) 
                  (show-menu results-menu) (show-menu app-menu nil) (show-menu open-menu))
                 ((eq selected-menu app-menu) 
                  (setf selected-menu results-menu) 
                  (show-menu results-menu) (show-menu app-menu nil) (show-menu open-menu nil))))
        (left (cond 
                ((eq selected-menu results-menu) 
                 (setf selected-menu app-menu)
                 (show-menu results-menu) (show-menu app-menu) (show-menu open-menu nil))
                ((eq selected-menu open-menu) 
                 (setf selected-menu results-menu) 
                 (show-menu results-menu) (show-menu app-menu nil) (show-menu open-menu nil))))
			
        (del (when (or (eq selected-menu open-menu) (eq selected-menu results-menu))
               (selected-menu 'set-opts (remove-nth (selected-menu 'selected) (selected-menu 'opts)))
               (when (> window-index (- (length (selected-menu 'opts)) 1)) (decf window-index))
               (show-menu selected-menu)))
        (enter 
         (cond 
           ((eq selected-menu app-menu)
            (let* ((new-app (funcall (app-menu 'select-cdr) 
                                     (when (results-menu 'opts) (results-menu 'select)))))
						
              #| EXIT HERE WHEN WE CALL THE EXIT APP |#
              (when exit (fill-screen) (return (wm 'results)))
						
              (open-menu 'push-opts (cons (new-app 'title) new-app)))
            (setf window-index 0)
            (setf current-window (open-menu 'nth-cdr window-index))
            (current-window 'show))
           ((eq selected-menu open-menu)
            (when (open-menu 'opts)
              (setf window-index (open-menu 'selected))
              (setf current-window (open-menu 'nth-cdr window-index))
              (current-window 'show)))))
						
        (push-result (results-menu 'push-opts (cadr msgs)))
        (results (results-menu 'opts))
        (current-window current-window)
        (set-current-window (setf (cadr msgs) current-window))
        (show (draw-window-border win) (show-menu results-menu) (show-menu app-menu)  (show-menu open-menu nil))
        )
      )
    ))

;
; Main Function 
;
;;; MAKE RESULTS REUSE WORK?
(defun uos (&optional results)
  (with-gfx (scr)
  (let* (
		 (write-text (lambda (str) (princ str scr)))
		 (wm (uos:win-browser results))
		 (current-window wm)
		 (lastkey nil) (exit nil)
		 )
    (current-window 'show)
    (loop
     (setf lastkey (keyboard-get-key))
     (when lastkey 
       (case lastkey
         #|get back to window manager menu touchscreen + space|#
         (9  (setf current-window wm) (current-window 'show))
				
         #|current window controls |#
         (218 (current-window 'up))
         (217 (current-window 'down))
         (216 (current-window 'left))
         (215 (current-window 'right))
         (211 (current-window 'ppage))
         (214 (current-window 'npage))
         (210 (current-window 'lstart))
         (213 (current-window 'lend))
         ((or 13 10) (current-window 'enter))
         ((or 3 17)  (setf exit t) (setf lastkey nil))
         ((or 8 127) (current-window 'del))
         (203 (current-window 'save))
         (204 (current-window 'load))
         (24 (current-window 'new))
         (t  (current-window lastkey))
         )
       )
     (when exit (fill-screen) (return (wm 'results)))
     )
    )))

;
; Helper functions
;
;

(defun split-string-to-list (delim str)
  (unless (or (eq str nil) (not (stringp str))) 
    (let* ((start 0)
           (end (search-str delim str))
           (lst nil))
      (loop
       (if (eq end nil) 
           (return (append lst (list (subseq str start))))
           (setq lst (append lst (list (subseq str start end)))))
       (setq start (1+ end))
       (setq end (search-str delim str start))))))

(defun split-line (str len)
  (let ((index 0) (lines nil))
    (loop
     (if (> (length str) (+ len index)) 
         (setf lines (append lines (list (subseq str index (+ index len)))))
         (return (append lines (list (subseq str index)))))
     (incf index len))))


(defun concat-string-list (lst &optional (sep "") (starting "") ) 
  "(concat-string-list lst [sep] [starting])
concatenates strings in lst with the starting string, seperated by sep"
  (if (not lst)
      starting
      (concat-string-list (cdr lst) sep (concatenate 'string starting sep (car lst)) )))

(defun printable (chr)
  "(printable chr) 
returns t if the character is printable, nil otherwise"
  (if (and (> (char-code chr) 31) (< (char-code chr) 127)) 
      t
      nil))

(defun remove-nth (n lst)
  "(remove-nth n lst) 
returns a list with the nth element removed"
  (if (> n (length lst))
      lst
      (append (subseq lst 0 n)
              (subseq lst (1+ n)))))
			
(defun read-file (filepath)	
  (let ((line-list nil) (line ""))
    (with-sd-card (str filepath 0)
      (loop
       (setf line (read-line str))
       (if line 
           (push line line-list)
           (if line-list 
               (return (reverse line-list))
               (return (list " ")))
           )))))



)lisplibrary";
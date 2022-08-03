;;; gph-mode.el --- major mode for gph files  -*- lexical-binding: t; -*-

;; Copyright (C) Troels Henriksen (athas@sigkill.dk) 2022
;;
;; URL: https://github.com/diku-dk/futhark-mode
;; Keywords: gopher
;; Version: 1.0
;; Package-Requires: ((emacs "25.1"))

;; This file is not part of GNU Emacs.

;;; License:
;; GPL-3+

;;; Commentary:
;; .gph is the map file format used by the geomyidae Gopher daemon.
;; This Emacs mode provides basic understanding of the link syntax,
;; such that highlighting and folding works properly.
;;
;; Files with the ".gph" extension are automatically handled by this mode.
;;
;; For extensions: Define local keybindings in `gph-mode-map'.  Add
;; startup functions to `gph-mode-hook'.

;;; Code:

(eval-when-compile
  (require 'rx))

(defvar gph--font-lock-defaults
  (let* ((type-rx '(or "0" "1" "3" "7" "8" "9" "g" "I" "h" "i"))
         (desc-rx '(* (not "|")))
         (path-rx '(* (not "|")))
         (host-rx '(* (not "|")))
         (port-rx '(+ digit))
         (link-rx `(: line-start "[" ,type-rx "|" ,desc-rx "|" ,path-rx "|" ,host-rx "|" ,port-rx "]"))
         (badlink-rx `(: line-start "[" (* anything))))
    `((,(rx-to-string link-rx) 0 font-lock-doc-markup-face)
      (,(rx-to-string badlink-rx) 0 font-lock-warning-face))))

(defvar gph-mode-hook nil
  "Hook for `gph-mode'.  Is run whenever the mode is entered.")

(defvar gph-mode-map
  (let ((map (make-keymap)))
    map)
  "Keymap for `gph-mode'.")

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.gph" . gph-mode))

;;;###autoload
(define-derived-mode gph-mode text-mode "gph"
  "Major mode for .gph files as used by geomyidae."
  (setq-local paragraph-start (concat "^\\[|\\|[ \t]*$\\|" page-delimiter))
  (setq-local paragraph-separate (concat "^\\[\\|[ \t]*$\\|" page-delimiter))
  (setq-local font-lock-defaults '(gph--font-lock-defaults)))

(provide 'gph-mode)

;;; gph-mode.el ends here

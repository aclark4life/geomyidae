# gph format

## vim
* Here you find syntax highlighting for gph files for vim.
	* Thanks dive on #gopherproject for contributing this!

### Installation

	cp vim/ftdetect/gph.vim ~/.vim/ftdetect
	cp vim/syntax/gph.vim ~/.vim/syntax

## emacs

### Installation

Add the following to your Emacs configuration file.

	(add-to-list 'load-path (concat user-emacs-directory "path/to/dir/with/gph-mode.el"))
	(require 'gph-mode)

For additional verbosity visit:

	https://www.gnu.org/software/emacs/manual/html_node/elisp/Auto-Major-Mode.html


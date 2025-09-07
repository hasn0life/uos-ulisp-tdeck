# uos-ulisp-tdeck
Operating system/window manager for the Lilygo T-Deck written in uLisp

## How to install
Do all the things needed to set up the [t-deck](http://www.ulisp.com/show?4JAO). The t-deck library contains the [SensorsLib](https://github.com/Xinyuan-LilyGO/T-Deck/tree/master/lib/SensorsLib) folder which contains the touchscreen drivers. Move that folder into the arduino library folder. If you're using the t-deck files from this repo you can skip the following steps, but if you're modifying the original [t-deck file](https://github.com/technoblogy/ulisp-tdeck) then

Add the lisplibrary.h and extensions.ino files to the folder which contains [ulisp-tdeck.ino](https://github.com/technoblogy/ulisp-tdeck). Then follow the setup instructions for [extensions](http://www.ulisp.com/show?19Q4) and [lisplibrary](http://www.ulisp.com/show?27OV) to enable both features. Add the `initTouch();` and `inittrackball();` functions to the `setup()` function in `ulisp-tdeck.ino` to enable the touchscreen and trackball.

## Usage
Thanks to innovative usage of the [touchscreen as a modifier for the t-decks keyboard output](https://github.com/hasn0life/ulisp-tdeck-touch-example) we can use uos without having to reprogram the [T-deck's keyboard](https://github.com/hasn0life/t-deck-keyboard-ex). Also the trackball is used to move the cursor around. 

## uos
### Window Manager
The window manager contains three menus: the applications (Apps), Results, and open windows (Open). Moving the trackball left and right switches between these menus and up and down selects different items. The Apps menu lets you pick applications to run. The `enter/newline` key opens the applications (note that pressing the trackball doesn't do anything). When you're in an application the controls become specific to that application but generally the trackball either scrolls or moves the cursor. `delete` lets you close open windows in the Open menu or remove results from the Results menu.

To leave the appliction hold the `touchscreen` and press the `space` key. This brings you back to the window manager, but now the application you opened should be shown in the Open menu. To go back to it, move the cursor to that menu and press `enter`. 

Applications can also produce results, which show up in the results menu. There is always a selected result. The result can be used as the input into the next application you select, however not all results work with all applications. There are different types of results like file paths (`'path`), ulisp object symbol (`'symbol`), single line text (`'text`), and multiline text (`'lines`), and more can be added. The type of the result isn't shown in the menu, but we should work on that.... 

## Applications
### Directory
(returns `'path`)

The directory app lets you select files on the SD card. You can navigate with the trackball, left and right let you access and leave folders. `Enter` pushes the selected file as a result and brings you back to the window manager. Pressing `n` lets you create a new file, `f` lets you create a new folder. 'r' lets you rename the selected file. The `delete` key deletes the selected folder. Note that folders with files inside them can't be deleted, you have to delete all the files inside first. 

### Text Editor
(takes `'path`, `'text`, `'symbol` or `'lines`, should return `'lines` but doesnt yet)

Lets you edit text. The trackball moves the cursor, holding the touchscreen lets you move the cursor accross more characters. Typing on the keyboard inserts the characters you typed. `enter` creates a new line, and `delete` deletes character. `Touchscreen + s` saves the text to the currently opened file (it also crashes if we havent selected a file rn). If a symbol is open `Touhscreen + s` binds the symbol. 

### Function Browser
(returns `'symbol`)

Lets you browse the currently defined uLisp functions and displays their documentation on the side. `enter` pushes the selected symbol into results. Typing will filter the functions shown. 

### Edit
(takes in `'symbol`)

Lets you edit uLisp functions, its operations are described in http://forum.ulisp.com/t/extensible-t-deck-lisp-editor/1322 The trackball lets you scroll the window.
New function `n` lets you rename the symbol. `k` lets you copy and `v` lets you paste

### Text Viewer
(takes `'path`, `'text`, `'symbol` or `'lines`)

Lets you view the text. Trackball scrolls the text up and down.

### Exit
Exits uos. You can also exit uos by using `touchscreen + c`

## Known Issues/TODO
All:
 - scroll bars
 - line numbers
 - parenthesis highlighting
 - copy paste
 - autocomplete
 - show result type
 - ~~slightly faster rendering~~

directory browser:
 - ~~rename files~~
 - highlight folders
 - going back through folders should remember position?

Edit:
 - ~~create new functions~~
 - ~~copy and paste~~

text editor:
 - ~~open and bind symbols~~
 - change save directories
 - put out text as 'lines result
 - allow saving of new files

Misc:
 - port to cardputer and Picocalc

## Thanks to/Credit
Hartmut Grawe - github.com/ersatzmoco

David Johnson-Davies http://forum.ulisp.com/t/extensible-t-deck-lisp-editor/1322 and [uLisp](http://www.ulisp.com) itself

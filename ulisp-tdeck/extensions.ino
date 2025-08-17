/*
  LispBox uLisp Extension - Version 1.0 - June 2024
  Hartmut Grawe - github.com/ersatzmoco - June 2024

  edited by hasn0life for Lilygo T-Deck - Jan 2025

  Licensed under the MIT license: https://opensource.org/licenses/MIT
*/

// Definitions


#define touchscreen

#if defined(touchscreen)
#include "TouchDrvGT911.hpp"
TouchDrvGT911 touch;
#endif

#define TDECK_TOUCH_INT     16

#define TDECK_TRACKBALL_UP 3
#define TDECK_TRACKBALL_DOWN 15
#define TDECK_TRACKBALL_LEFT 1
#define TDECK_TRACKBALL_RIGHT 2

volatile int ball_val = 0;

void initTouch(){
  #if defined (touchscreen)
  pinMode(TDECK_TOUCH_INT, INPUT);
  touch.setPins(-1, TDECK_TOUCH_INT);\
  //keyboard already initialized the I2C?
  if (!touch.begin(Wire1, GT911_SLAVE_ADDRESS_L)) {
    while (1) {
      Serial.println("Failed to find GT911 - check your wiring!");
      delay(1000);
    }
  }
  // Set touch max xy
  touch.setMaxCoordinates(320, 240);
  // Set swap xy
  touch.setSwapXY(true);
  // Set mirror xy
  touch.setMirrorXY(false, true);
  #endif
}

void ISR_trackball_up(){
  //if(ball_val == 0){
    ball_val = 218;
  //}
}
void ISR_trackball_down(){
  //if(ball_val == 0){
    ball_val = 217;
  //}
}
void ISR_trackball_left(){
  //if(ball_val == 0){
    ball_val = 216;
  //}
}
void ISR_trackball_right (){
  //if(ball_val == 0){
    ball_val = 215;
  //}
}
void inittrackball(){
  pinMode(TDECK_TRACKBALL_UP, INPUT_PULLUP);
  pinMode(TDECK_TRACKBALL_DOWN, INPUT_PULLUP);
  pinMode(TDECK_TRACKBALL_LEFT, INPUT_PULLUP);
  pinMode(TDECK_TRACKBALL_RIGHT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_UP), ISR_trackball_up, FALLING);
  attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_DOWN), ISR_trackball_down, FALLING);
  attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_LEFT), ISR_trackball_left, FALLING);
  attachInterrupt(digitalPinToInterrupt(TDECK_TRACKBALL_RIGHT), ISR_trackball_right, FALLING);
}

object *fn_get_touch_points (object *args, object *env) {
  #if defined(touchscreen)
  int16_t x[5], y[5];
  uint8_t touched = 0;
  object *result = nil;
  do {
    touched = touch.getPoint(x, y, touch.getSupportTouchPoint());
    if (touched > 0) {
      //start from the end of the list so we dont have to reverse it
      for (int i = touched; i > 0; --i) {
        result = cons(cons(number(x[i-1]), number(y[i-1])), result);
      }
    }
  } while(touch.isPressed());
  return result;

  #else
  return nil;
  #endif
}

bool isScreenTouched(){
  bool received_touch = false;
  //clear any previous readings since it buffers those
  do {
    int16_t x[5], y[5];
    uint8_t touched = touch.getPoint(x, y, touch.getSupportTouchPoint());
  } while(touch.isPressed());

  // touch.ispressed() will trigger like 5 times if you press it once so we have to loop through it and get the touchpoints
  do {
    int16_t x[5], y[5];
    uint8_t touched = touch.getPoint(x, y, touch.getSupportTouchPoint());
    if (touched > 0) {
      received_touch = true;
    }
  } while(touch.isPressed());

  return received_touch;
}


// T-Deck extras
char touchKeyModEditor(char temp){
  #if defined (touchscreen)
 /* t-deck / blackberry keyboard missing symbols
    missing mapped	alt symbol
    `       k       ' 
    ~       p       @ 
    %       $       
    ^       a       * 
    &       q       # 
    =       o       + 
    <       t       ( 
    >       y       ) 
    \       u       _ 
    |       g       / 
            
    [       alt-t   (
    ]       alt-y   )
            
    {       n/a
    }       n/a
    tab  	space

    while holding the touch screen
    c --- quit editor and return to REPL
    n --- discard current text buffer (i.e. new file)
    backspace --- delete line starting at cursor position
    trackball left --- move cursor to start of line
    trackball right --- move cursor to end of line
    ^ --- move cursor to beginning of buffer
    trackball up / trackball down --- move one page up or down

    Fn-h --- help menu
    Fn-( --- toggle bracket matching on/off
    Fn-) --- check if bracket under the cursor has a matching bracket in the buffer. If so, they are temporarily highlighted. (Use when continuous bracket matching is off.)
    Fn-b --- bind contents of the text buffer to a symbol of your choice and quit editor
    Fn-d --- delete a file on the SD card
    Fn-s --- save text buffer to SD card
    Fn-l --- load text from SD card into buffer, discarding the present one
    Fn-i --- show directory of SD card

  */

  if (isScreenTouched()) {
    if (temp == 'k')      return '`';
    else if (temp == 'p') return '~';
    else if (temp == '$') return '%';
    else if (temp == 'a') return '^';
    else if (temp == 'q') return '&';
    else if (temp == 'o') return '=';
    else if (temp == 't') return '<';
    else if (temp == 'y') return '>';
    else if (temp == 'u') return '\\';
    else if (temp == 'g') return '|';
    else if (temp == '(') return '[';
    else if (temp == ')') return ']';
    else if (temp == ' ') return '\t';

    else if  (temp == 'c') return (char)17; //quit
    else if (temp == 'n') return (char)24; //new
    else if (temp == 8) return (char)12; //delete line
    else if (temp == '*') return (char)94; //beginning
    else if (temp == 'h') return (char)16;  //help
    else if (temp == 's') return (char)203; //save
    else if (temp == 'l') return (char)204; //load
    else if (temp == 'd') return (char)202; //delete
    else if (temp == 'b') return (char)198; //bind
    else if (temp == 'i') return (char)205; //show dir
    else if (temp == '1') return (char)194; //toggle bracket
    else if (temp == '2') return (char)195; //highlight

  }
  #else
  if (temp == '@') temp = '~';
  if (temp == '_') temp = '\\';
  #endif
  return temp;
}

object *fn_KeyboardGetKey (object *args, object *env) {
  (void) env, (void) args;
  Wire1.requestFrom(0x55, 1);
  if (Wire1.available()){
    char temp = Wire1.read();
    if ((temp != 0) && (temp !=255)){
      temp = touchKeyModEditor(temp);
      //Serial.println((int)temp);
      return number(temp);
    }
  }
  if(ball_val != 0){
    int temp = ball_val;
    ball_val = 0;
    if (isScreenTouched()) {
      // ((or 1 210) (se:linestart))
      // ((or 5 213) (se:lineend))
      // (211 (se:prevpage))
      // (214 (se:nextpage))
      switch(temp){
        case 218: temp = 211; break; //up -> prevpage
        case 217: temp = 214; break; //down -> nextpage
        case 216: temp = 210; break; //left -> linestart
        case 215: temp = 213; break; //right -> lineend
      }
    }
    return number(temp);
  }
  return nil;
}

/*
  (keyboard-flush)
  Discard missing key up/down events.
*/
object *fn_KeyboardFlush (object *args, object *env) {
  (void) args, (void) env;
  return nil;
}



object *fn_searchstr (object *args, object *env) {
  (void) env;
  
  int startpos = 0;
  object *pattern = first(args);
  object *target = second(args);
  args = cddr(args);
  if (pattern == NULL) return number(0);
  else if (target == NULL) return nil;
  if (args != NULL) startpos = checkinteger(car(args));
  
if (stringp(pattern) && stringp(target)) {
    int l = stringlength(target);
    int m = stringlength(pattern);
    if (startpos > l) error2(indexrange);
    for (int i = startpos; i <= l-m; i++) {
      int j = 0;
      while (j < m && nthchar(target, i+j) == nthchar(pattern, j)) j++;
      if (j == m) return number(i);
    }
    return nil;
  } else error2("arguments are not both lists or strings");
  return nil;
}

object *fn_searchn (object *args, object *env) {
  (void) env;
  int matches = 0;
  int last_index = 0;
  object *pattern = first(args);
  object *target = second(args);
  if (cddr(args) != NULL){ 
    object *num = third(args);
    if(integerp(num)){
      matches = num->integer;
    }
  }
  if (pattern == NULL) return number(0);
  else if (target == NULL) return nil;
  else if (listp(pattern) && listp(target)) {
    int l = listlength(target);
    int m = listlength(pattern);
    for (int i = 0; i <= l-m; i++) {
      object *target1 = target;
      while (pattern != NULL && eq(car(target1), car(pattern))) {
        pattern = cdr(pattern);
        target1 = cdr(target1);
      }
      if (pattern == NULL){ 
        last_index = i;
        if(matches-- == 0){
          return number(i);
        }
      }
      pattern = first(args); target = cdr(target);
    }
    if(last_index > 0){
      return number(last_index);
    }
    return nil;
  } else if (stringp(pattern) && stringp(target)) {
    int l = stringlength(target);
    int m = stringlength(pattern);
    for (int i = 0; i <= l-m; i++) {
      int j = 0;
      while (j < m && nthchar(target, i+j) == nthchar(pattern, j)) j++;
      if (j == m){ 
        last_index = i;
        if(matches-- == 0){
          return number(i);
        }
      }
    }
    if(last_index > 0){
      return number(last_index);
    }
    return nil;
  } else error2(PSTR("arguments are not both lists or strings"));
  return nil;
}

#if defined sdcardsupport
/*
  (sd-file-exists filename)
  Returns t if filename exists on SD card, otherwise nil.
*/
object *fn_SDFileExists (object *args, object *env) {
  (void) args, (void) env;

  SD.begin(TDECK_SDCARD_CS);

  int slength = stringlength(checkstring(first(args)))+1;
  char *fnbuf = (char*)malloc(slength);
  cstring(first(args), fnbuf, slength);

  if (SD.exists(fnbuf)) {
    return tee;
  }
  else {
    return nil;
  }
}

/*
  (sd-file-remove filename)
  Returns t if filename exists on SD card, otherwise nil.
*/
object *fn_SDFileRemove (object *args, object *env) {
  (void) args, (void) env;

  SD.begin(TDECK_SDCARD_CS);
  int slength = stringlength(checkstring(first(args)))+1;
  char *fnbuf = (char*)malloc(slength);
  cstring(first(args), fnbuf, slength);

  if (SD.exists(fnbuf)) {
    SD.remove(fnbuf);
    return tee;
  }
  else {
    return nil;
  }
}

//Create a directory on the SD card. 
//This will also create any intermediate directories that don’t already exists; 
//e.g. SD.mkdir("a/b/c") will create a, b, and c.
object *fn_SDmkdir (object *args, object *env) {
  (void) args, (void) env;

  SD.begin(TDECK_SDCARD_CS);

  int slength = stringlength(checkstring(first(args)))+1;
  char *fnbuf = (char*)malloc(slength);
  cstring(first(args), fnbuf, slength);

  if (SD.mkdir(fnbuf)) {
    return tee;
  }
  else {
    return nil;
  }
}

//Remove a directory from the SD card. The directory must be empty.
object *fn_SDrmdir (object *args, object *env) {
  (void) args, (void) env;

  SD.begin(TDECK_SDCARD_CS);

  int slength = stringlength(checkstring(first(args)))+1;
  char *fnbuf = (char*)malloc(slength);
  cstring(first(args), fnbuf, slength);

  if (SD.rmdir(fnbuf)) {
    return tee;
  }
  else {
    return nil;
  }
}

object *fn_directory2(object *args, object *env) {
  (void) env;
  char *sd_path_buf = NULL; 

  SDBegin();
  File root; 
  object *result = cons(NULL, NULL);
  object *ptr = result;
  
  if (args != NULL) {
    object *arg1 = checkstring(first(args));
    int len = stringlength(arg1) + 2; //make it longer for the initial slash and the null terminator
    sd_path_buf = (char*)malloc(len); 
    if(sd_path_buf != NULL){
      cstring(arg1, &sd_path_buf[1], len-1);
      sd_path_buf[0] = '/';  //really weird way to add a slash at the front...
      root = SD.open(sd_path_buf);
    }
  }
  else{
    root = SD.open("/");
  }

  while (true) {
    File entry =  root.openNextFile();
    if (!entry) break; // no more files
    object *filename = lispstring((char*)entry.name());
    if (entry.isDirectory()) {
      cdr(ptr) = cons(filename, NULL);
    }else{
      cdr(ptr) = cons(cons(filename, number(entry.size())), NULL);
    }
    ptr = cdr(ptr);
    entry.close();
  }
  
  if(sd_path_buf != NULL) free(sd_path_buf);
  root.close();
  return cdr(result);
}

//from https://github.com/nanomonkey/ulisp-tdeck/blob/edits/ulisp-extensions.ino
object *fn_rename_file(object *args, object *env) {
  (void) env;
  SDBegin();
  object *path1 = first(args);
  object *path2 = second(args);
  char buffer1[BUFFERSIZE];
  char buffer2[BUFFERSIZE];
  if (SD.rename(MakeFilename(path1, buffer1), MakeFilename(path2, buffer2))) {
    return path2;
  }
  else { 
    error2(PSTR("Unable to rename file."));
    return nil;
  }
}

#endif


void SDWriteTwo (File file, uint16_t word) {
  file.write(word & 0xFF); file.write((word >> 8) & 0xFF);
}

// Symbol names
const char string_gettouchpoints[] PROGMEM = "get-touch-points";
const char stringKeyboardGetKey[] PROGMEM = "keyboard-get-key";
const char stringKeyboardFlush[] PROGMEM = "keyboard-flush";
const char stringSearchStr[] PROGMEM = "search-str";
const char stringsearchn[] PROGMEM = "searchn";

#if defined sdcardsupport
const char stringSDFileExists[] PROGMEM = "sd-file-exists";
const char stringSDFileRemove[] PROGMEM = "sd-file-remove";
const char string_rename_file[] PROGMEM = "rename-file";

const char stringDir2[] PROGMEM = "dir2";
const char stringmkdir[] PROGMEM = "mkdir";
const char stringrmdir[] PROGMEM = "rmdir";
#endif


// Documentation strings
const char doc_gettouchpoints[] PROGMEM = "(get-touch-points)\n"
"Returns all the points being touched on the screen in a list of x,y pairs or an empty list";

const char docKeyboardGetKey[] PROGMEM = "(keyboard-get-key [pressed])\n"
"Get key last recognized - default: when released, if [pressed] is t: when pressed).";
const char docKeyboardFlush[] PROGMEM = "(keyboard-flush)\n"
"Discard missing key up/down events.";

const char docSearchStr[] PROGMEM = "(search-str pattern target [startpos])\n"
"Returns the index of the first occurrence of pattern in target, or nil if it's not found\n"
"starting from startpos";

const char docsearchn[] PROGMEM = "(searchn pattern target [n])\n"
"Returns the index of the nth occurrence of pattern in target,\n"
"which can be lists or strings, or nil if it's not found.\n"
"if the pattern occured more than once but less than n times, it returns the last occuring index";


#if defined sdcardsupport
const char docSDFileExists[] PROGMEM = "(sd-file-exists filename)\n"
"Returns t if filename exists on SD card, otherwise nil.";
const char docSDFileRemove[] PROGMEM = "(sd-file-remove filename)\n"
"Delete file with filename. Returns t if successful, otherwise nil.";
const char doc_rename_file[] PROGMEM = "(rename-file filefrom fileto)" 
"renames file from old filepath to new, can be used to move a file";

const char docDir2[] PROGMEM = "(dir2 [directory])\n"
"returns a list of filenames in the root or certain directory";

const char docmkdir[] PROGMEM = "(mkdir directory)\n"
"Create a directory with the specified name. Returns t if successful, otherwise nil.";

const char docrmdir[] PROGMEM = "(rmdir directory)\n"
"Delete specified directory. Returns t if successful, otherwise nil.";
#endif



// Symbol lookup table
const tbl_entry_t lookup_table2[] PROGMEM = {
  { string_gettouchpoints, fn_get_touch_points, 0200, doc_gettouchpoints },

  { stringKeyboardGetKey, fn_KeyboardGetKey, 0201, docKeyboardGetKey },
  { stringKeyboardFlush, fn_KeyboardFlush, 0200, docKeyboardFlush },
  { stringSearchStr, fn_searchstr, 0224, docSearchStr },

  { stringsearchn, fn_searchn, 0223, docsearchn },

#if defined sdcardsupport
  { stringSDFileExists, fn_SDFileExists, 0211, docSDFileExists },
  { stringSDFileRemove, fn_SDFileRemove, 0211, docSDFileRemove },
  { string_rename_file, fn_rename_file, 0222, doc_rename_file },

  { stringDir2, fn_directory2, 0201, docDir2 },
  { stringmkdir, fn_SDmkdir, 0211, docmkdir },
  { stringrmdir, fn_SDrmdir, 0211, docrmdir },
#endif

};

// Table cross-reference functions

tbl_entry_t *tables[] = {lookup_table, lookup_table2};
const unsigned int tablesizes[] = { arraysize(lookup_table), arraysize(lookup_table2) };

const tbl_entry_t *table (int n) {
  return tables[n];
}

unsigned int tablesize (int n) {
  return tablesizes[n];
}
/* uLisp T-Deck Release 4.7a - www.ulisp.com
   David Johnson-Davies - www.technoblogy.com - 25th November 2024

   Licensed under the MIT license: https://opensource.org/licenses/MIT
*/

// Lisp Library
// const char LispLibrary[] = "";

// Compile options

// #define resetautorun
#define printfreespace
#define serialmonitor
// #define printgcs
#define sdcardsupport
#define gfxsupport
#define lisplibrary
#define extensions

// Includes

#include "LispLibrary.h"
#include <setjmp.h>
#include <SPI.h>
#include <Wire.h>
#include <limits.h>
#include <WiFi.h>
#include "soc/periph_defs.h" // Not sure why necessary
#include <I2S.h>
#include <TFT_eSPI.h>

#define COLOR_WHITE 0xFFFF
#define COLOR_BLACK 0x0000
#define COLOR_GREEN 0x07e0
#define TDECK_PERI_POWERON 10
#define TDECK_SPI_MOSI 41
#define TDECK_SPI_MISO 38
#define TDECK_SPI_SCK 40
#define TDECK_TFT_CS 12
#define TDECK_TFT_DC 11
#define TDECK_TFT_BACKLIGHT 42
#define TDECK_SDCARD_CS 39
#define TDECK_I2C_SDA 18
#define TDECK_I2C_SCL 8
#define TDECK_LORA_CS 9
#define TDECK_KEYBOARD_INT 46
#define GFX_DEV_DEVICE LILYGO_T_DECK
#define TFT_BACKLITE TDECK_TFT_BACKLIGHT


TFT_eSPI tft;

#if defined(sdcardsupport)
  #include <SD.h>
#endif

// Platform specific settings

#define WORDALIGNED __attribute__((aligned (4)))
#define BUFFERSIZE 36  // Number of bits+4

#if defined(ARDUINO_ESP32S3_DEV)
  #if defined(BOARD_HAS_PSRAM)
  #define WORKSPACESIZE 1000000        /* Cells (8*bytes) */
  #else
  #define WORKSPACESIZE 25000          /* Cells (8*bytes) */
  #endif
  #define MAX_STACK 6500
  #define LITTLEFS
  #include <LittleFS.h>
#else
#error "Board not supported!"
#endif

// C Macros

#define nil                NULL
#define car(x)             (((object *) (x))->car)
#define cdr(x)             (((object *) (x))->cdr)

#define first(x)           car(x)
#define rest(x)            cdr(x)
#define second(x)          first(rest(x))
#define cddr(x)            cdr(cdr(x))
#define third(x)           first(cddr(x))

#define push(x, y)         ((y) = cons((x),(y)))
#define pop(y)             ((y) = cdr(y))

#define protect(y)         push((y), GCStack)
#define unprotect()        pop(GCStack)

#define integerp(x)        ((x) != NULL && (x)->type == NUMBER)
#define floatp(x)          ((x) != NULL && (x)->type == FLOAT)
#define symbolp(x)         ((x) != NULL && (x)->type == SYMBOL)
#define stringp(x)         ((x) != NULL && (x)->type == STRING)
#define characterp(x)      ((x) != NULL && (x)->type == CHARACTER)
#define arrayp(x)          ((x) != NULL && (x)->type == ARRAY)
#define streamp(x)         ((x) != NULL && (x)->type == STREAM)

#define mark(x)            (car(x) = (object *)(((uintptr_t)(car(x))) | MARKBIT))
#define unmark(x)          (car(x) = (object *)(((uintptr_t)(car(x))) & ~MARKBIT))
#define marked(x)          ((((uintptr_t)(car(x))) & MARKBIT) != 0)
#define MARKBIT            1

#define setflag(x)         (Flags = Flags | 1<<(x))
#define clrflag(x)         (Flags = Flags & ~(1<<(x)))
#define tstflag(x)         (Flags & 1<<(x))

#define issp(x)            (x == ' ' || x == '\n' || x == '\r' || x == '\t')
#define isbr(x)            (x == ')' || x == '(' || x == '"' || x == '#' || x == '\'')
#define fntype(x)          (getminmax((uint16_t)(x))>>6)
#define longsymbolp(x)     (((x)->name & 0x03) == 0)
#define longnamep(x)       (((x) & 0x03) == 0)
#define twist(x)           ((uint32_t)((x)<<2) | (((x) & 0xC0000000)>>30))
#define untwist(x)         (((x)>>2 & 0x3FFFFFFF) | ((x) & 0x03)<<30)
#define arraysize(x)       (sizeof(x) / sizeof(x[0]))
#define stringifyX(x)      #x
#define stringify(x)       stringifyX(x)
#define PACKEDS            0x43238000
#define BUILTINS           0xF4240000
#define ENDFUNCTIONS       0x0BDC0000

// Constants

#define TRACEMAX 3  // Maximum number of traced functions
enum type { ZZERO=0, SYMBOL=2, CODE=4, NUMBER=6, STREAM=8, CHARACTER=10, FLOAT=12, ARRAY=14, STRING=16, PAIR=18 };  // ARRAY STRING and PAIR must be last
enum token { UNUSED, BRA, KET, QUO, DOT };
enum stream { SERIALSTREAM, I2CSTREAM, SPISTREAM, SDSTREAM, WIFISTREAM, STRINGSTREAM, GFXSTREAM };
enum fntypes_t { OTHER_FORMS, TAIL_FORMS, FUNCTIONS, SPECIAL_FORMS };

// Stream names used by printobject
const char serialstream[] = "serial";
const char i2cstream[] = "i2c";
const char spistream[] = "spi";
const char sdstream[] = "sd";
const char wifistream[] = "wifi";
const char stringstream[] = "string";
const char gfxstream[] = "gfx";
const char *const streamname[] = {serialstream, i2cstream, spistream, sdstream, wifistream, stringstream, gfxstream};

// Typedefs

typedef uint32_t symbol_t;
typedef uint32_t builtin_t;
typedef uint32_t chars_t;

typedef struct sobject {
  union {
    struct {
      sobject *car;
      sobject *cdr;
    };
    struct {
      unsigned int type;
      union {
        symbol_t name;
        int integer;
        chars_t chars; // For strings
        float single_float;
      };
    };
  };
} object;

typedef object *(*fn_ptr_type)(object *, object *);
typedef void (*mapfun_t)(object *, object **);

typedef const struct {
  const char *string;
  fn_ptr_type fptr;
  uint8_t minmax;
  const char *doc;
} tbl_entry_t;

typedef int (*gfun_t)();
typedef void (*pfun_t)(char);

enum builtins: builtin_t { NIL, TEE, NOTHING, OPTIONAL, FEATURES, INITIALELEMENT, ELEMENTTYPE, TEST, COLONA, COLONB,
COLONC, BIT, AMPREST, LAMBDA, LET, LETSTAR, CLOSURE, PSTAR, HIGHLIGHT, QUOTE, DEFUN, DEFVAR, EQ, CAR,
FIRST, CDR, REST, NTH, AREF, CHAR, STRINGFN, PINMODE, DIGITALWRITE, ANALOGREAD, REGISTER, FORMAT, 
 };

// Global variables

#if defined(BOARD_HAS_PSRAM)
object *Workspace WORDALIGNED;
#else
object Workspace[WORKSPACESIZE] WORDALIGNED;
#endif

jmp_buf toplevel_handler;
jmp_buf *handler = &toplevel_handler;
unsigned int Freespace = 0;
object *Freelist;
unsigned int I2Ccount;
unsigned int TraceFn[TRACEMAX];
unsigned int TraceDepth[TRACEMAX];
builtin_t Context;
#define BACKTRACESIZE 8
uint8_t TraceStart = 0, TraceTop = 0;
symbol_t Backtrace[BACKTRACESIZE];

object *GlobalEnv;
object *GCStack = NULL;
object *GlobalString;
object *GlobalStringTail;
int GlobalStringIndex = 0;
uint8_t PrintCount = 0;
uint8_t BreakLevel = 0;
char LastChar = 0;
char LastPrint = 0;
void* StackBottom;

// Flags
enum flag { PRINTREADABLY, RETURNFLAG, ESCAPE, EXITEDITOR, LIBRARYLOADED, NOESC, NOECHO, MUFFLEERRORS, BACKTRACE };
typedef uint16_t flags_t;
volatile flags_t Flags = 1<<PRINTREADABLY; // Set by default

// Forward references
object *tee;
void pfstring (const char *s, pfun_t pfun);

// Error handling

int modbacktrace (int n) {
  return (n+BACKTRACESIZE) % BACKTRACESIZE;
}

void printbacktrace () {
  if (TraceStart != TraceTop) pserial('[');
  int tracesize = modbacktrace(TraceTop-TraceStart);
  for (int i=1; i<=tracesize; i++) {
    printsymbol(symbol(Backtrace[modbacktrace(TraceTop-i)]), pserial);
    if (i!=tracesize) pfstring(" <- ", pserial);
  }
  if (TraceStart != TraceTop) pserial(']');
}

void errorsub (symbol_t fname, const char *string) {
  pfl(pserial); pfstring("Error", pserial);
  if (TraceStart != TraceTop) pserial(' ');
  printbacktrace();
  pfstring(": ", pserial);
  if (fname != sym(NIL)) {
    pserial('\'');
    psymbol(fname, pserial);
    pserial('\''); pserial(' ');
  }
  pfstring(string, pserial);
}

void errorend () { GCStack = NULL; longjmp(*handler, 1); }

void errorsym (symbol_t fname, const char *string, object *symbol) {
  if (!tstflag(MUFFLEERRORS)) {
    errorsub(fname, string);
    pserial(':'); pserial(' ');
    printobject(symbol, pserial);
    pln(pserial);
  }
  errorend();
}

void errorsym2 (symbol_t fname, const char *string) {
  if (!tstflag(MUFFLEERRORS)) {
    errorsub(fname, string);
    pln(pserial);
  }
  errorend();
}

void error (const char *string, object *symbol) {
  errorsym(sym(Context), string, symbol);
}

void error2 (const char *string) {
  errorsym2(sym(Context), string);
}

void formaterr (object *formatstr, const char *string, uint8_t p) {
  pln(pserial); indent(4, ' ', pserial); printstring(formatstr, pserial); pln(pserial);
  indent(p+5, ' ', pserial); pserial('^');
  error2(string);
  pln(pserial);
  errorend();
}

// Save space as these are used multiple times
const char notanumber[] = "argument is not a number";
const char notaninteger[] = "argument is not an integer";
const char notastring[] = "argument is not a string";
const char notalist[] = "argument is not a list";
const char notasymbol[] = "argument is not a symbol";
const char notproper[] = "argument is not a proper list";
const char toomanyargs[] = "too many arguments";
const char toofewargs[] = "too few arguments";
const char noargument[] = "missing argument";
const char nostream[] = "missing stream argument";
const char overflow[] = "arithmetic overflow";
const char divisionbyzero[] = "division by zero";
const char indexnegative[] = "index can't be negative";
const char invalidarg[] = "invalid argument";
const char invalidkey[] = "invalid keyword";
const char illegalclause[] = "illegal clause";
const char illegalfn[] = "illegal function";
const char invalidpin[] = "invalid pin";
const char oddargs[] = "odd number of arguments";
const char indexrange[] = "index out of range";
const char canttakecar[] = "can't take car";
const char canttakecdr[] = "can't take cdr";
const char unknownstreamtype[] = "unknown stream type";

// Set up workspace

void initworkspace () {
  Freelist = NULL;
  for (int i=WORKSPACESIZE-1; i>=0; i--) {
    object *obj = &Workspace[i];
    car(obj) = NULL;
    cdr(obj) = Freelist;
    Freelist = obj;
    Freespace++;
  }
}

object *myalloc () {
  if (Freespace == 0) { Context = NIL; error2("no room"); }
  object *temp = Freelist;
  Freelist = cdr(Freelist);
  Freespace--;
  return temp;
}

inline void myfree (object *obj) {
  car(obj) = NULL;
  cdr(obj) = Freelist;
  Freelist = obj;
  Freespace++;
}

// Make each type of object

object *number (int n) {
  object *ptr = myalloc();
  ptr->type = NUMBER;
  ptr->integer = n;
  return ptr;
}

object *makefloat (float f) {
  object *ptr = myalloc();
  ptr->type = FLOAT;
  ptr->single_float = f;
  return ptr;
}

object *character (uint8_t c) {
  object *ptr = myalloc();
  ptr->type = CHARACTER;
  ptr->chars = c;
  return ptr;
}

object *cons (object *arg1, object *arg2) {
  object *ptr = myalloc();
  ptr->car = arg1;
  ptr->cdr = arg2;
  return ptr;
}

object *symbol (symbol_t name) {
  object *ptr = myalloc();
  ptr->type = SYMBOL;
  ptr->name = name;
  return ptr;
}

inline object *bsymbol (builtin_t name) {
  return intern(twist(name+BUILTINS));
}

object *intern (symbol_t name) {
  #if !defined(BOARD_HAS_PSRAM)
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && obj->name == name) return obj;
  }
  #endif
  return symbol(name);
}

bool eqsymbols (object *obj, char *buffer) {
  object *arg = cdr(obj);
  int i = 0;
  while (!(arg == NULL && buffer[i] == 0)) {
    if (arg == NULL || buffer[i] == 0) return false;
    chars_t test = 0; int shift = 24;
    for (int j=0; j<4; j++, i++) {
      if (buffer[i] == 0) break;
      test = test | buffer[i]<<shift;
      shift = shift - 8;
    }
    if (arg->chars != test) return false;
    arg = car(arg);
  }
  return true;
}

object *internlong (char *buffer) {
  #if !defined(BOARD_HAS_PSRAM)
  for (int i=0; i<WORKSPACESIZE; i++) {
    object *obj = &Workspace[i];
    if (obj->type == SYMBOL && longsymbolp(obj) && eqsymbols(obj, buffer)) return obj;
  }
  #endif
  object *obj = lispstring(buffer);
  obj->type = SYMBOL;
  return obj;
}

object *stream (uint8_t streamtype, uint8_t address) {
  object *ptr = myalloc();
  ptr->type = STREAM;
  ptr->integer = streamtype<<8 | address;
  return ptr;
}

object *newstring () {
  object *ptr = myalloc();
  ptr->type = STRING;
  ptr->chars = 0;
  return ptr;
}

// Features

const char floatingpoint[] = ":floating-point";
const char arrays[] = ":arrays";
const char doc[] = ":documentation";
const char errorhandling[] = ":error-handling";
const char wifi[] = ":wi-fi";
const char gfx[] = ":gfx";
const char sdcard[] = ":sd-card";
const char lx6[] = ":lx6";
const char lx7[] = ":lx7";
const char riscv[] = ":risc-v";

object *features () {
  object *result = NULL;
  #if defined(CPU_RISC_V)
  push(internlong((char *)riscv), result);
  #elif defined(CPU_LX6)
  push(internlong((char *)lx6), result);
  #elif defined(CPU_LX7)
  push(internlong((char *)lx7), result);
  #endif
  #if defined(gfxsupport)
  push(internlong((char *)gfx), result);
  #endif
  #if defined(sdcardsupport)
  push(internlong((char *)sdcard), result);
  #endif
  push(internlong((char *)wifi), result);
  push(internlong((char *)errorhandling), result);
  push(internlong((char *)doc), result);
  push(internlong((char *)arrays), result);
  push(internlong((char *)floatingpoint), result);
  return result;
}

// Garbage collection

void markobject (object *obj) {
  MARK:
  if (obj == NULL) return;
  if (marked(obj)) return;

  object* arg = car(obj);
  unsigned int type = obj->type;
  mark(obj);

  if (type >= PAIR || type == ZZERO) { // cons
    markobject(arg);
    obj = cdr(obj);
    goto MARK;
  }

  if (type == ARRAY) {
    obj = cdr(obj);
    goto MARK;
  }

  if ((type == STRING) || (type == SYMBOL && longsymbolp(obj))) {
    obj = cdr(obj);
    while (obj != NULL) {
      arg = car(obj);
      mark(obj);
      obj = arg;
    }
  }
}

void sweep () {
  Freelist = NULL;
  Freespace = 0;
  for (int i=WORKSPACESIZE-1; i>=0; i--) {
    object *obj = &Workspace[i];
    if (!marked(obj)) myfree(obj); else unmark(obj);
  }
}

void gc (object *form, object *env) {
  #if defined(printgcs)
  int start = Freespace;
  #endif
  markobject(tee);
  markobject(GlobalEnv);
  markobject(GCStack);
  markobject(form);
  markobject(env);
  sweep();
  #if defined(printgcs)
  pfl(pserial); pserial('{'); pint(Freespace - start, pserial); pserial('}');
  #endif
}

// Compact image

void movepointer (object *from, object *to) {
   uintptr_t limit = ((uintptr_t)(from) - (uintptr_t)(Workspace))/sizeof(uintptr_t);
   for (uintptr_t i=0; i<limit; i++) {
    object *obj = &Workspace[i];
    unsigned int type = (obj->type) & ~MARKBIT;
    if (marked(obj) && (type >= ARRAY || type==ZZERO || (type == SYMBOL && longsymbolp(obj)))) {
      if (car(obj) == (object *)((uintptr_t)from | MARKBIT))
        car(obj) = (object *)((uintptr_t)to | MARKBIT);
      if (cdr(obj) == from) cdr(obj) = to;
    }
  }
  // Fix strings and long symbols
  for (uintptr_t i=0; i<limit; i++) {
    object *obj = &Workspace[i];
    if (marked(obj)) {
      unsigned int type = (obj->type) & ~MARKBIT;
      if (type == STRING || (type == SYMBOL && longsymbolp(obj))) {
        obj = cdr(obj);
        while (obj != NULL) {
          if (cdr(obj) == to) cdr(obj) = from;
          obj = (object *)((uintptr_t)(car(obj)) & ~MARKBIT);
        }
      }
    }
  }
}

uintptr_t compactimage (object **arg) {
  markobject(tee);
  markobject(GlobalEnv);
  markobject(GCStack);
  object *firstfree = Workspace;
  while (marked(firstfree)) firstfree++;
  object *obj = &Workspace[WORKSPACESIZE-1];
  while (firstfree < obj) {
    if (marked(obj)) {
      car(firstfree) = car(obj);
      cdr(firstfree) = cdr(obj);
      unmark(obj);
      movepointer(obj, firstfree);
      if (GlobalEnv == obj) GlobalEnv = firstfree;
      if (GCStack == obj) GCStack = firstfree;
      if (*arg == obj) *arg = firstfree;
      while (marked(firstfree)) firstfree++;
    }
    obj--;
  }
  sweep();
  return firstfree - Workspace;
}

// Make SD card filename

char *MakeFilename (object *arg, char *buffer) {
  int max = (BUFFERSIZE * 4) -1;
  buffer[0]='/';
  int i = 1;
  do {
    char c = nthchar(arg, i-1);
    if (c == '\0') break;
    buffer[i++] = c;
  } while (i<max);
  buffer[i] = '\0';
  return buffer;
}

// Save-image and load-image

#if defined(sdcardsupport)
void SDBegin() {
  digitalWrite(TDECK_SDCARD_CS, HIGH);
  digitalWrite(TDECK_LORA_CS, HIGH);
  digitalWrite(TDECK_TFT_CS, HIGH);
  SD.begin(TDECK_SDCARD_CS, SPI, 800000U);
}

void SDWriteInt (File file, int data) {
  file.write(data & 0xFF); file.write(data>>8 & 0xFF);
  file.write(data>>16 & 0xFF); file.write(data>>24 & 0xFF);
}

int SDReadInt (File file) {
  uintptr_t b0 = file.read(); uintptr_t b1 = file.read();
  uintptr_t b2 = file.read(); uintptr_t b3 = file.read();
  return b0 | b1<<8 | b2<<16 | b3<<24;
}
#elif defined(LITTLEFS)
void FSWrite32 (File file, uint32_t data) {
  union { uint32_t data2; uint8_t u8[4]; };
  data2 = data;
  if (file.write(u8, 4) != 4) error2("not enough room");
}

uint32_t FSRead32 (File file) {
  union { uint32_t data; uint8_t u8[4]; };
  file.read(u8, 4);
  return data;
}
#else
void EpromWriteInt(int *addr, uintptr_t data) {
  EEPROM.write((*addr)++, data & 0xFF); EEPROM.write((*addr)++, data>>8 & 0xFF);
  EEPROM.write((*addr)++, data>>16 & 0xFF); EEPROM.write((*addr)++, data>>24 & 0xFF);
}

int EpromReadInt (int *addr) {
  uint8_t b0 = EEPROM.read((*addr)++); uint8_t b1 = EEPROM.read((*addr)++);
  uint8_t b2 = EEPROM.read((*addr)++); uint8_t b3 = EEPROM.read((*addr)++);
  return b0 | b1<<8 | b2<<16 | b3<<24;
}
#endif

unsigned int saveimage (object *arg) {
#if defined(sdcardsupport)
  unsigned int imagesize = compactimage(&arg);
  SDBegin();
  File file;
  if (stringp(arg)) {
    char buffer[BUFFERSIZE];
    file = SD.open(MakeFilename(arg, buffer), FILE_WRITE);
    if (!file) error2("problem saving to SD card or invalid filename");
    arg = NULL;
  } else if (arg == NULL || listp(arg)) {
    file = SD.open("/ULISP.IMG", FILE_WRITE);
    if (!file) error2("problem saving to SD card");
  } else error(invalidarg, arg);
  SDWriteInt(file, (uintptr_t)arg);
  SDWriteInt(file, imagesize);
  SDWriteInt(file, (uintptr_t)GlobalEnv);
  SDWriteInt(file, (uintptr_t)GCStack);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    SDWriteInt(file, (uintptr_t)car(obj));
    SDWriteInt(file, (uintptr_t)cdr(obj));
  }
  file.close();
  return imagesize;
#elif defined(LITTLEFS)
  unsigned int imagesize = compactimage(&arg);
  if (!LittleFS.begin(true)) error2("problem mounting LittleFS");
  int bytesneeded = imagesize*8 + 36; int bytesavailable = LittleFS.totalBytes();
  if (bytesneeded > bytesavailable) error("image too large by", number(bytesneeded - bytesavailable));
  File file;
  if (stringp(arg)) {
    char buffer[BUFFERSIZE];
    file = LittleFS.open(MakeFilename(arg, buffer), "w");
    if (!file) error2("problem saving to LittleFS or invalid filename");
    arg = NULL;
  } else if (arg == NULL || listp(arg)) {
    file = LittleFS.open("/ULISP.IMG", "w");
    if (!file) error2("problem saving to LittleFS");
  } else error(invalidarg, arg);
  FSWrite32(file, (uintptr_t)arg);
  FSWrite32(file, imagesize);
  FSWrite32(file, (uintptr_t)GlobalEnv);
  FSWrite32(file, (uintptr_t)GCStack);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    FSWrite32(file, (uintptr_t)car(obj));
    FSWrite32(file, (uintptr_t)cdr(obj));
  }
  file.close();
  return imagesize;
#elif defined(EEPROMSIZE)
  unsigned int imagesize = compactimage(&arg);
  if (!(arg == NULL || listp(arg))) error("illegal argument", arg);
  int bytesneeded = imagesize*8 + 36;
  if (bytesneeded > EEPROMSIZE) error("image too large by", number(bytesneeded - EEPROMSIZE));
  EEPROM.begin(EEPROMSIZE);
  int addr = 0;
  EpromWriteInt(&addr, (uintptr_t)arg);
  EpromWriteInt(&addr, imagesize);
  EpromWriteInt(&addr, (uintptr_t)GlobalEnv);
  EpromWriteInt(&addr, (uintptr_t)GCStack);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    EpromWriteInt(&addr, (uintptr_t)car(obj));
    EpromWriteInt(&addr, (uintptr_t)cdr(obj));
  }
  EEPROM.commit();
  return imagesize;
#else
  (void) arg;
  error2("not available");
  return 0;
#endif
}

unsigned int loadimage (object *arg) {
#if defined(sdcardsupport)
  SDBegin();
  File file;
  if (stringp(arg)) {
    char buffer[BUFFERSIZE];
    file = SD.open(MakeFilename(arg, buffer));
    if (!file) error2("problem loading from SD card or invalid filename");
  } else if (arg == NULL) {
    file = SD.open("/ULISP.IMG");
    if (!file) error2("problem loading from SD card");
  } else error(invalidarg, arg);
  SDReadInt(file);
  unsigned int imagesize = SDReadInt(file);
  GlobalEnv = (object *)SDReadInt(file);
  GCStack = (object *)SDReadInt(file);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    car(obj) = (object *)SDReadInt(file);
    cdr(obj) = (object *)SDReadInt(file);
  }
  file.close();
  gc(NULL, NULL);
  return imagesize;
#elif defined(LITTLEFS)
  if (!LittleFS.begin()) error2("problem mounting LittleFS");
  File file;
  if (stringp(arg)) {
    char buffer[BUFFERSIZE];
    file = LittleFS.open(MakeFilename(arg, buffer), "r");
    if (!file) error2("problem loading from LittleFS or invalid filename");
  }
  else if (arg == NULL) {
    file = LittleFS.open("/ULISP.IMG", "r");
    if (!file) error2("problem loading from LittleFS");
  }
  else error(invalidarg, arg);
  FSRead32(file);
  unsigned int imagesize = FSRead32(file);
  GlobalEnv = (object *)FSRead32(file);
  GCStack = (object *)FSRead32(file);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    car(obj) = (object *)FSRead32(file);
    cdr(obj) = (object *)FSRead32(file);
  }
  file.close();
  gc(NULL, NULL);
  return imagesize;
#elif defined(EEPROMSIZE)
  (void) arg;
  EEPROM.begin(EEPROMSIZE);
  int addr = 0;
  EpromReadInt(&addr); // Skip eval address
  unsigned int imagesize = EpromReadInt(&addr);
  if (imagesize == 0 || imagesize == 0xFFFFFFFF) error2("no saved image");
  GlobalEnv = (object *)EpromReadInt(&addr);
  GCStack = (object *)EpromReadInt(&addr);
  for (unsigned int i=0; i<imagesize; i++) {
    object *obj = &Workspace[i];
    car(obj) = (object *)EpromReadInt(&addr);
    cdr(obj) = (object *)EpromReadInt(&addr);
  }
  gc(NULL, NULL);
  return imagesize;
#else
  (void) arg;
  error2("not available");
  return 0;
#endif
}

void autorunimage () {
#if defined(sdcardsupport)
  SDBegin();
  File file = SD.open("/ULISP.IMG");
  if (!file) error2("problem autorunning from SD card");
  object *autorun = (object *)SDReadInt(file);
  file.close();
  if (autorun != NULL) {
    loadimage(NULL);
    apply(autorun, NULL, NULL);
  }
#elif defined(LITTLEFS)
  if (!LittleFS.begin()) error2("problem mounting LittleFS");
  File file = LittleFS.open("/ULISP.IMG", "r");
  if (!file) error2("problem autorunning from LittleFS");
  object *autorun = (object *)FSRead32(file);
  file.close();
  if (autorun != NULL) {
    loadimage(NULL);
    apply(autorun, NULL, NULL);
  }
#elif defined(EEPROMSIZE)
  EEPROM.begin(EEPROMSIZE);
  int addr = 0;
  object *autorun = (object *)EpromReadInt(&addr);
  if (autorun != NULL && (unsigned int)autorun != 0xFFFF) {
    loadimage(NULL);
    apply(autorun, NULL, NULL);
  }
#else
  error2("autorun not available");
#endif
}

// Tracing

int tracing (symbol_t name) {
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == name) return i+1;
    i++;
  }
  return 0;
}

void trace (symbol_t name) {
  if (tracing(name)) error("already being traced", symbol(name));
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == 0) { TraceFn[i] = name; TraceDepth[i] = 0; return; }
    i++;
  }
  error2("already tracing " stringify(TRACEMAX) " functions");
}

void untrace (symbol_t name) {
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] == name) { TraceFn[i] = 0; return; }
    i++;
  }
  error("not tracing", symbol(name));
}

// Helper functions

bool consp (object *x) {
  if (x == NULL) return false;
  unsigned int type = x->type;
  return type >= PAIR || type == ZZERO;
}

#define atom(x) (!consp(x))

bool listp (object *x) {
  if (x == NULL) return true;
  unsigned int type = x->type;
  return type >= PAIR || type == ZZERO;
}

#define improperp(x) (!listp(x))

object *quote (object *arg) {
  return cons(bsymbol(QUOTE), cons(arg,NULL));
}

// Radix 40 encoding

builtin_t builtin (symbol_t name) {
  return (builtin_t)(untwist(name) - BUILTINS);
}

symbol_t sym (builtin_t x) {
  return twist(x + BUILTINS);
}

int8_t toradix40 (char ch) {
  if (ch == 0) return 0;
  if (ch >= '0' && ch <= '9') return ch-'0'+1;
  if (ch == '-') return 37; if (ch == '*') return 38; if (ch == '$') return 39;
  ch = ch | 0x20;
  if (ch >= 'a' && ch <= 'z') return ch-'a'+11;
  return -1; // Invalid
}

char fromradix40 (char n) {
  if (n >= 1 && n <= 10) return '0'+n-1;
  if (n >= 11 && n <= 36) return 'a'+n-11;
  if (n == 37) return '-'; if (n == 38) return '*'; if (n == 39) return '$';
  return 0;
}

uint32_t pack40 (char *buffer) {
  int x = 0, j = 0;
  for (int i=0; i<6; i++) {
    x = x * 40 + toradix40(buffer[j]);
    if (buffer[j] != 0) j++;
  }
  return x;
}

bool valid40 (char *buffer) {
  int t = 11;
  for (int i=0; i<6; i++) {
    if (toradix40(buffer[i]) < t) return false;
    if (buffer[i] == 0) break;
    t = 0;
  }
  return true;
}

int8_t digitvalue (char d) {
  if (d>='0' && d<='9') return d-'0';
  d = d | 0x20;
  if (d>='a' && d<='f') return d-'a'+10;
  return 16;
}

int checkinteger (object *obj) {
  if (!integerp(obj)) error(notaninteger, obj);
  return obj->integer;
}

int checkbitvalue (object *obj) {
  if (!integerp(obj)) error(notaninteger, obj);
  int n = obj->integer;
  if (n & ~1) error("argument is not a bit value", obj);
  return n;
}

float checkintfloat (object *obj) {
  if (integerp(obj)) return (float)obj->integer;
  if (!floatp(obj)) error(notanumber, obj);
  return obj->single_float;
}

int checkchar (object *obj) {
  if (!characterp(obj)) error("argument is not a character", obj);
  return obj->chars;
}

object *checkstring (object *obj) {
  if (!stringp(obj)) error(notastring, obj);
  return obj;
}

int isstream (object *obj){
  if (!streamp(obj)) error("not a stream", obj);
  return obj->integer;
}

int isbuiltin (object *obj, builtin_t n) {
  return symbolp(obj) && obj->name == sym(n);
}

bool builtinp (symbol_t name) {
  return (untwist(name) >= BUILTINS);
}

int checkkeyword (object *obj) {
  if (!keywordp(obj)) error("argument is not a keyword", obj);
  builtin_t kname = builtin(obj->name);
  uint8_t context = getminmax(kname);
  if (context != 0 && context != Context) error(invalidkey, obj);
  return ((int)lookupfn(kname));
}

void checkargs (object *args) {
  int nargs = listlength(args);
  checkminmax(Context, nargs);
}

bool eqlongsymbol (symbol_t sym1, symbol_t sym2) {
  object *arg1 = (object *)sym1; object *arg2 = (object *)sym2;
  while ((arg1 != NULL) || (arg2 != NULL)) {
    if (arg1 == NULL || arg2 == NULL) return false;
    if (arg1->chars != arg2->chars) return false;
    arg1 = car(arg1); arg2 = car(arg2);
  }
  return true;
}

bool eqsymbol (symbol_t sym1, symbol_t sym2) {
  if (!longnamep(sym1) && !longnamep(sym2)) return (sym1 == sym2);  // Same short symbol
  if (longnamep(sym1) && longnamep(sym2)) return eqlongsymbol(sym1, sym2);  // Same long symbol
  return false;
}

bool eq (object *arg1, object *arg2) {
  if (arg1 == arg2) return true;  // Same object
  if ((arg1 == nil) || (arg2 == nil)) return false;  // Not both values
  #if !defined(BOARD_HAS_PSRAM)
  if (arg1->cdr != arg2->cdr) return false;  // Different values
  if (symbolp(arg1) && symbolp(arg2)) return true;  // Same symbol
  #else
  if (symbolp(arg1) && symbolp(arg2)) return eqsymbol(arg1->name, arg2->name);  // Same symbol?
  if (arg1->cdr != arg2->cdr) return false;  // Different values
  #endif
  if (integerp(arg1) && integerp(arg2)) return true;  // Same integer
  if (floatp(arg1) && floatp(arg2)) return true; // Same float
  if (characterp(arg1) && characterp(arg2)) return true;  // Same character
  return false;
}

bool equal (object *arg1, object *arg2) {
  if (stringp(arg1) && stringp(arg2)) return (stringcompare(cons(arg1, cons(arg2, nil)), false, false, true) != -1);
  if (consp(arg1) && consp(arg2)) return (equal(car(arg1), car(arg2)) && equal(cdr(arg1), cdr(arg2)));
  return eq(arg1, arg2);
}

int listlength (object *list) {
  int length = 0;
  while (list != NULL) {
    if (improperp(list)) error2(notproper);
    list = cdr(list);
    length++;
  }
  return length;
}

object *checkarguments (object *args, int min, int max) {
  if (args == NULL) error2(noargument);
  args = first(args);
  if (!listp(args)) error(notalist, args);
  int length = listlength(args);
  if (length < min) error(toofewargs, args);
  if (length > max) error(toomanyargs, args);
  return args;
}

// Mathematical helper functions

object *add_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult + checkintfloat(arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

object *subtract_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    fresult = fresult - checkintfloat(arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

object *negate (object *arg) {
  if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat(-result);
    else return number(-result);
  } else if (floatp(arg)) return makefloat(-(arg->single_float));
  else error(notanumber, arg);
  return nil;
}

object *multiply_floats (object *args, float fresult) {
  while (args != NULL) {
   object *arg = car(args);
    fresult = fresult * checkintfloat(arg);
    args = cdr(args);
  }
  return makefloat(fresult);
}

object *divide_floats (object *args, float fresult) {
  while (args != NULL) {
    object *arg = car(args);
    float f = checkintfloat(arg);
    if (f == 0.0) error2(divisionbyzero);
    fresult = fresult / f;
    args = cdr(args);
  }
  return makefloat(fresult);
}

object *remmod (object *args, bool mod) {
  object *arg1 = first(args);
  object *arg2 = second(args);
  if (integerp(arg1) && integerp(arg2)) {
    int divisor = arg2->integer;
    if (divisor == 0) error2(divisionbyzero);
    int dividend = arg1->integer;
    int remainder = dividend % divisor;
    if (mod && (dividend<0) != (divisor<0)) remainder = remainder + divisor;
    return number(remainder);
  } else {
    float fdivisor = checkintfloat(arg2);
    if (fdivisor == 0.0) error2(divisionbyzero);
    float fdividend = checkintfloat(arg1);
    float fremainder = fmod(fdividend , fdivisor);
    if (mod && (fdividend<0) != (fdivisor<0)) fremainder = fremainder + fdivisor;
    return makefloat(fremainder);
  }
}

object *compare (object *args, bool lt, bool gt, bool eq) {
  object *arg1 = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg2 = first(args);
    if (integerp(arg1) && integerp(arg2)) {
      if (!lt && ((arg1->integer) < (arg2->integer))) return nil;
      if (!eq && ((arg1->integer) == (arg2->integer))) return nil;
      if (!gt && ((arg1->integer) > (arg2->integer))) return nil;
    } else {
      if (!lt && (checkintfloat(arg1) < checkintfloat(arg2))) return nil;
      if (!eq && (checkintfloat(arg1) == checkintfloat(arg2))) return nil;
      if (!gt && (checkintfloat(arg1) > checkintfloat(arg2))) return nil;
    }
    arg1 = arg2;
    args = cdr(args);
  }
  return tee;
}

int intpower (int base, int exp) {
  int result = 1;
  while (exp) {
    if (exp & 1) result = result * base;
    exp = exp / 2;
    base = base * base;
  }
  return result;
}

// Association lists

object *testargument (object *args) {
  object *test = bsymbol(EQ);
  if (args != NULL) {
    if (cdr(args) == NULL) error2("unpaired keyword");
    if ((isbuiltin(first(args), TEST))) test = second(args);
    else error("unsupported keyword", first(args));
  }
  return test;
}

object *delassoc (object *key, object **alist) {
  object *list = *alist;
  object *prev = NULL;
  while (list != NULL) {
    object *pair = first(list);
    if (eq(key,car(pair))) {
      if (prev == NULL) *alist = cdr(list);
      else cdr(prev) = cdr(list);
      return key;
    }
    prev = list;
    list = cdr(list);
  }
  return nil;
}

// Array utilities

int nextpower2 (int n) {
  n--; n |= n >> 1; n |= n >> 2; n |= n >> 4;
  n |= n >> 8; n |= n >> 16; n++;
  return n<2 ? 2 : n;
}

object *buildarray (int n, int s, object *def) {
  int s2 = s>>1;
  if (s2 == 1) {
    if (n == 2) return cons(def, def);
    else if (n == 1) return cons(def, NULL);
    else return NULL;
  } else if (n >= s2) return cons(buildarray(s2, s2, def), buildarray(n - s2, s2, def));
  else return cons(buildarray(n, s2, def), nil);
}

object *makearray (object *dims, object *def, bool bitp) {
  int size = 1;
  object *dimensions = dims;
  while (dims != NULL) {
    int d = car(dims)->integer;
    if (d < 0) error2("dimension can't be negative");
    size = size * d;
    dims = cdr(dims);
  }
  // Bit array identified by making first dimension negative
  if (bitp) {
    size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
    car(dimensions) = number(-(car(dimensions)->integer));
  }
  object *ptr = myalloc();
  ptr->type = ARRAY;
  object *tree = nil;
  if (size != 0) tree = buildarray(size, nextpower2(size), def);
  ptr->cdr = cons(tree, dimensions);
  return ptr;
}

object **arrayref (object *array, int index, int size) {
  int mask = nextpower2(size)>>1;
  object **p = &car(cdr(array));
  while (mask) {
    if ((index & mask) == 0) p = &(car(*p)); else p = &(cdr(*p));
    mask = mask>>1;
  }
  return p;
}

object **getarray (object *array, object *subs, object *env, int *bit) {
  int index = 0, size = 1, s;
  *bit = -1;
  bool bitp = false;
  object *dims = cddr(array);
  while (dims != NULL && subs != NULL) {
    int d = car(dims)->integer;
    if (d < 0) { d = -d; bitp = true; }
    if (env) s = checkinteger(eval(car(subs), env)); else s = checkinteger(car(subs));
    if (s < 0 || s >= d) error("subscript out of range", car(subs));
    size = size * d;
    index = index * d + s;
    dims = cdr(dims); subs = cdr(subs);
  }
  if (dims != NULL) error2("too few subscripts");
  if (subs != NULL) error2("too many subscripts");
  if (bitp) {
    size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
    *bit = index & (sizeof(int)==4 ? 0x1F : 0x0F);
    index = index>>(sizeof(int)==4 ? 5 : 4);
  }
  return arrayref(array, index, size);
}

void rslice (object *array, int size, int slice, object *dims, object *args) {
  int d = first(dims)->integer;
  for (int i = 0; i < d; i++) {
    int index = slice * d + i;
    if (!consp(args)) error2("initial contents don't match array type");
    if (cdr(dims) == NULL) {
      object **p = arrayref(array, index, size);
      *p = car(args);
    } else rslice(array, size, index, cdr(dims), car(args));
    args = cdr(args);
  }
}

object *readarray (int d, object *args) {
  object *list = args;
  object *dims = NULL; object *head = NULL;
  int size = 1;
  for (int i = 0; i < d; i++) {
    if (!listp(list)) error2("initial contents don't match array type");
    int l = listlength(list);
    if (dims == NULL) { dims = cons(number(l), NULL); head = dims; }
    else { cdr(dims) = cons(number(l), NULL); dims = cdr(dims); }
    size = size * l;
    if (list != NULL) list = car(list);
  }
  object *array = makearray(head, NULL, false);
  rslice(array, size, 0, head, args);
  return array;
}

object *readbitarray (gfun_t gfun) {
  char ch = gfun();
  object *head = NULL;
  object *tail = NULL;
  while (!issp(ch) && !isbr(ch)) {
    if (ch != '0' && ch != '1') error2("illegal character in bit array");
    object *cell = cons(number(ch - '0'), NULL);
    if (head == NULL) head = cell;
    else tail->cdr = cell;
    tail = cell;
    ch = gfun();
  }
  LastChar = ch;
  int size = listlength(head);
  object *array = makearray(cons(number(size), NULL), number(0), true);
  size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
  int index = 0;
  while (head != NULL) {
    object **loc = arrayref(array, index>>(sizeof(int)==4 ? 5 : 4), size);
    int bit = index & (sizeof(int)==4 ? 0x1F : 0x0F);
    *loc = number((((*loc)->integer) & ~(1<<bit)) | (car(head)->integer)<<bit);
    index++;
    head = cdr(head);
  }
  return array;
}

void pslice (object *array, int size, int slice, object *dims, pfun_t pfun, bool bitp) {
  bool spaces = true;
  if (slice == -1) { spaces = false; slice = 0; }
  int d = first(dims)->integer;
  if (d < 0) d = -d;
  for (int i = 0; i < d; i++) {
    if (i && spaces) pfun(' ');
    int index = slice * d + i;
    if (cdr(dims) == NULL) {
      if (bitp) pint(((*arrayref(array, index>>(sizeof(int)==4 ? 5 : 4), size))->integer)>>
        (index & (sizeof(int)==4 ? 0x1F : 0x0F)) & 1, pfun);
      else printobject(*arrayref(array, index, size), pfun);
    } else { pfun('('); pslice(array, size, index, cdr(dims), pfun, bitp); pfun(')'); }
  }
}

void printarray (object *array, pfun_t pfun) {
  object *dimensions = cddr(array);
  object *dims = dimensions;
  bool bitp = false;
  int size = 1, n = 0;
  while (dims != NULL) {
    int d = car(dims)->integer;
    if (d < 0) { bitp = true; d = -d; }
    size = size * d;
    dims = cdr(dims); n++;
  }
  if (bitp) size = (size + sizeof(int)*8 - 1)/(sizeof(int)*8);
  pfun('#');
  if (n == 1 && bitp) { pfun('*'); pslice(array, size, -1, dimensions, pfun, bitp); }
  else {
    if (n > 1) { pint(n, pfun); pfun('A'); }
    pfun('('); pslice(array, size, 0, dimensions, pfun, bitp); pfun(')');
  }
}

// String utilities

void indent (uint8_t spaces, char ch, pfun_t pfun) {
  for (uint8_t i=0; i<spaces; i++) pfun(ch);
}

object *startstring () {
  object *string = newstring();
  GlobalString = string;
  GlobalStringTail = string;
  return string;
}

object *princtostring (object *arg) {
  object *obj = startstring();
  prin1object(arg, pstr);
  return obj;
}

void buildstring (char ch, object** tail) {
  object* cell;
  if (cdr(*tail) == NULL) {
    cell = myalloc(); cdr(*tail) = cell;
  } else if (((*tail)->chars & 0xFFFFFF) == 0) {
    (*tail)->chars |= ch<<16; return;
  } else if (((*tail)->chars & 0xFFFF) == 0) {
    (*tail)->chars |= ch<<8; return;
  } else if (((*tail)->chars & 0xFF) == 0) {
    (*tail)->chars |= ch; return;
  } else {
    cell = myalloc(); car(*tail) = cell;
  }
  car(cell) = NULL; cell->chars = ch<<24; *tail = cell;
}

object *copystring (object *arg) {
  object *obj = newstring();
  object *ptr = obj;
  arg = cdr(arg);
  while (arg != NULL) {
    object *cell =  myalloc(); car(cell) = NULL;
    if (cdr(obj) == NULL) cdr(obj) = cell; else car(ptr) = cell;
    ptr = cell;
    ptr->chars = arg->chars;
    arg = car(arg);
  }
  return obj;
}

object *readstring (uint8_t delim, bool esc, gfun_t gfun) {
  object *obj = newstring();
  object *tail = obj;
  int ch = gfun();
  if (ch == -1) return nil;
  while ((ch != delim) && (ch != -1)) {
    if (esc && ch == '\\') ch = gfun();
    buildstring(ch, &tail);
    ch = gfun();
  }
  return obj;
}

int stringlength (object *form) {
  int length = 0;
  form = cdr(form);
  while (form != NULL) {
    int chars = form->chars;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      if (chars>>i & 0xFF) length++;
    }
    form = car(form);
  }
  return length;
}

object **getcharplace (object *string, int n, int *shift) {
  object **arg = &cdr(string);
  int top;
  if (sizeof(int) == 4) { top = n>>2; *shift = 3 - (n&3); }
  else { top = n>>1; *shift = 1 - (n&1); }
  *shift = - (*shift + 2);
  for (int i=0; i<top; i++) {
    if (*arg == NULL) break;
    arg = &car(*arg);
  }
  return arg;
}

uint8_t nthchar (object *string, int n) {
  int shift;
  object **arg = getcharplace(string, n, &shift);
  if (*arg == NULL) return 0;
  return (((*arg)->chars)>>((-shift-2)<<3)) & 0xFF;
}

int gstr () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  char c = nthchar(GlobalString, GlobalStringIndex++);
  if (c != 0) return c;
  return '\n'; // -1?
}

void pstr (char c) {
  buildstring(c, &GlobalStringTail);
}

object *lispstring (char *s) {
  object *obj = newstring();
  object *tail = obj;
  while(1) {
    char ch = *s++;
    if (ch == 0) break;
    if (ch == '\\') ch = *s++;
    buildstring(ch, &tail);
  }
  return obj;
}

int stringcompare (object *args, bool lt, bool gt, bool eq) {
  object *arg1 = checkstring(first(args));
  object *arg2 = checkstring(second(args));
  arg1 = cdr(arg1); arg2 = cdr(arg2);
  int m = 0; chars_t a = 0, b = 0;
  while ((arg1 != NULL) || (arg2 != NULL)) {
    if (arg1 == NULL) return lt ? m : -1;
    if (arg2 == NULL) return gt ? m : -1;
    a = arg1->chars; b = arg2->chars;
    if (a < b) { if (lt) { m = m + sizeof(int); while (a != b) { m--; a = a >> 8; b = b >> 8; } return m; } else return -1; }
    if (a > b) { if (gt) { m = m + sizeof(int); while (a != b) { m--; a = a >> 8; b = b >> 8; } return m; } else return -1; }
    arg1 = car(arg1); arg2 = car(arg2);
    m = m + sizeof(int);
  }
  if (eq) { m = m - sizeof(int); while (a != 0) { m++; a = a << 8;} return m;} else return -1;
}

object *documentation (object *arg, object *env) {
  if (arg == NULL) return nil;
  if (!symbolp(arg)) error(notasymbol, arg);
  object *pair = findpair(arg, env);
  if (pair != NULL) {
    object *val = cdr(pair);
    if (listp(val) && first(val)->name == sym(LAMBDA) && cdr(val) != NULL && cddr(val) != NULL) {
      if (stringp(third(val))) return third(val);
    }
  }
  symbol_t docname = arg->name;
  if (!builtinp(docname)) return nil;
  char *docstring = lookupdoc(builtin(docname));
  if (docstring == NULL) return nil;
  object *obj = startstring();
  pfstring(docstring, pstr);
  return obj;
}

object *apropos (object *arg, bool print) {
  char buf[17], buf2[33];
  char *part = cstring(princtostring(arg), buf, 17);
  object *result = cons(NULL, NULL);
  object *ptr = result;
  // User-defined?
  object *globals = GlobalEnv;
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    object *val = cdr(pair);
    char *full = cstring(princtostring(var), buf2, 33);
    if (strstr(full, part) != NULL) {
      if (print) {
        printsymbol(var, pserial); pserial(' '); pserial('(');
        if (consp(val) && isbuiltin(car(val), LAMBDA)) pfstring("user function", pserial);
        else if (consp(val) && car(val)->type == CODE) pfstring("code", pserial);
        else pfstring("user symbol", pserial);
        pserial(')'); pln(pserial);
      } else {
        cdr(ptr) = cons(var, NULL); ptr = cdr(ptr);
      }
    }
    globals = cdr(globals);
    testescape();
  }
  // Built-in?
  int entries = tablesize(0) + tablesize(1);
  for (int i = 0; i < entries; i++) {
    if (findsubstring(part, (builtin_t)i)) {
      if (print) {
        uint8_t fntype = fntype(i);
        pbuiltin((builtin_t)i, pserial); pserial(' '); pserial('(');
        if (fntype == FUNCTIONS) pfstring("function", pserial);
        else if (fntype == SPECIAL_FORMS || fntype == TAIL_FORMS) pfstring("special form", pserial);
        else pfstring("symbol/keyword", pserial);
        pserial(')'); pln(pserial);
      } else {
        cdr(ptr) = cons(bsymbol(i), NULL); ptr = cdr(ptr);
      }
    }
    testescape();
  }
  return cdr(result);
}

char *cstring (object *form, char *buffer, int buflen) {
  form = cdr(checkstring(form));
  int index = 0;
  while (form != NULL) {
    int chars = form->integer;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      char ch = chars>>i & 0xFF;
      if (ch) {
        if (index >= buflen-1) error2("no room for string");
        buffer[index++] = ch;
      }
    }
    form = car(form);
  }
  buffer[index] = '\0';
  return buffer;
}

object *iptostring (uint32_t ip) {
  union { uint32_t data2; uint8_t u8[4]; };
  object *obj = startstring();
  data2 = ip;
  for (int i=0; i<4; i++) {
    if (i) pstr('.');
    pintbase(u8[i], 10, pstr);
  }
  return obj;
}

uint32_t ipstring (object *form) {
  form = cdr(checkstring(form));
  int p = 0;
  union { uint32_t ipaddress; uint8_t ipbytes[4]; } ;
  ipaddress = 0;
  while (form != NULL) {
    int chars = form->integer;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      char ch = chars>>i & 0xFF;
      if (ch) {
        if (ch == '.') { p++; if (p > 3) error2("illegal IP address"); }
        else ipbytes[p] = (ipbytes[p] * 10) + ch - '0';
      }
    }
    form = car(form);
  }
  return ipaddress;
}

object *value (symbol_t n, object *env) {
  while (env != NULL) {
    object *pair = car(env);
    #if !defined(BOARD_HAS_PSRAM)
    if (pair != NULL && car(pair)->name == n) return pair;
    #else
    if (pair != NULL && eqsymbol(car(pair)->name, n)) return pair;
    #endif
    env = cdr(env);
  }
  return nil;
}

object *findpair (object *var, object *env) {
  symbol_t name = var->name;
  object *pair = value(name, env);
  if (pair == NULL) pair = value(name, GlobalEnv);
  return pair;
}

bool boundp (object *var, object *env) {
  if (!symbolp(var)) error(notasymbol, var);
  return (findpair(var, env) != NULL);
}

object *findvalue (object *var, object *env) {
  object *pair = findpair(var, env);
  if (pair == NULL) error("unknown variable", var);
  return pair;
}

// Handling closures

object *closure (int tc, symbol_t name, object *function, object *args, object **env) {
  object *state = car(function);
  function = cdr(function);
  int trace = tracing(name);
  if (trace) {
    indent(TraceDepth[trace-1]<<1, ' ', pserial);
    pint(TraceDepth[trace-1]++, pserial);
    pserial(':'); pserial(' '); pserial('('); printsymbol(symbol(name), pserial);
  }
  object *params = first(function);
  if (!listp(params)) errorsym(name, notalist, params);
  function = cdr(function);
  // Dropframe
  if (tc) {
    if (*env != NULL && car(*env) == NULL) {
      pop(*env);
      while (*env != NULL && car(*env) != NULL) pop(*env);
    } else push(nil, *env);
  }
  // Push state
  while (consp(state)) {
    object *pair = first(state);
    push(pair, *env);
    state = cdr(state);
  }
  // Add arguments to environment
  bool optional = false;
  while (params != NULL) {
    object *value;
    object *var = first(params);
    if (isbuiltin(var, OPTIONAL)) optional = true;
    else {
      if (consp(var)) {
        if (!optional) errorsym(name, "invalid default value", var);
        if (args == NULL) value = eval(second(var), *env);
        else { value = first(args); args = cdr(args); }
        var = first(var);
        if (!symbolp(var)) errorsym(name, "illegal optional parameter", var);
      } else if (!symbolp(var)) {
        errorsym(name, "illegal function parameter", var);
      } else if (isbuiltin(var, AMPREST)) {
        params = cdr(params);
        var = first(params);
        value = args;
        args = NULL;
      } else {
        if (args == NULL) {
          if (optional) value = nil;
          else errorsym2(name, toofewargs);
        } else { value = first(args); args = cdr(args); }
      }
      push(cons(var,value), *env);
      if (trace) { pserial(' '); printobject(value, pserial); }
    }
    params = cdr(params);
  }
  if (args != NULL) errorsym2(name, toomanyargs);
  if (trace) { pserial(')'); pln(pserial); }
  // Do an implicit progn
  if (tc) push(nil, *env);
  return tf_progn(function, *env);
}

object *apply (object *function, object *args, object *env) {
  if (symbolp(function)) {
    builtin_t fname = builtin(function->name);
    if ((fname < ENDFUNCTIONS) && (fntype(fname) == FUNCTIONS)) {
      Context = fname;
      checkargs(args);
      return ((fn_ptr_type)lookupfn(fname))(args, env);
    } else function = eval(function, env);
  }
  if (consp(function) && isbuiltin(car(function), LAMBDA)) {
    object *result = closure(0, sym(NIL), function, args, &env);
    return eval(result, env);
  }
  if (consp(function) && isbuiltin(car(function), CLOSURE)) {
    function = cdr(function);
    object *result = closure(0, sym(NIL), function, args, &env);
    return eval(result, env);
  }
  error(illegalfn, function);
  return NULL;
}

// In-place operations

object **place (object *args, object *env, int *bit) {
  *bit = -1;
  if (atom(args)) return &cdr(findvalue(args, env));
  object* function = first(args);
  if (symbolp(function)) {
    symbol_t sname = function->name;
    if (sname == sym(CAR) || sname == sym(FIRST)) {
      object *value = eval(second(args), env);
      if (!listp(value)) error(canttakecar, value);
      return &car(value);
    }
    if (sname == sym(CDR) || sname == sym(REST)) {
      object *value = eval(second(args), env);
      if (!listp(value)) error(canttakecdr, value);
      return &cdr(value);
    }
    if (sname == sym(NTH)) {
      int index = checkinteger(eval(second(args), env));
      object *list = eval(third(args), env);
      if (atom(list)) { Context = NTH; error("second argument is not a list", list); }
      int i = index;
      while (i > 0) {
        list = cdr(list);
        if (list == NULL) { Context = NTH; error(indexrange, number(index)); }
        i--;
      }
      return &car(list);
    }
    if (sname == sym(CHAR)) {
      int index = checkinteger(eval(third(args), env));
      object *string = checkstring(eval(second(args), env));
      object **loc = getcharplace(string, index, bit);
      if ((*loc) == NULL || (((((*loc)->chars)>>((-(*bit)-2)<<3)) & 0xFF) == 0)) { Context = CHAR; error(indexrange, number(index)); }
      return loc;
    }
    if (sname == sym(AREF)) {
      object *array = eval(second(args), env);
      if (!arrayp(array)) { Context = AREF; error("first argument is not an array", array); }
      return getarray(array, cddr(args), env, bit);
    }
  }
  error2("illegal place");
  return nil;
}

// Checked car and cdr

object *carx (object *arg) {
  if (!listp(arg)) error(canttakecar, arg);
  if (arg == nil) return nil;
  return car(arg);
}

object *cdrx (object *arg) {
  if (!listp(arg)) error(canttakecdr, arg);
  if (arg == nil) return nil;
  return cdr(arg);
}

object *cxxxr (object *args, uint8_t pattern) {
  object *arg = first(args);
  while (pattern != 1) {
    if ((pattern & 1) == 0) arg = carx(arg); else arg = cdrx(arg);
    pattern = pattern>>1;
  }
  return arg;
}

// Mapping helper functions

object *mapcl (object *args, object *env, bool mapl) {
  object *function = first(args);
  args = cdr(args);
  object *result = first(args);
  protect(result);
  object *params = cons(NULL, NULL);
  protect(params);
  // Make parameters
  while (true) {
    object *tailp = params;
    object *lists = args;
    while (lists != NULL) {
      object *list = car(lists);
      if (list == NULL) {
         unprotect(); unprotect();
         return result;
      }
      if (improperp(list)) error(notproper, list);
      object *item = mapl ? list : first(list);
      object *obj = cons(item, NULL);
      car(lists) = cdr(list);
      cdr(tailp) = obj; tailp = obj;
      lists = cdr(lists);
    }
    apply(function, cdr(params), env);
  }
}

void mapcarfun (object *result, object **tail) {
  object *obj = cons(result,NULL);
  cdr(*tail) = obj; *tail = obj;
}

void mapcanfun (object *result, object **tail) {
  if (cdr(*tail) != NULL) error(notproper, *tail);
  while (consp(result)) {
    cdr(*tail) = result; *tail = result;
    result = cdr(result);
  }
}

object *mapcarcan (object *args, object *env, mapfun_t fun, bool maplist) {
  object *function = first(args);
  args = cdr(args);
  object *params = cons(NULL, NULL);
  protect(params);
  object *head = cons(NULL, NULL);
  protect(head);
  object *tail = head;
  // Make parameters
  while (true) {
    object *tailp = params;
    object *lists = args;
    while (lists != NULL) {
      object *list = car(lists);
      if (list == NULL) {
         unprotect(); unprotect();
         return cdr(head);
      }
      if (improperp(list)) error(notproper, list);
      object *item = maplist ? list : first(list);
      object *obj = cons(item, NULL);
      car(lists) = cdr(list);
      cdr(tailp) = obj; tailp = obj;
      lists = cdr(lists);
    }
    object *result = apply(function, cdr(params), env);
    fun(result, &tail);
  }
}

object *dobody (object *args, object *env, bool star) {
  object *varlist = first(args), *endlist = second(args);
  object *head = cons(NULL, NULL);
  protect(head);
  object *ptr = head;
  object *newenv = env;
  while (varlist != NULL) {
    object *varform = first(varlist);
    object *var, *init = NULL, *step = NULL;
    if (atom(varform)) var = varform;
    else {
      var = first(varform);
      varform = cdr(varform);
      if (varform != NULL) {  
        init = eval(first(varform), env);
        varform = cdr(varform);
        if (varform != NULL) step = cons(first(varform), NULL);
      }
    }  
    object *pair = cons(var, init);
    push(pair, newenv);
    if (star) env = newenv;
    object *cell = cons(cons(step, pair), NULL);
    cdr(ptr) = cell; ptr = cdr(ptr);
    varlist = cdr(varlist);
  }
  env = newenv;
  head = cdr(head);
  object *endtest = first(endlist), *results = cdr(endlist);
  while (eval(endtest, env) == NULL) {
    object *forms = cddr(args);
    while (forms != NULL) {
    object *result = eval(car(forms), env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        return result;
      }
      forms = cdr(forms);
    }
    object *varlist = head;
    int count = 0;
    while (varlist != NULL) {
      object *varform = first(varlist);
      object *step = car(varform), *pair = cdr(varform);
      if (step != NULL) {
        object *val = eval(first(step), env);
        if (star) {
          cdr(pair) = val;
        } else {
          push(val, GCStack);
          push(pair, GCStack);
          count++;
        }
      } 
      varlist = cdr(varlist);
    }
    while (count > 0) {
      cdr(car(GCStack)) = car(cdr(GCStack));
      pop(GCStack); pop(GCStack);
      count--;
    }
  }
  unprotect();
  return eval(tf_progn(results, env), env);
}

// I2C interface for up to two ports, using Arduino Wire

void I2Cinit (TwoWire *port, bool enablePullup) {
  (void) enablePullup;
  port->begin();
}

void I2Cinit (TwoWire *port, uint8_t SDA, uint8_t SCL, bool enablePullup) {
  (void) enablePullup;
  port->begin(SDA, SCL);
}

int I2Cread (TwoWire *port) {
  return port->read();
}

void I2Cwrite (TwoWire *port, uint8_t data) {
  port->write(data);
}

bool I2Cstart (TwoWire *port, uint8_t address, uint8_t read) {
 int ok = true;
 if (read == 0) {
   port->beginTransmission(address);
   ok = (port->endTransmission(true) == 0);
   port->beginTransmission(address);
 }
 else port->requestFrom(address, I2Ccount);
 return ok;
}

bool I2Crestart (TwoWire *port, uint8_t address, uint8_t read) {
  int error = (port->endTransmission(false) != 0);
  if (read == 0) port->beginTransmission(address);
  else port->requestFrom(address, I2Ccount);
  return error ? false : true;
}

void I2Cstop (TwoWire *port, uint8_t read) {
  if (read == 0) port->endTransmission(); // Check for error?
  // Release pins
  port->end();
}

// Streams

// Simplify board differences
#if defined(ARDUINO_ADAFRUIT_QTPY_ESP32S2)
#define ULISP_I2C1
#endif

inline int spiread () { return SPI.transfer(0); }
inline int i2cread () { return I2Cread(&Wire); }
#if defined(ULISP_I2C1)
inline int i2c1read () { return I2Cread(&Wire1); }
#endif
inline int serial1read () { while (!Serial1.available()) testescape(); return Serial1.read(); }
#if defined(sdcardsupport)
File SDpfile, SDgfile;
inline int SDread () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  return SDgfile.read();
}
#endif

WiFiClient client;
WiFiServer server(80);

inline int WiFiread () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  while (!client.available()) testescape();
  return client.read();
}

void serialbegin (int address, int baud) {
  if (address == 1) Serial1.begin((long)baud*100);
  else error("port not supported", number(address));
}

void serialend (int address) {
  if (address == 1) {Serial1.flush(); Serial1.end(); }
  else error("port not supported", number(address));
}

gfun_t gstreamfun (object *args) {
  int streamtype = SERIALSTREAM;
  int address = 0;
  gfun_t gfun = gserial;
  if (args != NULL) {
    int stream = isstream(first(args));
    streamtype = stream>>8; address = stream & 0xFF;
  }
  if (streamtype == I2CSTREAM) {
    if (address < 128) gfun = i2cread;
    #if defined(ULISP_I2C1)
    else gfun = i2c1read;
    #endif
  } else if (streamtype == SPISTREAM) gfun = spiread;
  else if (streamtype == SERIALSTREAM) {
    if (address == 0) gfun = gserial;
    else if (address == 1) gfun = serial1read;
  }
  #if defined(sdcardsupport)
  else if (streamtype == SDSTREAM) gfun = (gfun_t)SDread;
  #endif
  else if (streamtype == WIFISTREAM) gfun = (gfun_t)WiFiread;
  else error2("unknown stream type");
  return gfun;
}

inline void spiwrite (char c) { SPI.transfer(c); }
inline void i2cwrite (char c) { I2Cwrite(&Wire, c); }
#if defined(ULISP_I2C1)
inline void i2c1write (char c) { I2Cwrite(&Wire1, c); }
#endif
inline void serial1write (char c) { Serial1.write(c); }
inline void WiFiwrite (char c) { client.write(c); }
#if defined(sdcardsupport)
inline void SDwrite (char c) { SDpfile.write(c); }
#endif
#if defined(gfxsupport)
inline void gfxwrite (char c) { tft.write(c); }
#endif

pfun_t pstreamfun (object *args) {
  int streamtype = SERIALSTREAM;
  int address = 0;
  pfun_t pfun = pserial;
  if (args != NULL && first(args) != NULL) {
    int stream = isstream(first(args));
    streamtype = stream>>8; address = stream & 0xFF;
  }
  if (streamtype == I2CSTREAM) {
    if (address < 128) pfun = i2cwrite;
    #if defined(ULISP_I2C1)
    else pfun = i2c1write;
    #endif
  } else if (streamtype == SPISTREAM) pfun = spiwrite;
  else if (streamtype == SERIALSTREAM) {
    if (address == 0) pfun = pserial;
    else if (address == 1) pfun = serial1write;
  }
  else if (streamtype == STRINGSTREAM) {
    pfun = pstr;
  }
  #if defined(sdcardsupport)
  else if (streamtype == SDSTREAM) pfun = (pfun_t)SDwrite;
  #endif
  #if defined(gfxsupport)
  else if (streamtype == GFXSTREAM) pfun = (pfun_t)gfxwrite;
  #endif
  else if (streamtype == WIFISTREAM) pfun = (pfun_t)WiFiwrite;
  else error2("unknown stream type");
  return pfun;
}

// Check pins

void checkanalogread (int pin) {
#if defined(ARDUINO_ESP32S3_DEV)
  if (!((pin>=1 && pin<=20))) error("invalid pin", number(pin));
#endif
}

void checkanalogwrite (int pin) {
#if defined(ARDUINO_ESP32S3_DEV)
  error2("not supported");
#endif
}

// Note

void tone (int pin, int freq, uint16_t duration) {
  const int samplerate = 8000;
  int halfwave = samplerate / freq, count = 0, amplitude = 500;
  unsigned long start = millis();
  if (!I2S.begin(I2S_PHILIPS_MODE, samplerate, 16)) error2("error with i2s");
  while (millis() < start + duration) {
    if (count % halfwave == 0) amplitude = -1 * amplitude;
    I2S.write(amplitude); I2S.write(amplitude);
    count++;
  }
  I2S.end();
}

void noTone (int pin) {
  (void) pin;
}

const int scale[] = {4186,4435,4699,4978,5274,5588,5920,6272,6645,7040,7459,7902};

void playnote (int pin, int note, int octave, uint16_t duration) {
  int prescaler = 8 - octave - note/12;
  if (prescaler<0 || prescaler>8) error("octave out of range", number(prescaler));
  tone(pin, pgm_read_word(&scale[note%12])>>prescaler, duration);
}

void nonote (int pin) {
  noTone(pin);
}

// Sleep

void initsleep () { }

void doze (int secs) {
  delay(1000 * secs);
}

// Prettyprint

const int PPINDENT = 2;
const int PPWIDTH = 52;
const int GFXPPWIDTH = 52; // 320 pixel wide screen
int ppwidth = PPWIDTH;

void pcount (char c) {
  if (c == '\n') PrintCount++;
  PrintCount++;
}

uint8_t atomwidth (object *obj) {
  PrintCount = 0;
  printobject(obj, pcount);
  return PrintCount;
}

uint8_t basewidth (object *obj, uint8_t base) {
  PrintCount = 0;
  pintbase(obj->integer, base, pcount);
  return PrintCount;
}

bool quoted (object *obj) {
  return (consp(obj) && car(obj) != NULL && car(obj)->name == sym(QUOTE) && consp(cdr(obj)) && cddr(obj) == NULL);
}

int subwidth (object *obj, int w) {
  if (atom(obj)) return w - atomwidth(obj);
  if (quoted(obj)) obj = car(cdr(obj));
  return subwidthlist(obj, w - 1);
}

int subwidthlist (object *form, int w) {
  while (form != NULL && w >= 0) {
    if (atom(form)) return w - (2 + atomwidth(form));
    w = subwidth(car(form), w - 1);
    form = cdr(form);
  }
  return w;
}

bool highlighted (object *obj) {
  return (consp(obj) && car(obj) != NULL && car(obj)->name == sym(HIGHLIGHT));
}

const char STX = 2; // Code to invert text
const char ETX = 3; // Code to invert text

void superprint (object *form, int lm, bool match, pfun_t pfun) {
  if (atom(form)) {
    if (symbolp(form) && form->name == sym(NOTHING)) printsymbol(form, pfun);
    else printobject(form, pfun);
  } else if (quoted(form)) {
    pfun('\'');
    superprint(car(cdr(form)), lm + 1, match, pfun);
  } else if (highlighted(form)) {
    pfun(STX);
    superprint(car(cdr(form)), lm, true, pfun);
    pfun(ETX);
  } else {
    lm = lm + PPINDENT;
    bool fits = (subwidth(form, PPWIDTH - lm - PPINDENT) >= 0);
    int special = 0; bool separate = true, hilite = false;
    object *arg = car(form);
    if (symbolp(arg) && builtinp(arg->name)) {
      uint8_t minmax = getminmax(builtin(arg->name));
      if (minmax == 0327 || minmax == 0313) special = 2; // defun, setq, setf, defvar
      else if (minmax == 0317 || minmax == 0017 || minmax == 0117 || minmax == 0123) special = 1;
    }
    while (form != NULL) {
      if (atom(form)) { pfstring(" . ", pfun); printobject(form, pfun); pfun(')'); return; }
      object *arg = car(form);
      if (symbolp(arg) && arg->name == sym(HIGHLIGHT)) {
        hilite = true;
        form = car(cdr(form));
      }
      if (separate) { 
        pfun('(');
        separate = false;
      } else if (special != 0) {
        if (hilite) pfun(' ' | 0x80); else pfun(' ');
        special--; 
      } else if (fits) {
        if (hilite) pfun(' ' | 0x80); else pfun(' ');
      } else {
        pln(pfun);
        if (match) pfun(ETX);
        indent(lm-1, ' ', pfun);
        if (hilite) pfun(' ' | 0x80); else pfun(' ');
        if (match) pfun(STX);
      }
      hilite = false;
      if (form != NULL) { superprint(car(form), lm, match, pfun); form = cdr(form); }
    }
    pfun(')');
    if (!match) pfun(ETX);
  }
}

// void superprint (object *form, int lm, bool match, pfun_t pfun) {
//   if (atom(form)) {
//     if (isbuiltin(form, NOTHING)) printsymbol(form, pfun);
//     else printobject(form, pfun);
//   } else if (quoted(form)) {
//     pfun('\'');
//     superprint(car(cdr(form)), lm + 1, false, pfun);
//   } else {
//     lm = lm + PPINDENT;
//     bool fits = (subwidth(form, ppwidth - lm - PPINDENT) >= 0);
//     int special = 0, extra = 0; bool separate = true;
//     object *arg = car(form);
//     if (symbolp(arg) && builtinp(arg->name)) {
//       uint8_t minmax = getminmax(builtin(arg->name));
//       if (minmax == 0327 || minmax == 0313) special = 2; // defun, setq, setf, defvar
//       else if (minmax == 0317 || minmax == 0017 || minmax == 0117 || minmax == 0123) special = 1;
//     }
//     while (form != NULL) {
//       if (atom(form)) { pfstring(" . ", pfun); printobject(form, pfun); pfun(')'); return; }
//       else if (separate) { 
//         pfun('(');
//         separate = false;
//       } else if (special) {
//         pfun(' ');
//         special--; 
//       } else if (fits) {
//         pfun(' ');
//       } else { pln(pfun); indent(lm, ' ', pfun); }
//       superprint(car(form), lm+extra, false, pfun);
//       form = cdr(form);
//     }
//     pfun(')');
//   }
// }

object *edit (object *fun) {
  while (1) {
    if (tstflag(EXITEDITOR)) return fun;
    char c = gserial();
    if (c == 'q') setflag(EXITEDITOR);
    else if (c == 'b') return fun;
    else if (c == 'r') fun = read(gserial);
    else if (c == '\n') { pfl(pserial); superprint(fun, 0, false, pserial); pln(pserial); }
    else if (c == 'c') fun = cons(read(gserial), fun);
    else if (atom(fun)) pserial('!');
    else if (c == 'd') fun = cons(car(fun), edit(cdr(fun)));
    else if (c == 'a') fun = cons(edit(car(fun)), cdr(fun));
    else if (c == 'x') fun = cdr(fun);
    else pserial('?');
  }
}

// Special forms

object *sp_quote (object *args, object *env) {
  (void) env;
  return first(args);
}

object *sp_or (object *args, object *env) {
  while (args != NULL) {
    object *val = eval(car(args), env);
    if (val != NULL) return val;
    args = cdr(args);
  }
  return nil;
}

object *sp_defun (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(notasymbol, var);
  object *val = cons(bsymbol(LAMBDA), cdr(args));
  object *pair = value(var->name, GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

object *sp_defvar (object *args, object *env) {
  object *var = first(args);
  if (!symbolp(var)) error(notasymbol, var);
  object *val = NULL;
  args = cdr(args);
  if (args != NULL) { setflag(NOESC); val = eval(first(args), env); clrflag(NOESC); }
  object *pair = value(var->name, GlobalEnv);
  if (pair != NULL) cdr(pair) = val;
  else push(cons(var, val), GlobalEnv);
  return var;
}

object *sp_setq (object *args, object *env) {
  object *arg = nil; builtin_t setq = Context;
  while (args != NULL) {
    if (cdr(args) == NULL) { Context = setq; error2(oddargs); }
    object *pair = findvalue(first(args), env);
    arg = eval(second(args), env);
    cdr(pair) = arg;
    args = cddr(args);
  }
  return arg;
}

object *sp_loop (object *args, object *env) {
  object *start = args;
  for (;;) {
    yield();
    args = start;
    while (args != NULL) {
      object *result = eval(car(args),env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        return result;
      }
      args = cdr(args);
    }
    testescape();
  }
}

object *sp_push (object *args, object *env) {
  int bit;
  object *item = eval(first(args), env);
  object **loc = place(second(args), env, &bit);
  if (bit != -1) error2(invalidarg);
  push(item, *loc);
  return *loc;
}

object *sp_pop (object *args, object *env) {
  int bit;
  object *arg = first(args);
  if (arg == NULL) error2(invalidarg);
  object **loc = place(arg, env, &bit);
  if (bit < -1) error(invalidarg, arg);
  if (!consp(*loc)) error(notalist, *loc);
  object *result = car(*loc);
  pop(*loc);
  return result;
}

// Accessors

object *sp_incf (object *args, object *env) {
  int bit;
  object **loc = place(first(args), env, &bit);
  if (bit < -1) error2(notanumber);
  args = cdr(args);

  object *x = *loc;
  object *inc = (args != NULL) ? eval(first(args), env) : NULL;

  if (bit != -1) {
    int increment;
    if (inc == NULL) increment = 1; else increment = checkbitvalue(inc);
    int newvalue = (((*loc)->integer)>>bit & 1) + increment;

    if (newvalue & ~1) error2("result is not a bit value");
    *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
    return number(newvalue);
  }

  if (floatp(x) || floatp(inc)) {
    float increment;
    float value = checkintfloat(x);

    if (inc == NULL) increment = 1.0; else increment = checkintfloat(inc);

    *loc = makefloat(value + increment);
  } else if (integerp(x) && (integerp(inc) || inc == NULL)) {
    int increment;
    int value = x->integer;

    if (inc == NULL) increment = 1; else increment = inc->integer;

    if (increment < 1) {
      if (INT_MIN - increment > value) *loc = makefloat((float)value + (float)increment);
      else *loc = number(value + increment);
    } else {
      if (INT_MAX - increment < value) *loc = makefloat((float)value + (float)increment);
      else *loc = number(value + increment);
    }
  } else error2(notanumber);
  return *loc;
}

object *sp_decf (object *args, object *env) {
  int bit;
  object **loc = place(first(args), env, &bit);
  if (bit < -1) error2(notanumber);
  args = cdr(args);

  object *x = *loc;
  object *dec = (args != NULL) ? eval(first(args), env) : NULL;

  if (bit != -1) {
    int decrement;
    if (dec == NULL) decrement = 1; else decrement = checkbitvalue(dec);
    int newvalue = (((*loc)->integer)>>bit & 1) - decrement;

    if (newvalue & ~1) error2("result is not a bit value");
    *loc = number((((*loc)->integer) & ~(1<<bit)) | newvalue<<bit);
    return number(newvalue);
  }

  if (floatp(x) || floatp(dec)) {
    float decrement;
    float value = checkintfloat(x);

    if (dec == NULL) decrement = 1.0; else decrement = checkintfloat(dec);

    *loc = makefloat(value - decrement);
  } else if (integerp(x) && (integerp(dec) || dec == NULL)) {
    int decrement;
    int value = x->integer;

    if (dec == NULL) decrement = 1; else decrement = dec->integer;

    if (decrement < 1) {
      if (INT_MAX + decrement < value) *loc = makefloat((float)value - (float)decrement);
      else *loc = number(value - decrement);
    } else {
      if (INT_MIN + decrement > value) *loc = makefloat((float)value - (float)decrement);
      else *loc = number(value - decrement);
    }
  } else error2(notanumber);
  return *loc;
}

object *sp_setf (object *args, object *env) {
  int bit; builtin_t setf = Context;
  object *arg = nil;
  while (args != NULL) {
    if (cdr(args) == NULL) { Context = setf; error2(oddargs); }
    object **loc = place(first(args), env, &bit);
    arg = eval(second(args), env);
    if (bit == -1) *loc = arg;
    else if (bit < -1) (*loc)->chars = ((*loc)->chars & ~(0xff<<((-bit-2)<<3))) | checkchar(arg)<<((-bit-2)<<3);
    else *loc = number((checkinteger(*loc) & ~(1<<bit)) | checkbitvalue(arg)<<bit);
    args = cddr(args);
  }
  return arg;
}

// Other special forms

object *sp_dolist (object *args, object *env) {
  object *params = checkarguments(args, 2, 3);
  object *var = first(params);
  object *list = eval(second(params), env);
  protect(list); // Don't GC the list
  object *pair = cons(var,nil);
  push(pair,env);
  params = cddr(params);
  args = cdr(args);
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    cdr(pair) = first(list);
    object *forms = args;
    while (forms != NULL) {
      object *result = eval(car(forms), env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        unprotect();
        return result;
      }
      forms = cdr(forms);
    }
    list = cdr(list);
  }
  cdr(pair) = nil;
  unprotect();
  if (params == NULL) return nil;
  return eval(car(params), env);
}

object *sp_dotimes (object *args, object *env) {
  object *params = checkarguments(args, 2, 3);
  object *var = first(params);
  int count = checkinteger(eval(second(params), env));
  int index = 0;
  params = cddr(params);
  object *pair = cons(var,number(0));
  push(pair,env);
  args = cdr(args);
  while (index < count) {
    cdr(pair) = number(index);
    object *forms = args;
    while (forms != NULL) {
      object *result = eval(car(forms), env);
      if (tstflag(RETURNFLAG)) {
        clrflag(RETURNFLAG);
        return result;
      }
      forms = cdr(forms);
    }
    index++;
  }
  cdr(pair) = number(index);
  if (params == NULL) return nil;
  return eval(car(params), env);
}

object *sp_do (object *args, object *env) {
  return dobody(args, env, false);
}

object *sp_dostar (object *args, object *env) {
  return dobody(args, env, true);
}

object *sp_trace (object *args, object *env) {
  (void) env;
  while (args != NULL) {
    object *var = first(args);
    if (!symbolp(var)) error(notasymbol, var);
    trace(var->name);
    args = cdr(args);
  }
  int i = 0;
  while (i < TRACEMAX) {
    if (TraceFn[i] != 0) args = cons(symbol(TraceFn[i]), args);
    i++;
  }
  return args;
}

object *sp_untrace (object *args, object *env) {
  (void) env;
  if (args == NULL) {
    int i = 0;
    while (i < TRACEMAX) {
      if (TraceFn[i] != 0) args = cons(symbol(TraceFn[i]), args);
      TraceFn[i] = 0;
      i++;
    }
  } else {
    while (args != NULL) {
      object *var = first(args);
      if (!symbolp(var)) error(notasymbol, var);
      untrace(var->name);
      args = cdr(args);
    }
  }
  return args;
}

object *sp_formillis (object *args, object *env) {
  object *param = checkarguments(args, 0, 1);
  unsigned long start = millis();
  unsigned long now, total = 0;
  if (param != NULL) total = checkinteger(eval(first(param), env));
  eval(tf_progn(cdr(args),env), env);
  do {
    now = millis() - start;
    testescape();
  } while (now < total);
  if (now <= INT_MAX) return number(now);
  return nil;
}

object *sp_time (object *args, object *env) {
  unsigned long start = millis();
  object *result = eval(first(args), env);
  unsigned long elapsed = millis() - start;
  printobject(result, pserial);
  pfstring("\nTime: ", pserial);
  if (elapsed < 1000) {
    pint(elapsed, pserial);
    pfstring(" ms\n", pserial);
  } else {
    elapsed = elapsed+50;
    pint(elapsed/1000, pserial);
    pserial('.'); pint((elapsed/100)%10, pserial);
    pfstring(" s\n", pserial);
  }
  return bsymbol(NOTHING);
}

object *sp_withoutputtostring (object *args, object *env) {
  object *params = checkarguments(args, 1, 1);
  object *var = first(params);
  object *pair = cons(var, stream(STRINGSTREAM, 0));
  push(pair,env);
  object *string = startstring();
  protect(string);
  object *forms = cdr(args);
  eval(tf_progn(forms,env), env);
  unprotect();
  return string;
}

object *sp_withserial (object *args, object *env) {
  object *params = checkarguments(args, 2, 3);
  object *var = first(params);
  int address = checkinteger(eval(second(params), env));
  params = cddr(params);
  int baud = 96;
  if (params != NULL) baud = checkinteger(eval(first(params), env));
  object *pair = cons(var, stream(SERIALSTREAM, address));
  push(pair,env);
  serialbegin(address, baud);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  serialend(address);
  return result;
}

object *sp_withi2c (object *args, object *env) {
  object *params = checkarguments(args, 2, 4);
  object *var = first(params);
  int address = checkinteger(eval(second(params), env));
  params = cddr(params);
  if ((address == 0 || address == 1) && params != NULL) {
    address = address * 128 + checkinteger(eval(first(params), env));
    params = cdr(params);
  }
  int read = 0; // Write
  I2Ccount = 0;
  if (params != NULL) {
    object *rw = eval(first(params), env);
    if (integerp(rw)) I2Ccount = rw->integer;
    read = (rw != NULL);
  }
  // Top bit of address is I2C port
  TwoWire *port = &Wire;
  #if defined(ULISP_I2C1)
  if (address > 127) port = &Wire1;
  #endif
  I2Cinit(port, 1); // Pullups
  object *pair = cons(var, (I2Cstart(port, address & 0x7F, read)) ? stream(I2CSTREAM, address) : nil);
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  I2Cstop(port, read);
  return result;
}

object *sp_withspi (object *args, object *env) {
  object *params = checkarguments(args, 2, 6);
  object *var = first(params);
  params = cdr(params);
  if (params == NULL) error2(nostream);
  int pin = checkinteger(eval(car(params), env));
  pinMode(pin, OUTPUT);
  digitalWrite(pin, HIGH);
  params = cdr(params);
  int clock = 4000, mode = SPI_MODE0; // Defaults
  int bitorder = MSBFIRST;
  if (params != NULL) {
    clock = checkinteger(eval(car(params), env));
    params = cdr(params);
    if (params != NULL) {
      bitorder = (checkinteger(eval(car(params), env)) == 0) ? LSBFIRST : MSBFIRST;
      params = cdr(params);
      if (params != NULL) {
        int modeval = checkinteger(eval(car(params), env));
        mode = (modeval == 3) ? SPI_MODE3 : (modeval == 2) ? SPI_MODE2 : (modeval == 1) ? SPI_MODE1 : SPI_MODE0;
      }
    }
  }
  object *pair = cons(var, stream(SPISTREAM, pin));
  push(pair,env);
  SPI.begin();
  SPI.beginTransaction(SPISettings(((unsigned long)clock * 1000), bitorder, mode));
  digitalWrite(pin, LOW);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  digitalWrite(pin, HIGH);
  SPI.endTransaction();
  return result;
}

object *sp_withsdcard (object *args, object *env) {
  #if defined(sdcardsupport)
  object *params = checkarguments(args, 2, 3);
  object *var = first(params);
  params = cdr(params);
  if (params == NULL) error2("no filename specified");
  builtin_t temp = Context;
  object *filename = eval(first(params), env);
  Context = temp;
  if (!stringp(filename)) error("filename is not a string", filename);
  params = cdr(params);
  SDBegin();
  int mode = 0;
  if (params != NULL && first(params) != NULL) mode = checkinteger(first(params));
  const char *oflag = FILE_READ;
  if (mode == 1) oflag = FILE_APPEND; else if (mode == 2) oflag = FILE_WRITE;
  if (mode >= 1) {
    char buffer[BUFFERSIZE*4];
    SDpfile = SD.open(MakeFilename(filename, buffer), oflag);
    if (!SDpfile) error2("problem writing to SD card or invalid filename");
  } else {
    char buffer[BUFFERSIZE*4];
    SDgfile = SD.open(MakeFilename(filename, buffer), oflag);
    if (!SDgfile) error2("problem reading from SD card or invalid filename");
  }
  object *pair = cons(var, stream(SDSTREAM, 1));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  if (mode >= 1) SDpfile.close(); else SDgfile.close();
  return result;
  #else
  (void) args, (void) env;
  error2("not supported");
  return nil;
  #endif
}

// Tail-recursive forms

object *tf_progn (object *args, object *env) {
  if (args == NULL) return nil;
  object *more = cdr(args);
  while (more != NULL) {
    object *result = eval(car(args),env);
    if (tstflag(RETURNFLAG)) return quote(result);
    args = more;
    more = cdr(args);
  }
  return car(args);
}

object *tf_if (object *args, object *env) {
  if (args == NULL || cdr(args) == NULL) error2(toofewargs);
  if (eval(first(args), env) != nil) return second(args);
  args = cddr(args);
  return (args != NULL) ? first(args) : nil;
}

object *tf_cond (object *args, object *env) {
  while (args != NULL) {
    object *clause = first(args);
    if (!consp(clause)) error(illegalclause, clause);
    object *test = eval(first(clause), env);
    object *forms = cdr(clause);
    if (test != nil) {
      if (forms == NULL) return quote(test); else return tf_progn(forms, env);
    }
    args = cdr(args);
  }
  return nil;
}

object *tf_when (object *args, object *env) {
  if (args == NULL) error2(noargument);
  if (eval(first(args), env) != nil) return tf_progn(cdr(args),env);
  else return nil;
}

object *tf_unless (object *args, object *env) {
  if (args == NULL) error2(noargument);
  if (eval(first(args), env) != nil) return nil;
  else return tf_progn(cdr(args),env);
}

object *tf_case (object *args, object *env) {
  object *test = eval(first(args), env);
  args = cdr(args);
  while (args != NULL) {
    object *clause = first(args);
    if (!consp(clause)) error(illegalclause, clause);
    object *key = car(clause);
    object *forms = cdr(clause);
    if (consp(key)) {
      while (key != NULL) {
        if (eq(test,car(key))) return tf_progn(forms, env);
        key = cdr(key);
      }
    } else if (eq(test,key) || eq(key,tee)) return tf_progn(forms, env);
    args = cdr(args);
  }
  return nil;
}

object *tf_and (object *args, object *env) {
  if (args == NULL) return tee;
  object *more = cdr(args);
  while (more != NULL) {
    if (eval(car(args), env) == NULL) return nil;
    args = more;
    more = cdr(args);
  }
  return car(args);
}

// Core functions

object *fn_not (object *args, object *env) {
  (void) env;
  return (first(args) == nil) ? tee : nil;
}

object *fn_cons (object *args, object *env) {
  (void) env;
  return cons(first(args), second(args));
}

object *fn_atom (object *args, object *env) {
  (void) env;
  return atom(first(args)) ? tee : nil;
}

object *fn_listp (object *args, object *env) {
  (void) env;
  return listp(first(args)) ? tee : nil;
}

object *fn_consp (object *args, object *env) {
  (void) env;
  return consp(first(args)) ? tee : nil;
}

object *fn_symbolp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (arg == NULL || symbolp(arg)) ? tee : nil;
}

object *fn_arrayp (object *args, object *env) {
  (void) env;
  return arrayp(first(args)) ? tee : nil;
}

object *fn_boundp (object *args, object *env) {
  return boundp(first(args), env) ? tee : nil;
}

object *fn_keywordp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!symbolp(arg)) return nil;
  return (keywordp(arg) || colonp(arg->name)) ? tee : nil;
}

object *fn_setfn (object *args, object *env) {
  object *arg = nil;
  while (args != NULL) {
    if (cdr(args) == NULL) error2(oddargs);
    object *pair = findvalue(first(args), env);
    arg = second(args);
    cdr(pair) = arg;
    args = cddr(args);
  }
  return arg;
}

object *fn_streamp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return streamp(arg) ? tee : nil;
}

object *fn_eq (object *args, object *env) {
  (void) env;
  return eq(first(args), second(args)) ? tee : nil;
}

object *fn_equal (object *args, object *env) {
  (void) env;
  return equal(first(args), second(args)) ? tee : nil;
}

// List functions

object *fn_car (object *args, object *env) {
  (void) env;
  return carx(first(args));
}

object *fn_cdr (object *args, object *env) {
  (void) env;
  return cdrx(first(args));
}

object *fn_caar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b100);
}

object *fn_cadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b101);
}

object *fn_cdar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b110);
}

object *fn_cddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b111);
}

object *fn_caaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1000);
}

object *fn_caadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1001);;
}

object *fn_cadar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1010);
}

object *fn_caddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1011);
}

object *fn_cdaar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1100);
}

object *fn_cdadr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1101);
}

object *fn_cddar (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1110);
}

object *fn_cdddr (object *args, object *env) {
  (void) env;
  return cxxxr(args, 0b1111);
}

object *fn_length (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (listp(arg)) return number(listlength(arg));
  if (stringp(arg)) return number(stringlength(arg));
  if (!(arrayp(arg) && cdr(cddr(arg)) == NULL)) error("argument is not a list, 1d array, or string", arg);
  return number(abs(first(cddr(arg))->integer));
}

object *fn_arraydimensions (object *args, object *env) {
  (void) env;
  object *array = first(args);
  if (!arrayp(array)) error("argument is not an array", array);
  object *dimensions = cddr(array);
  return (first(dimensions)->integer < 0) ? cons(number(-(first(dimensions)->integer)), cdr(dimensions)) : dimensions;
}

object *fn_list (object *args, object *env) {
  (void) env;
  return args;
}

object *fn_copylist (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!listp(arg)) error(notalist, arg);
  object *result = cons(NULL, NULL);
  object *ptr = result;
  while (arg != NULL) {
    cdr(ptr) = cons(car(arg), NULL); 
    ptr = cdr(ptr); arg = cdr(arg);
  }
  return cdr(result);
}

object *fn_makearray (object *args, object *env) {
  (void) env;
  object *def = nil;
  bool bitp = false;
  object *dims = first(args);
  if (dims == NULL) error2("dimensions can't be nil");
  else if (atom(dims)) dims = cons(dims, NULL);
  args = cdr(args);
  while (args != NULL && cdr(args) != NULL) {
    object *var = first(args);
    if (isbuiltin(first(args), INITIALELEMENT)) def = second(args);
    else if (isbuiltin(first(args), ELEMENTTYPE) && isbuiltin(second(args), BIT)) bitp = true;
    else error("argument not recognised", var);
    args = cddr(args);
  }
  if (bitp) {
    if (def == nil) def = number(0);
    else def = number(-checkbitvalue(def)); // 1 becomes all ones
  }
  return makearray(dims, def, bitp);
}

object *fn_reverse (object *args, object *env) {
  (void) env;
  object *list = first(args);
  object *result = NULL;
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    push(first(list),result);
    list = cdr(list);
  }
  return result;
}

object *fn_nth (object *args, object *env) {
  (void) env;
  int n = checkinteger(first(args));
  if (n < 0) error(indexnegative, first(args));
  object *list = second(args);
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    if (n == 0) return car(list);
    list = cdr(list);
    n--;
  }
  return nil;
}

object *fn_aref (object *args, object *env) {
  (void) env;
  int bit;
  object *array = first(args);
  if (!arrayp(array)) error("first argument is not an array", array);
  object *loc = *getarray(array, cdr(args), 0, &bit);
  if (bit == -1) return loc;
  else return number((loc->integer)>>bit & 1);
}

object *fn_assoc (object *args, object *env) {
  (void) env;
  object *key = first(args);
  object *list = second(args);
  object *test = testargument(cddr(args));
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    object *pair = first(list);
    if (!listp(pair)) error("element is not a list", pair);
    if (pair != NULL && apply(test, cons(key, cons(car(pair), NULL)), env) != NULL) return pair;
    list = cdr(list);
  }
  return nil;
}

object *fn_member (object *args, object *env) {
  (void) env;
  object *item = first(args);
  object *list = second(args);
  object *test = testargument(cddr(args));
  while (list != NULL) {
    if (improperp(list)) error(notproper, list);
    if (apply(test, cons(item, cons(car(list), NULL)), env) != NULL) return list;
    list = cdr(list);
  }
  return nil;
}

object *fn_apply (object *args, object *env) {
  object *previous = NULL;
  object *last = args;
  while (cdr(last) != NULL) {
    previous = last;
    last = cdr(last);
  }
  object *arg = car(last);
  if (!listp(arg)) error(notalist, arg);
  cdr(previous) = arg;
  return apply(first(args), cdr(args), env);
}

object *fn_funcall (object *args, object *env) {
  return apply(first(args), cdr(args), env);
}

object *fn_append (object *args, object *env) {
  (void) env;
  object *head = NULL;
  object *tail;
  while (args != NULL) {
    object *list = first(args);
    if (!listp(list)) error(notalist, list);
    while (consp(list)) {
      object *obj = cons(car(list), cdr(list));
      if (head == NULL) head = obj;
      else cdr(tail) = obj;
      tail = obj;
      list = cdr(list);
      if (cdr(args) != NULL && improperp(list)) error(notproper, first(args));
    }
    args = cdr(args);
  }
  return head;
}

object *fn_mapc (object *args, object *env) {
  return mapcl(args, env, false);
}

object *fn_mapl (object *args, object *env) {
  return mapcl(args, env, true);
}

object *fn_mapcar (object *args, object *env) {
  return mapcarcan(args, env, mapcarfun, false);
}

object *fn_mapcan (object *args, object *env) {
  return mapcarcan(args, env, mapcanfun, false);
}

object *fn_maplist (object *args, object *env) {
  return mapcarcan(args, env, mapcarfun, true);
}

object *fn_mapcon (object *args, object *env) {
  return mapcarcan(args, env, mapcanfun, true);
}

// Arithmetic functions

object *fn_add (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    object *arg = car(args);
    if (floatp(arg)) return add_floats(args, (float)result);
    else if (integerp(arg)) {
      int val = arg->integer;
      if (val < 1) { if (INT_MIN - val > result) return add_floats(args, (float)result); }
      else { if (INT_MAX - val < result) return add_floats(args, (float)result); }
      result = result + val;
    } else error(notanumber, arg);
    args = cdr(args);
  }
  return number(result);
}

object *fn_subtract (object *args, object *env) {
  (void) env;
  object *arg = car(args);
  args = cdr(args);
  if (args == NULL) return negate(arg);
  else if (floatp(arg)) return subtract_floats(args, arg->single_float);
  else if (integerp(arg)) {
    int result = arg->integer;
    while (args != NULL) {
      arg = car(args);
      if (floatp(arg)) return subtract_floats(args, result);
      else if (integerp(arg)) {
        int val = (car(args))->integer;
        if (val < 1) { if (INT_MAX + val < result) return subtract_floats(args, result); }
        else { if (INT_MIN + val > result) return subtract_floats(args, result); }
        result = result - val;
      } else error(notanumber, arg);
      args = cdr(args);
    }
    return number(result);
  } else error(notanumber, arg);
  return nil;
}

object *fn_multiply (object *args, object *env) {
  (void) env;
  int result = 1;
  while (args != NULL){
    object *arg = car(args);
    if (floatp(arg)) return multiply_floats(args, result);
    else if (integerp(arg)) {
      int64_t val = result * (int64_t)(arg->integer);
      if ((val > INT_MAX) || (val < INT_MIN)) return multiply_floats(args, result);
      result = val;
    } else error(notanumber, arg);
    args = cdr(args);
  }
  return number(result);
}

object *fn_divide (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  args = cdr(args);
  // One argument
  if (args == NULL) {
    if (floatp(arg)) {
      float f = arg->single_float;
      if (f == 0.0) error2(divisionbyzero);
      return makefloat(1.0 / f);
    } else if (integerp(arg)) {
      int i = arg->integer;
      if (i == 0) error2(divisionbyzero);
      else if (i == 1) return number(1);
      else return makefloat(1.0 / i);
    } else error(notanumber, arg);
  }
  // Multiple arguments
  if (floatp(arg)) return divide_floats(args, arg->single_float);
  else if (integerp(arg)) {
    int result = arg->integer;
    while (args != NULL) {
      arg = car(args);
      if (floatp(arg)) {
        return divide_floats(args, result);
      } else if (integerp(arg)) {
        int i = arg->integer;
        if (i == 0) error2(divisionbyzero);
        if ((result % i) != 0) return divide_floats(args, result);
        if ((result == INT_MIN) && (i == -1)) return divide_floats(args, result);
        result = result / i;
        args = cdr(args);
      } else error(notanumber, arg);
    }
    return number(result);
  } else error(notanumber, arg);
  return nil;
}

object *fn_mod (object *args, object *env) {
  (void) env;
  return remmod(args, true);
}

object *fn_rem (object *args, object *env) {
  (void) env;
  return remmod(args, false);
}

object *fn_oneplus (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  if (floatp(arg)) return makefloat((arg->single_float) + 1.0);
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MAX) return makefloat((arg->integer) + 1.0);
    else return number(result + 1);
  } else error(notanumber, arg);
  return nil;
}

object *fn_oneminus (object *args, object *env) {
  (void) env;
  object* arg = first(args);
  if (floatp(arg)) return makefloat((arg->single_float) - 1.0);
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat((arg->integer) - 1.0);
    else return number(result - 1);
  } else error(notanumber, arg);
  return nil;
}

object *fn_abs (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return makefloat(abs(arg->single_float));
  else if (integerp(arg)) {
    int result = arg->integer;
    if (result == INT_MIN) return makefloat(abs((float)result));
    else return number(abs(result));
  } else error(notanumber, arg);
  return nil;
}

object *fn_random (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (integerp(arg)) return number(random(arg->integer));
  else if (floatp(arg)) return makefloat((float)rand()/(float)(RAND_MAX/(arg->single_float)));
  else error(notanumber, arg);
  return nil;
}

object *fn_maxfn (object *args, object *env) {
  (void) env;
  object* result = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg = car(args);
    if (integerp(result) && integerp(arg)) {
      if ((arg->integer) > (result->integer)) result = arg;
    } else if ((checkintfloat(arg) > checkintfloat(result))) result = arg;
    args = cdr(args);
  }
  return result;
}

object *fn_minfn (object *args, object *env) {
  (void) env;
  object* result = first(args);
  args = cdr(args);
  while (args != NULL) {
    object *arg = car(args);
    if (integerp(result) && integerp(arg)) {
      if ((arg->integer) < (result->integer)) result = arg;
    } else if ((checkintfloat(arg) < checkintfloat(result))) result = arg;
    args = cdr(args);
  }
  return result;
}

// Arithmetic comparisons

object *fn_noteq (object *args, object *env) {
  (void) env;
  while (args != NULL) {
    object *nargs = args;
    object *arg1 = first(nargs);
    nargs = cdr(nargs);
    while (nargs != NULL) {
      object *arg2 = first(nargs);
      if (integerp(arg1) && integerp(arg2)) {
        if ((arg1->integer) == (arg2->integer)) return nil;
      } else if ((checkintfloat(arg1) == checkintfloat(arg2))) return nil;
      nargs = cdr(nargs);
    }
    args = cdr(args);
  }
  return tee;
}

object *fn_numeq (object *args, object *env) {
  (void) env;
  return compare(args, false, false, true);
}

object *fn_less (object *args, object *env) {
  (void) env;
  return compare(args, true, false, false);
}

object *fn_lesseq (object *args, object *env) {
  (void) env;
  return compare(args, true, false, true);
}

object *fn_greater (object *args, object *env) {
  (void) env;
  return compare(args, false, true, false);
}

object *fn_greatereq (object *args, object *env) {
  (void) env;
  return compare(args, false, true, true);
}

object *fn_plusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) > 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) > 0) ? tee : nil;
  else error(notanumber, arg);
  return nil;
}

object *fn_minusp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) < 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) < 0) ? tee : nil;
  else error(notanumber, arg);
  return nil;
}

object *fn_zerop (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (floatp(arg)) return ((arg->single_float) == 0.0) ? tee : nil;
  else if (integerp(arg)) return ((arg->integer) == 0) ? tee : nil;
  else error(notanumber, arg);
  return nil;
}

object *fn_oddp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(first(args));
  return ((arg & 1) == 1) ? tee : nil;
}

object *fn_evenp (object *args, object *env) {
  (void) env;
  int arg = checkinteger(first(args));
  return ((arg & 1) == 0) ? tee : nil;
}

// Number functions

object *fn_integerp (object *args, object *env) {
  (void) env;
  return integerp(first(args)) ? tee : nil;
}

object *fn_numberp (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (integerp(arg) || floatp(arg)) ? tee : nil;
}

// Floating-point functions

object *fn_floatfn (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  return (floatp(arg)) ? arg : makefloat((float)(arg->integer));
}

object *fn_floatp (object *args, object *env) {
  (void) env;
  return floatp(first(args)) ? tee : nil;
}

object *fn_sin (object *args, object *env) {
  (void) env;
  return makefloat(sin(checkintfloat(first(args))));
}

object *fn_cos (object *args, object *env) {
  (void) env;
  return makefloat(cos(checkintfloat(first(args))));
}

object *fn_tan (object *args, object *env) {
  (void) env;
  return makefloat(tan(checkintfloat(first(args))));
}

object *fn_asin (object *args, object *env) {
  (void) env;
  return makefloat(asin(checkintfloat(first(args))));
}

object *fn_acos (object *args, object *env) {
  (void) env;
  return makefloat(acos(checkintfloat(first(args))));
}

object *fn_atan (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float div = 1.0;
  args = cdr(args);
  if (args != NULL) div = checkintfloat(first(args));
  return makefloat(atan2(checkintfloat(arg), div));
}

object *fn_sinh (object *args, object *env) {
  (void) env;
  return makefloat(sinh(checkintfloat(first(args))));
}

object *fn_cosh (object *args, object *env) {
  (void) env;
  return makefloat(cosh(checkintfloat(first(args))));
}

object *fn_tanh (object *args, object *env) {
  (void) env;
  return makefloat(tanh(checkintfloat(first(args))));
}

object *fn_exp (object *args, object *env) {
  (void) env;
  return makefloat(exp(checkintfloat(first(args))));
}

object *fn_sqrt (object *args, object *env) {
  (void) env;
  return makefloat(sqrt(checkintfloat(first(args))));
}

object *fn_log (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  float fresult = log(checkintfloat(arg));
  args = cdr(args);
  if (args == NULL) return makefloat(fresult);
  else return makefloat(fresult / log(checkintfloat(first(args))));
}

object *fn_expt (object *args, object *env) {
  (void) env;
  object *arg1 = first(args); object *arg2 = second(args);
  float float1 = checkintfloat(arg1);
  float value = log(abs(float1)) * checkintfloat(arg2);
  if (integerp(arg1) && integerp(arg2) && ((arg2->integer) >= 0) && (abs(value) < 21.4875))
    return number(intpower(arg1->integer, arg2->integer));
  if (float1 < 0) {
    if (integerp(arg2)) return makefloat((arg2->integer & 1) ? -exp(value) : exp(value));
    else error2("invalid result");
  }
  return makefloat(exp(value));
}

object *fn_ceiling (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(ceil(checkintfloat(arg) / checkintfloat(first(args))));
  else return number(ceil(checkintfloat(arg)));
}

object *fn_floor (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(floor(checkintfloat(arg) / checkintfloat(first(args))));
  else return number(floor(checkintfloat(arg)));
}

object *fn_truncate (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number((int)(checkintfloat(arg) / checkintfloat(first(args))));
  else return number((int)(checkintfloat(arg)));
}

object *fn_round (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  args = cdr(args);
  if (args != NULL) return number(round(checkintfloat(arg) / checkintfloat(first(args))));
  else return number(round(checkintfloat(arg)));
}

// Characters

object *fn_char (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (!stringp(arg)) error(notastring, arg);
  object *n = second(args);
  char c = nthchar(arg, checkinteger(n));
  if (c == 0) error(indexrange, n);
  return character(c);
}

object *fn_charcode (object *args, object *env) {
  (void) env;
  return number(checkchar(first(args)));
}

object *fn_codechar (object *args, object *env) {
  (void) env;
  return character(checkinteger(first(args)));
}

object *fn_characterp (object *args, object *env) {
  (void) env;
  return characterp(first(args)) ? tee : nil;
}

// Strings

object *fn_stringp (object *args, object *env) {
  (void) env;
  return stringp(first(args)) ? tee : nil;
}

object *fn_stringeq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, false, false, true);
  return m == -1 ? nil : tee;
}

object *fn_stringless (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, true, false, false);
  return m == -1 ? nil : number(m);
}

object *fn_stringgreater (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, false, true, false);
  return m == -1 ? nil : number(m);
}

object *fn_stringnoteq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, true, true, false);
  return m == -1 ? nil : number(m);
}

object *fn_stringlesseq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, true, false, true);
  return m == -1 ? nil : number(m);
}

object *fn_stringgreatereq (object *args, object *env) {
  (void) env;
  int m = stringcompare(args, false, true, true);
  return m == -1 ? nil : number(m);
}

object *fn_sort (object *args, object *env) {
  if (first(args) == NULL) return nil;
  object *list = cons(nil,first(args));
  protect(list);
  object *predicate = second(args);
  object *compare = cons(NULL, cons(NULL, NULL));
  protect(compare);
  object *ptr = cdr(list);
  while (cdr(ptr) != NULL) {
    object *go = list;
    while (go != ptr) {
      car(compare) = car(cdr(ptr));
      car(cdr(compare)) = car(cdr(go));
      if (apply(predicate, compare, env)) break;
      go = cdr(go);
    }
    if (go != ptr) {
      object *obj = cdr(ptr);
      cdr(ptr) = cdr(obj);
      cdr(obj) = cdr(go);
      cdr(go) = obj;
    } else ptr = cdr(ptr);
  }
  unprotect(); unprotect();
  return cdr(list);
}

object *fn_stringfn (object *args, object *env) {
  return fn_princtostring(args, env);
}

object *fn_concatenate (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  if (builtin(arg->name) != STRINGFN) error2("only supports strings");
  args = cdr(args);
  object *result = newstring();
  object *tail = result;
  while (args != NULL) {
    object *obj = checkstring(first(args));
    obj = cdr(obj);
    while (obj != NULL) {
      int quad = obj->chars;
      while (quad != 0) {
         char ch = quad>>((sizeof(int)-1)*8) & 0xFF;
         buildstring(ch, &tail);
         quad = quad<<8;
      }
      obj = car(obj);
    }
    args = cdr(args);
  }
  return result;
}

object *fn_subseq (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  int start = checkinteger(second(args)), end;
  if (start < 0) error(indexnegative, second(args));
  args = cddr(args);
  if (listp(arg)) {
    int length = listlength(arg);
    if (args != NULL) end = checkinteger(car(args)); else end = length;
    if (start > end || end > length) error2(indexrange);
    object *result = cons(NULL, NULL);
    object *ptr = result;
    for (int x = 0; x < end; x++) {
      if (x >= start) { cdr(ptr) = cons(car(arg), NULL); ptr = cdr(ptr); }
      arg = cdr(arg);
    }
    return cdr(result);
  } else if (stringp(arg)) {
    int length = stringlength(arg);
    if (args != NULL) end = checkinteger(car(args)); else end = length;
    if (start > end || end > length) error2(indexrange);
    object *result = newstring();
    object *tail = result;
    for (int i=start; i<end; i++) {
      char ch = nthchar(arg, i);
      buildstring(ch, &tail);
    }
    return result;
  } else error2("argument is not a list or string");
  return nil;
}

object *fn_search (object *args, object *env) {
  (void) env;
  object *pattern = first(args);
  object *target = second(args);
  if (pattern == NULL) return number(0);
  else if (target == NULL) return nil;
  
  else if (listp(pattern) && listp(target)) {
    object *test = testargument(cddr(args));
    int l = listlength(target);
    int m = listlength(pattern);
    for (int i = 0; i <= l-m; i++) {
      object *target1 = target;
      while (pattern != NULL && apply(test, cons(car(target1), cons(car(pattern), NULL)), env) != NULL) {
        pattern = cdr(pattern);
        target1 = cdr(target1);
      }
      if (pattern == NULL) return number(i);
      pattern = first(args); target = cdr(target);
    }
    return nil;

  } else if (stringp(pattern) && stringp(target)) {
    if (cddr(args) != NULL) error2("keyword argument not supported for strings");
    int l = stringlength(target);
    int m = stringlength(pattern);
    for (int i = 0; i <= l-m; i++) {
      int j = 0;
      while (j < m && nthchar(target, i+j) == nthchar(pattern, j)) j++;
      if (j == m) return number(i);
    }
    return nil;
  } else error2("arguments are not both lists or strings");
  return nil;
}

object *fn_readfromstring (object *args, object *env) {
  (void) env;
  object *arg = checkstring(first(args));
  GlobalString = arg;
  GlobalStringIndex = 0;
  object *val = read(gstr);
  LastChar = 0;
  return val;
}

object *fn_princtostring (object *args, object *env) {
  (void) env;
  return princtostring(first(args));
}

object *fn_prin1tostring (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  object *obj = startstring();
  printobject(arg, pstr);
  return obj;
}

// Bitwise operators

object *fn_logand (object *args, object *env) {
  (void) env;
  int result = -1;
  while (args != NULL) {
    result = result & checkinteger(first(args));
    args = cdr(args);
  }
  return number(result);
}

object *fn_logior (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result | checkinteger(first(args));
    args = cdr(args);
  }
  return number(result);
}

object *fn_logxor (object *args, object *env) {
  (void) env;
  int result = 0;
  while (args != NULL) {
    result = result ^ checkinteger(first(args));
    args = cdr(args);
  }
  return number(result);
}

object *fn_lognot (object *args, object *env) {
  (void) env;
  int result = checkinteger(car(args));
  return number(~result);
}

object *fn_ash (object *args, object *env) {
  (void) env;
  int value = checkinteger(first(args));
  int count = checkinteger(second(args));
  if (count >= 0) return number(value << count);
  else return number(value >> abs(count));
}

object *fn_logbitp (object *args, object *env) {
  (void) env;
  int index = checkinteger(first(args));
  int value = checkinteger(second(args));
  return (bitRead(value, index) == 1) ? tee : nil;
}

// System functions

object *fn_eval (object *args, object *env) {
  return eval(first(args), env);
}

object *fn_return (object *args, object *env) {
  (void) env;
  setflag(RETURNFLAG);
  if (args == NULL) return nil; else return first(args);
}

object *fn_globals (object *args, object *env) {
  (void) args, (void) env;
  object *result = cons(NULL, NULL);
  object *ptr = result;
  object *arg = GlobalEnv;
  while (arg != NULL) {
    cdr(ptr) = cons(car(car(arg)), NULL); ptr = cdr(ptr);
    arg = cdr(arg);
  }
  return cdr(result);
}

object *fn_locals (object *args, object *env) {
  (void) args;
  return env;
}

object *fn_makunbound (object *args, object *env) {
  (void) env;
  object *var = first(args);
  if (!symbolp(var)) error(notasymbol, var);
  delassoc(var, &GlobalEnv);
  return var;
}

object *fn_break (object *args, object *env) {
  (void) args;
  pfstring("\nBreak!\n", pserial);
  BreakLevel++;
  repl(env);
  BreakLevel--;
  return nil;
}

object *fn_read (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  return read(gfun);
}

object *fn_prin1 (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  printobject(obj, pfun);
  return obj;
}

object *fn_print (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  pln(pfun);
  printobject(obj, pfun);
  pfun(' ');
  return obj;
}

object *fn_princ (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  prin1object(obj, pfun);
  return obj;
}

object *fn_terpri (object *args, object *env) {
  (void) env;
  pfun_t pfun = pstreamfun(args);
  pln(pfun);
  return nil;
}

object *fn_readbyte (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  int c = gfun();
  return (c == -1) ? nil : number(c);
}

object *fn_readline (object *args, object *env) {
  (void) env;
  gfun_t gfun = gstreamfun(args);
  return readstring('\n', false, gfun);
}

object *fn_writebyte (object *args, object *env) {
  (void) env;
  int value = checkinteger(first(args));
  pfun_t pfun = pstreamfun(cdr(args));
  (pfun)(value);
  return nil;
}

object *fn_writestring (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  flags_t temp = Flags;
  clrflag(PRINTREADABLY);
  printstring(obj, pfun);
  Flags = temp;
  return nil;
}

object *fn_writeline (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  flags_t temp = Flags;
  clrflag(PRINTREADABLY);
  printstring(obj, pfun);
  pln(pfun);
  Flags = temp;
  return nil;
}

object *fn_restarti2c (object *args, object *env) {
  (void) env;
  int stream = isstream(first(args));
  args = cdr(args);
  int read = 0; // Write
  I2Ccount = 0;
  if (args != NULL) {
    object *rw = first(args);
    if (integerp(rw)) I2Ccount = rw->integer;
    read = (rw != NULL);
  }
  int address = stream & 0xFF;
  if (stream>>8 != I2CSTREAM) error2("not an i2c stream");
  TwoWire *port;
  if (address < 128) port = &Wire;
  #if defined(ULISP_I2C1)
  else port = &Wire1;
  #endif
  return I2Crestart(port, address & 0x7F, read) ? tee : nil;
}

object *fn_gc (object *args, object *env) {
  if (args == NULL || first(args) != NULL) {
    int initial = Freespace;
    unsigned long start = micros();
    gc(args, env);
    unsigned long elapsed = micros() - start;
    pfstring("Space: ", pserial);
    pint(Freespace - initial, pserial);
    pfstring(" bytes, Time: ", pserial);
    pint(elapsed, pserial);
    pfstring(" us\n", pserial);
  } else gc(args, env);
  return nil;
}

object *fn_room (object *args, object *env) {
  (void) args, (void) env;
  return number(Freespace);
}

object *fn_backtrace (object *args, object *env) {
  (void) env;
  if (args == NULL) return (tstflag(BACKTRACE)) ? tee : nil;
  if (first(args) == NULL) clrflag(BACKTRACE); else setflag(BACKTRACE);
  return first(args);
}

object *fn_saveimage (object *args, object *env) {
  if (args != NULL) args = eval(first(args), env);
  return number(saveimage(args));
}

object *fn_loadimage (object *args, object *env) {
  (void) env;
  if (args != NULL) args = first(args);
  return number(loadimage(args));
}

object *fn_cls (object *args, object *env) {
  (void) args, (void) env;
  pserial(12);
  return nil;
}

// Arduino procedures

object *fn_pinmode (object *args, object *env) {
  (void) env; int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else pin = checkinteger(first(args));
  int pm = INPUT;
  arg = second(args);
  if (keywordp(arg)) pm = checkkeyword(arg);
  else if (integerp(arg)) {
    int mode = arg->integer;
    if (mode == 1) pm = OUTPUT; else if (mode == 2) pm = INPUT_PULLUP;
    #if defined(INPUT_PULLDOWN)
    else if (mode == 4) pm = INPUT_PULLDOWN;
    #endif
  } else if (arg != nil) pm = OUTPUT;
  pinMode(pin, pm);
  return nil;
}

object *fn_digitalread (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else pin = checkinteger(arg);
  if (digitalRead(pin) != 0) return tee; else return nil;
}

object *fn_digitalwrite (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else pin = checkinteger(arg);
  arg = second(args);
  int mode;
  if (keywordp(arg)) mode = checkkeyword(arg);
  else if (integerp(arg)) mode = arg->integer ? HIGH : LOW;
  else mode = (arg != nil) ? HIGH : LOW;
  digitalWrite(pin, mode);
  return arg;
}

object *fn_analogread (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else {
    pin = checkinteger(arg);
    checkanalogread(pin);
  }
  return number(analogRead(pin));
}

object *fn_analogreadresolution (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  #if defined(ESP32)
  analogReadResolution(checkinteger(arg));
  #else
  error2("not supported");
  #endif
  return arg;
}

object *fn_analogwrite (object *args, object *env) {
  (void) env;
  int pin;
  object *arg = first(args);
  if (keywordp(arg)) pin = checkkeyword(arg);
  else pin = checkinteger(arg);
  checkanalogwrite(pin);
  object *value = second(args);
  analogWrite(pin, checkinteger(value));
  return value;
}

object *fn_delay (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  unsigned long start = millis();
  unsigned long total = checkinteger(arg1);
  do testescape();
  while (millis() - start < total);
  return arg1;
}

object *fn_millis (object *args, object *env) {
  (void) args, (void) env;
  return number(millis());
}

object *fn_sleep (object *args, object *env) {
  (void) env;
  object *arg1 = first(args);
  doze(checkinteger(arg1));
  return arg1;
}

object *fn_note (object *args, object *env) {
  (void) env;
  static int pin = 255;
  if (args != NULL) {
    pin = checkinteger(car(args));
    int note = 48, octave = 0; uint16_t duration = 0; // Duration mandatory on T-Deck
    args = cdr(args);
    if (args != NULL) {
      note = checkinteger(car(args));
      args = cdr(args);
      if (args != NULL) {
        octave = checkinteger(car(args));
        args = cdr(args);
        if (args != NULL) duration = checkinteger(car(args));
      }
    }
    playnote(pin, note, octave, duration);
  } else nonote(pin);
  return nil;
}

object *fn_register (object *args, object *env) {
  (void) env;
  object *arg = first(args);
  int addr;
  if (keywordp(arg)) addr = checkkeyword(arg);
  else addr = checkinteger(first(args));
  if (cdr(args) == NULL) return number(*(uint32_t *)addr);
  (*(uint32_t *)addr) = checkinteger(second(args));
  return second(args);
}

// Tree Editor

object *fn_edit (object *args, object *env) {
  object *fun = first(args);
  object *pair = findvalue(fun, env);
  clrflag(EXITEDITOR);
  object *arg = edit(eval(fun, env));
  cdr(pair) = arg;
  return arg;
}

// Pretty printer

object *fn_pprint (object *args, object *env) {
  (void) env;
  object *obj = first(args);
  pfun_t pfun = pstreamfun(cdr(args));
  #if defined(gfxsupport)
  if (pfun == gfxwrite) ppwidth = GFXPPWIDTH;
  #endif
  pln(pfun);
  superprint(obj, 0, false, pfun);
  ppwidth = PPWIDTH;
  return bsymbol(NOTHING);
}

object *fn_pprintall (object *args, object *env) {
  (void) env;
  pfun_t pfun = pstreamfun(args);
  #if defined(gfxsupport)
  if (pfun == gfxwrite) ppwidth = GFXPPWIDTH;
  #endif
  object *globals = GlobalEnv;
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    object *val = cdr(pair);
    pln(pfun);
    if (consp(val) && symbolp(car(val)) && builtin(car(val)->name) == LAMBDA) {
      superprint(cons(bsymbol(DEFUN), cons(var, cdr(val))), 0, false, pfun);
    } else {
      superprint(cons(bsymbol(DEFVAR), cons(var, cons(quote(val), NULL))), 0, false, pfun);
    }
    pln(pfun);
    testescape();
    globals = cdr(globals);
  }
  ppwidth = PPWIDTH;
  return bsymbol(NOTHING);
}

// Format

object *fn_format (object *args, object *env) {
  (void) env;
  pfun_t pfun = pserial;
  object *output = first(args);
  object *obj;
  if (output == nil) { obj = startstring(); pfun = pstr; }
  else if (!eq(output, tee)) pfun = pstreamfun(args);
  object *formatstr = checkstring(second(args));
  object *save = NULL;
  args = cddr(args);
  int len = stringlength(formatstr);
  uint8_t n = 0, width = 0, w, bra = 0;
  char pad = ' ';
  bool tilde = false, mute = false, comma = false, quote = false;
  while (n < len) {
    char ch = nthchar(formatstr, n);
    char ch2 = ch & ~0x20; // force to upper case
    if (tilde) {
     if (ch == '}') {
        if (save == NULL) formaterr(formatstr, "no matching ~{", n);
        if (args == NULL) { args = cdr(save); save = NULL; } else n = bra;
        mute = false; tilde = false;
      }
      else if (!mute) {
        if (comma && quote) { pad = ch; comma = false, quote = false; }
        else if (ch == '\'') {
          if (comma) quote = true;
          else formaterr(formatstr, "quote not valid", n);
        }
        else if (ch == '~') { pfun('~'); tilde = false; }
        else if (ch >= '0' && ch <= '9') width = width*10 + ch - '0';
        else if (ch == ',') comma = true;
        else if (ch == '%') { pln(pfun); tilde = false; }
        else if (ch == '&') { pfl(pfun); tilde = false; }
        else if (ch == '^') {
          if (save != NULL && args == NULL) mute = true;
          tilde = false;
        }
        else if (ch == '{') {
          if (save != NULL) formaterr(formatstr, "can't nest ~{", n);
          if (args == NULL) formaterr(formatstr, noargument, n);
          if (!listp(first(args))) formaterr(formatstr, notalist, n);
          save = args; args = first(args); bra = n; tilde = false;
          if (args == NULL) mute = true;
        }
        else if (ch2 == 'A' || ch2 == 'S' || ch2 == 'D' || ch2 == 'G' || ch2 == 'X' || ch2 == 'B') {
          if (args == NULL) formaterr(formatstr, noargument, n);
          object *arg = first(args); args = cdr(args);
          uint8_t aw = atomwidth(arg);
          if (width < aw) w = 0; else w = width-aw;
          tilde = false;
          if (ch2 == 'A') { prin1object(arg, pfun); indent(w, pad, pfun); }
          else if (ch2 == 'S') { printobject(arg, pfun); indent(w, pad, pfun); }
          else if (ch2 == 'D' || ch2 == 'G') { indent(w, pad, pfun); prin1object(arg, pfun); }
          else if (ch2 == 'X' || ch2 == 'B') {
            if (integerp(arg)) {
              uint8_t base = (ch2 == 'B') ? 2 : 16;
              uint8_t hw = basewidth(arg, base); if (width < hw) w = 0; else w = width-hw;
              indent(w, pad, pfun); pintbase(arg->integer, base, pfun);
            } else {
              indent(w, pad, pfun); prin1object(arg, pfun);
            }
          }
          tilde = false;
        } else formaterr(formatstr, "invalid directive", n);
      }
    } else {
      if (ch == '~') { tilde = true; pad = ' '; width = 0; comma = false; quote = false; }
      else if (!mute) pfun(ch);
    }
    n++;
  }
  if (output == nil) return obj;
  else return nil;
}

// LispLibrary

object *fn_require (object *args, object *env) {
  object *arg = first(args);
  object *globals = GlobalEnv;
  if (!symbolp(arg)) error(notasymbol, arg);
  while (globals != NULL) {
    object *pair = first(globals);
    object *var = car(pair);
    if (symbolp(var) && var == arg) return nil;
    globals = cdr(globals);
  }
  GlobalStringIndex = 0;
  object *line = read(glibrary);
  while (line != NULL) {
    // Is this the definition we want
    symbol_t fname = first(line)->name;
    if ((fname == sym(DEFUN) || fname == sym(DEFVAR)) && symbolp(second(line)) && second(line)->name == arg->name) {
      eval(line, env);
      return tee;
    }
    line = read(glibrary);
  }
  return nil;
}

object *fn_listlibrary (object *args, object *env) {
  (void) args, (void) env;
  GlobalStringIndex = 0;
  object *line = read(glibrary);
  while (line != NULL) {
    builtin_t bname = builtin(first(line)->name);
    if (bname == DEFUN || bname == DEFVAR) {
      printsymbol(second(line), pserial); pserial(' ');
    }
    line = read(glibrary);
  }
  return bsymbol(NOTHING);
}

// Documentation

object *sp_help (object *args, object *env) {
  if (args == NULL) error2(noargument);
  object *docstring = documentation(first(args), env);
  if (docstring) {
    flags_t temp = Flags;
    clrflag(PRINTREADABLY);
    printstring(docstring, pserial);
    Flags = temp;
  }
  return bsymbol(NOTHING);
}

object *fn_documentation (object *args, object *env) {
  return documentation(first(args), env);
}

object *fn_apropos (object *args, object *env) {
  (void) env;
  apropos(first(args), true);
  return bsymbol(NOTHING);
}

object *fn_aproposlist (object *args, object *env) {
  (void) env;
  return apropos(first(args), false);
}

// Error handling

object *sp_unwindprotect (object *args, object *env) {
  if (args == NULL) error2(toofewargs);
  object *current_GCStack = GCStack;
  jmp_buf dynamic_handler;
  jmp_buf *previous_handler = handler;
  handler = &dynamic_handler;
  object *protected_form = first(args);
  object *result;

  bool signaled = false;
  if (!setjmp(dynamic_handler)) {
    result = eval(protected_form, env);
  } else {
    GCStack = current_GCStack;
    signaled = true;
  }
  handler = previous_handler;

  object *protective_forms = cdr(args);
  while (protective_forms != NULL) {
    eval(car(protective_forms), env);
    if (tstflag(RETURNFLAG)) break;
    protective_forms = cdr(protective_forms);
  }

  if (!signaled) return result;
  GCStack = NULL;
  longjmp(*handler, 1);
}

object *sp_ignoreerrors (object *args, object *env) {
  object *current_GCStack = GCStack;
  jmp_buf dynamic_handler;
  jmp_buf *previous_handler = handler;
  handler = &dynamic_handler;
  object *result = nil;

  bool muffled = tstflag(MUFFLEERRORS);
  setflag(MUFFLEERRORS);
  bool signaled = false;
  if (!setjmp(dynamic_handler)) {
    while (args != NULL) {
      result = eval(car(args), env);
      if (tstflag(RETURNFLAG)) break;
      args = cdr(args);
    }
  } else {
    GCStack = current_GCStack;
    signaled = true;
  }
  handler = previous_handler;
  if (!muffled) clrflag(MUFFLEERRORS);

  if (signaled) return bsymbol(NOTHING);
  else return result;
}

object *sp_error (object *args, object *env) {
  object *message = eval(cons(bsymbol(FORMAT), cons(nil, args)), env);
  if (!tstflag(MUFFLEERRORS)) {
    flags_t temp = Flags;
    clrflag(PRINTREADABLY);
    pfstring("Error: ", pserial); printstring(message, pserial);
    Flags = temp;
    pln(pserial);
  }
  GCStack = NULL;
  longjmp(*handler, 1);
}

// SD Card utilities

object *fn_directory (object *args, object *env) {
  (void) args, (void) env;
  #if defined(sdcardsupport)
  SDBegin();
  File root = SD.open("/");
  if (!root) error2("problem reading from SD card");
  object *result = cons(NULL, NULL);
  object *ptr = result;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    object *filename = lispstring((char*)entry.name());
    cdr(ptr) = cons(filename, NULL);
    ptr = cdr(ptr);
    entry.close();
  }
  root.close();
  return cdr(result);
  #else
  error2("not supported");
  return nil;
  #endif
}

// Wi-Fi

object *sp_withclient (object *args, object *env) {
  object *params = checkarguments(args, 1, 3);
  object *var = first(params);
  char buffer[BUFFERSIZE];
  params = cdr(params);
  int n;
  if (params == NULL) {
    client = server.available();
    if (!client) return nil;
    n = 2;
  } else {
    object *address = eval(first(params), env);
    object *port = eval(second(params), env);
    int success;
    if (stringp(address)) success = client.connect(cstring(address, buffer, BUFFERSIZE), checkinteger(port));
    else if (integerp(address)) success = client.connect(address->integer, checkinteger(port));
    else error2("invalid address");
    if (!success) return nil;
    n = 1;
  }
  object *pair = cons(var, stream(WIFISTREAM, n));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  client.stop();
  return result;
}

object *fn_available (object *args, object *env) {
  (void) env;
  if (isstream(first(args))>>8 != WIFISTREAM) error2("invalid stream");
  return number(client.available());
}

object *fn_wifiserver (object *args, object *env) {
  (void) args, (void) env;
  server.begin();
  return nil;
}

object *fn_wifisoftap (object *args, object *env) {
  (void) env;
  char ssid[33], pass[65];
  if (args == NULL) return WiFi.softAPdisconnect(true) ? tee : nil;
  object *first = first(args); args = cdr(args);
  if (args == NULL) WiFi.softAP(cstring(first, ssid, 33));
  else {
    object *second = first(args);
    args = cdr(args);
    int channel = 1;
    bool hidden = false;
    if (args != NULL) {
      channel = checkinteger(first(args));
      args = cdr(args);
      if (args != NULL) hidden = (first(args) != nil);
    }
    WiFi.softAP(cstring(first, ssid, 33), cstring(second, pass, 65), channel, hidden);
  }
  return iptostring(WiFi.softAPIP());
}

object *fn_connected (object *args, object *env) {
  (void) env;
  if (isstream(first(args))>>8 != WIFISTREAM) error2("invalid stream");
  return client.connected() ? tee : nil;
}

object *fn_wifilocalip (object *args, object *env) {
  (void) args, (void) env;
  return iptostring(WiFi.localIP());
}

object *fn_wificonnect (object *args, object *env) {
  (void) env;
  char ssid[33], pass[65];
  if (args == NULL) { WiFi.disconnect(true); return nil; }
  if (cdr(args) == NULL) WiFi.begin(cstring(first(args), ssid, 33));
  else WiFi.begin(cstring(first(args), ssid, 33), cstring(second(args), pass, 65));
  int result = WiFi.waitForConnectResult();
  if (result == WL_CONNECTED) return iptostring(WiFi.localIP());
  else if (result == WL_NO_SSID_AVAIL) error2("network not found");
  else if (result == WL_CONNECT_FAILED) error2("connection failed");
  else error2("unable to connect");
  return nil;
}

// Graphics functions

object *sp_withgfx (object *args, object *env) {
#if defined(gfxsupport)
  object *params = checkarguments(args, 1, 1);
  object *var = first(params);
  object *pair = cons(var, stream(GFXSTREAM, 1));
  push(pair,env);
  object *forms = cdr(args);
  object *result = eval(tf_progn(forms,env), env);
  return result;
#else
  (void) args, (void) env;
  error2("not supported");
  return nil;
#endif
}

object *fn_drawpixel (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t colour = COLOR_WHITE;
  if (cddr(args) != NULL) colour = checkinteger(third(args));
  tft.drawPixel(checkinteger(first(args)), checkinteger(second(args)), colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_drawline (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.drawLine(params[0], params[1], params[2], params[3], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_drawrect (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.drawRect(params[0], params[1], params[2], params[3], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_fillrect (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[4], colour = COLOR_WHITE;
  for (int i=0; i<4; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.fillRect(params[0], params[1], params[2], params[3], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_drawcircle (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[3], colour = COLOR_WHITE;
  for (int i=0; i<3; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.drawCircle(params[0], params[1], params[2], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_fillcircle (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[3], colour = COLOR_WHITE;
  for (int i=0; i<3; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.fillCircle(params[0], params[1], params[2], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_drawroundrect (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[5], colour = COLOR_WHITE;
  for (int i=0; i<5; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.drawRoundRect(params[0], params[1], params[2], params[3], params[4], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_fillroundrect (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[5], colour = COLOR_WHITE;
  for (int i=0; i<5; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.fillRoundRect(params[0], params[1], params[2], params[3], params[4], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_drawtriangle (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[6], colour = COLOR_WHITE;
  for (int i=0; i<6; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.drawTriangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_filltriangle (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t params[6], colour = COLOR_WHITE;
  for (int i=0; i<6; i++) { params[i] = checkinteger(car(args)); args = cdr(args); }
  if (args != NULL) colour = checkinteger(car(args));
  tft.fillTriangle(params[0], params[1], params[2], params[3], params[4], params[5], colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_drawchar (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t colour = COLOR_WHITE, bg = COLOR_BLACK, size = 1;
  object *more = cdr(cddr(args));
  if (more != NULL) {
    colour = checkinteger(car(more));
    more = cdr(more);
    if (more != NULL) {
      bg = checkinteger(car(more));
      more = cdr(more);
      if (more != NULL) size = checkinteger(car(more));
    }
  }
  tft.drawChar(checkinteger(first(args)), checkinteger(second(args)), checkchar(third(args)),
    colour, bg, size);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_setcursor (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  tft.setCursor(checkinteger(first(args)), checkinteger(second(args)));
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_settextcolor (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  if (cdr(args) != NULL) tft.setTextColor(checkinteger(first(args)), checkinteger(second(args)));
  else tft.setTextColor(checkinteger(first(args)));
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_settextsize (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  tft.setTextSize(checkinteger(first(args)));
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_settextwrap (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  tft.setTextWrap(first(args) != NULL);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_fillscreen (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  uint16_t colour = COLOR_BLACK;
  if (args != NULL) colour = checkinteger(first(args));
  tft.fillScreen(colour);
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_setrotation (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  tft.setRotation(checkinteger(first(args)));
  #else
  (void) args;
  #endif
  return nil;
}

object *fn_invertdisplay (object *args, object *env) {
  (void) env;
  #if defined(gfxsupport)
  tft.invertDisplay(first(args) != NULL);
  #else
  (void) args;
  #endif
  return nil;
}

// T-Deck extras
char touchKeyMod(char temp){
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
  */
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
  
  if (received_touch) {
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
    //}
  }
  #else
  if (temp == '@') temp = '~';
  if (temp == '_') temp = '\\';
  #endif
  return temp;
}

char getKey () {
  char temp;
  do {
    Wire1.requestFrom(0x55, 1);
    while (!Wire1.available());
    temp = Wire1.read();
  } while ((temp == 0) || (temp ==255));
  temp = touchKeyMod(temp);
  return temp;
}

object *fn_getkey (object *args, object *env) {
  (void) env, (void) args;
  return character(getKey());
}

// Built-in symbol names
const char string0[] PROGMEM = "nil";
const char string1[] PROGMEM = "t";
const char string2[] PROGMEM = "nothing";
const char string3[] PROGMEM = "&optional";
const char string4[] PROGMEM = "*features*";
const char string5[] PROGMEM = ":initial-element";
const char string6[] PROGMEM = ":element-type";
const char string7[] PROGMEM = ":test";
const char string8[] PROGMEM = ":a";
const char string9[] PROGMEM = ":b";
const char string10[] PROGMEM = ":c";
const char string11[] PROGMEM = "bit";
const char string12[] PROGMEM = "&rest";
const char string13[] PROGMEM = "lambda";
const char string14[] PROGMEM = "let";
const char string15[] PROGMEM = "let*";
const char string16[] PROGMEM = "closure";
const char string17[] PROGMEM = "*pc*";
const char string18[] PROGMEM = "highlight";
const char string19[] PROGMEM = "quote";
const char string20[] PROGMEM = "defun";
const char string21[] PROGMEM = "defvar";
const char string22[] PROGMEM = "eq";
const char string23[] PROGMEM = "car";
const char string24[] PROGMEM = "first";
const char string25[] PROGMEM = "cdr";
const char string26[] PROGMEM = "rest";
const char string27[] PROGMEM = "nth";
const char string28[] PROGMEM = "aref";
const char string29[] PROGMEM = "char";
const char string30[] PROGMEM = "string";
const char string31[] PROGMEM = "pinmode";
const char string32[] PROGMEM = "digitalwrite";
const char string33[] PROGMEM = "analogread";
const char string34[] PROGMEM = "register";
const char string35[] PROGMEM = "format";
const char string36[] PROGMEM = "or";
const char string37[] PROGMEM = "setq";
const char string38[] PROGMEM = "loop";
const char string39[] PROGMEM = "push";
const char string40[] PROGMEM = "pop";
const char string41[] PROGMEM = "incf";
const char string42[] PROGMEM = "decf";
const char string43[] PROGMEM = "setf";
const char string44[] PROGMEM = "dolist";
const char string45[] PROGMEM = "dotimes";
const char string46[] PROGMEM = "do";
const char string47[] PROGMEM = "do*";
const char string48[] PROGMEM = "trace";
const char string49[] PROGMEM = "untrace";
const char string50[] PROGMEM = "for-millis";
const char string51[] PROGMEM = "time";
const char string52[] PROGMEM = "with-output-to-string";
const char string53[] PROGMEM = "with-serial";
const char string54[] PROGMEM = "with-i2c";
const char string55[] PROGMEM = "with-spi";
const char string56[] PROGMEM = "with-sd-card";
const char string57[] PROGMEM = "progn";
const char string58[] PROGMEM = "if";
const char string59[] PROGMEM = "cond";
const char string60[] PROGMEM = "when";
const char string61[] PROGMEM = "unless";
const char string62[] PROGMEM = "case";
const char string63[] PROGMEM = "and";
const char string64[] PROGMEM = "not";
const char string65[] PROGMEM = "null";
const char string66[] PROGMEM = "cons";
const char string67[] PROGMEM = "atom";
const char string68[] PROGMEM = "listp";
const char string69[] PROGMEM = "consp";
const char string70[] PROGMEM = "symbolp";
const char string71[] PROGMEM = "arrayp";
const char string72[] PROGMEM = "boundp";
const char string73[] PROGMEM = "keywordp";
const char string74[] PROGMEM = "set";
const char string75[] PROGMEM = "streamp";
const char string76[] PROGMEM = "equal";
const char string77[] PROGMEM = "caar";
const char string78[] PROGMEM = "cadr";
const char string79[] PROGMEM = "second";
const char string80[] PROGMEM = "cdar";
const char string81[] PROGMEM = "cddr";
const char string82[] PROGMEM = "caaar";
const char string83[] PROGMEM = "caadr";
const char string84[] PROGMEM = "cadar";
const char string85[] PROGMEM = "caddr";
const char string86[] PROGMEM = "third";
const char string87[] PROGMEM = "cdaar";
const char string88[] PROGMEM = "cdadr";
const char string89[] PROGMEM = "cddar";
const char string90[] PROGMEM = "cdddr";
const char string91[] PROGMEM = "length";
const char string92[] PROGMEM = "array-dimensions";
const char string93[] PROGMEM = "list";
const char string94[] PROGMEM = "copy-list";
const char string95[] PROGMEM = "make-array";
const char string96[] PROGMEM = "reverse";
const char string97[] PROGMEM = "assoc";
const char string98[] PROGMEM = "member";
const char string99[] PROGMEM = "apply";
const char string100[] PROGMEM = "funcall";
const char string101[] PROGMEM = "append";
const char string102[] PROGMEM = "mapc";
const char string103[] PROGMEM = "mapl";
const char string104[] PROGMEM = "mapcar";
const char string105[] PROGMEM = "mapcan";
const char string106[] PROGMEM = "maplist";
const char string107[] PROGMEM = "mapcon";
const char string108[] PROGMEM = "+";
const char string109[] PROGMEM = "-";
const char string110[] PROGMEM = "*";
const char string111[] PROGMEM = "/";
const char string112[] PROGMEM = "mod";
const char string113[] PROGMEM = "rem";
const char string114[] PROGMEM = "1+";
const char string115[] PROGMEM = "1-";
const char string116[] PROGMEM = "abs";
const char string117[] PROGMEM = "random";
const char string118[] PROGMEM = "max";
const char string119[] PROGMEM = "min";
const char string120[] PROGMEM = "/=";
const char string121[] PROGMEM = "=";
const char string122[] PROGMEM = "<";
const char string123[] PROGMEM = "<=";
const char string124[] PROGMEM = ">";
const char string125[] PROGMEM = ">=";
const char string126[] PROGMEM = "plusp";
const char string127[] PROGMEM = "minusp";
const char string128[] PROGMEM = "zerop";
const char string129[] PROGMEM = "oddp";
const char string130[] PROGMEM = "evenp";
const char string131[] PROGMEM = "integerp";
const char string132[] PROGMEM = "numberp";
const char string133[] PROGMEM = "float";
const char string134[] PROGMEM = "floatp";
const char string135[] PROGMEM = "sin";
const char string136[] PROGMEM = "cos";
const char string137[] PROGMEM = "tan";
const char string138[] PROGMEM = "asin";
const char string139[] PROGMEM = "acos";
const char string140[] PROGMEM = "atan";
const char string141[] PROGMEM = "sinh";
const char string142[] PROGMEM = "cosh";
const char string143[] PROGMEM = "tanh";
const char string144[] PROGMEM = "exp";
const char string145[] PROGMEM = "sqrt";
const char string146[] PROGMEM = "log";
const char string147[] PROGMEM = "expt";
const char string148[] PROGMEM = "ceiling";
const char string149[] PROGMEM = "floor";
const char string150[] PROGMEM = "truncate";
const char string151[] PROGMEM = "round";
const char string152[] PROGMEM = "char-code";
const char string153[] PROGMEM = "code-char";
const char string154[] PROGMEM = "characterp";
const char string155[] PROGMEM = "stringp";
const char string156[] PROGMEM = "string=";
const char string157[] PROGMEM = "string<";
const char string158[] PROGMEM = "string>";
const char string159[] PROGMEM = "string/=";
const char string160[] PROGMEM = "string<=";
const char string161[] PROGMEM = "string>=";
const char string162[] PROGMEM = "sort";
const char string163[] PROGMEM = "concatenate";
const char string164[] PROGMEM = "subseq";
const char string165[] PROGMEM = "search";
const char string166[] PROGMEM = "read-from-string";
const char string167[] PROGMEM = "princ-to-string";
const char string168[] PROGMEM = "prin1-to-string";
const char string169[] PROGMEM = "logand";
const char string170[] PROGMEM = "logior";
const char string171[] PROGMEM = "logxor";
const char string172[] PROGMEM = "lognot";
const char string173[] PROGMEM = "ash";
const char string174[] PROGMEM = "logbitp";
const char string175[] PROGMEM = "eval";
const char string176[] PROGMEM = "return";
const char string177[] PROGMEM = "globals";
const char string178[] PROGMEM = "locals";
const char string179[] PROGMEM = "makunbound";
const char string180[] PROGMEM = "break";
const char string181[] PROGMEM = "read";
const char string182[] PROGMEM = "prin1";
const char string183[] PROGMEM = "print";
const char string184[] PROGMEM = "princ";
const char string185[] PROGMEM = "terpri";
const char string186[] PROGMEM = "read-byte";
const char string187[] PROGMEM = "read-line";
const char string188[] PROGMEM = "write-byte";
const char string189[] PROGMEM = "write-string";
const char string190[] PROGMEM = "write-line";
const char string191[] PROGMEM = "restart-i2c";
const char string192[] PROGMEM = "gc";
const char string193[] PROGMEM = "room";
const char string194[] PROGMEM = "backtrace";
const char string195[] PROGMEM = "save-image";
const char string196[] PROGMEM = "load-image";
const char string197[] PROGMEM = "cls";
const char string198[] PROGMEM = "digitalread";
const char string199[] PROGMEM = "analogreadresolution";
const char string200[] PROGMEM = "analogwrite";
const char string201[] PROGMEM = "delay";
const char string202[] PROGMEM = "millis";
const char string203[] PROGMEM = "sleep";
const char string204[] PROGMEM = "note";
const char string205[] PROGMEM = "edit";
const char string206[] PROGMEM = "pprint";
const char string207[] PROGMEM = "pprintall";
const char string208[] PROGMEM = "require";
const char string209[] PROGMEM = "list-library";
const char string210[] PROGMEM = "?";
const char string211[] PROGMEM = "documentation";
const char string212[] PROGMEM = "apropos";
const char string213[] PROGMEM = "apropos-list";
const char string214[] PROGMEM = "unwind-protect";
const char string215[] PROGMEM = "ignore-errors";
const char string216[] PROGMEM = "error";
const char string217[] PROGMEM = "directory";
const char string218[] PROGMEM = "with-client";
const char string219[] PROGMEM = "available";
const char string220[] PROGMEM = "wifi-server";
const char string221[] PROGMEM = "wifi-softap";
const char string222[] PROGMEM = "connected";
const char string223[] PROGMEM = "wifi-localip";
const char string224[] PROGMEM = "wifi-connect";
const char string225[] PROGMEM = "with-gfx";
const char string226[] PROGMEM = "draw-pixel";
const char string227[] PROGMEM = "draw-line";
const char string228[] PROGMEM = "draw-rect";
const char string229[] PROGMEM = "fill-rect";
const char string230[] PROGMEM = "draw-circle";
const char string231[] PROGMEM = "fill-circle";
const char string232[] PROGMEM = "draw-round-rect";
const char string233[] PROGMEM = "fill-round-rect";
const char string234[] PROGMEM = "draw-triangle";
const char string235[] PROGMEM = "fill-triangle";
const char string236[] PROGMEM = "draw-char";
const char string237[] PROGMEM = "set-cursor";
const char string238[] PROGMEM = "set-text-color";
const char string239[] PROGMEM = "set-text-size";
const char string240[] PROGMEM = "set-text-wrap";
const char string241[] PROGMEM = "fill-screen";
const char string242[] PROGMEM = "set-rotation";
const char string243[] PROGMEM = "invert-display";
const char string244[] PROGMEM = "get-key";
const char string245[] PROGMEM = ":led-builtin";
const char string246[] PROGMEM = ":high";
const char string247[] PROGMEM = ":low";
const char string248[] PROGMEM = ":input";
const char string249[] PROGMEM = ":input-pullup";
const char string250[] PROGMEM = ":input-pulldown";
const char string251[] PROGMEM = ":output";

// Documentation strings
const char doc0[] PROGMEM = "nil\n"
"A symbol equivalent to the empty list (). Also represents false.";
const char doc1[] PROGMEM = "t\n"
"A symbol representing true.";
const char doc2[] PROGMEM = "nothing\n"
"A symbol with no value.\n"
"It is useful if you want to suppress printing the result of evaluating a function.";
const char doc3[] PROGMEM = "&optional\n"
"Can be followed by one or more optional parameters in a lambda or defun parameter list.";
const char doc4[] PROGMEM = "*features*\n"
"Returns a list of keywords representing features supported by this platform.";
const char doc12[] PROGMEM = "&rest\n"
"Can be followed by a parameter in a lambda or defun parameter list,\n"
"and is assigned a list of the corresponding arguments.";
const char doc13[] PROGMEM = "(lambda (parameter*) form*)\n"
"Creates an unnamed function with parameters. The body is evaluated with the parameters as local variables\n"
"whose initial values are defined by the values of the forms after the lambda form.";
const char doc14[] PROGMEM = "(let ((var value) ... ) forms*)\n"
"Declares local variables with values, and evaluates the forms with those local variables.";
const char doc15[] PROGMEM = "(let* ((var value) ... ) forms*)\n"
"Declares local variables with values, and evaluates the forms with those local variables.\n"
"Each declaration can refer to local variables that have been defined earlier in the let*.";
const char doc20[] PROGMEM = "(defun name (parameters) form*)\n"
"Defines a function.";
const char doc21[] PROGMEM = "(defvar variable form)\n"
"Defines a global variable.";
const char doc22[] PROGMEM = "(eq item item)\n"
"Tests whether the two arguments are the same symbol, same character, equal numbers,\n"
"or point to the same cons, and returns t or nil as appropriate.";
const char doc23[] PROGMEM = "(car list)\n"
"Returns the first item in a list.";
const char doc25[] PROGMEM = "(cdr list)\n"
"Returns a list with the first item removed.";
const char doc27[] PROGMEM = "(nth number list)\n"
"Returns the nth item in list, counting from zero.";
const char doc28[] PROGMEM = "(aref array index [index*])\n"
"Returns an element from the specified array.";
const char doc29[] PROGMEM = "(char string n)\n"
"Returns the nth character in a string, counting from zero.";
const char doc30[] PROGMEM = "(string item)\n"
"Converts its argument to a string.";
const char doc31[] PROGMEM = "(pinmode pin mode)\n"
"Sets the input/output mode of an Arduino pin number, and returns nil.\n"
"The mode parameter can be an integer, a keyword, or t or nil.";
const char doc32[] PROGMEM = "(digitalwrite pin state)\n"
"Sets the state of the specified Arduino pin number.";
const char doc33[] PROGMEM = "(analogread pin)\n"
"Reads the specified Arduino analogue pin number and returns the value.";
const char doc34[] PROGMEM = "(register address [value])\n"
"Reads or writes the value of a peripheral register.\n"
"If value is not specified the function returns the value of the register at address.\n"
"If value is specified the value is written to the register at address and the function returns value.";
const char doc35[] PROGMEM = "(format output controlstring [arguments]*)\n"
"Outputs its arguments formatted according to the format directives in controlstring.";
const char doc36[] PROGMEM = "(or item*)\n"
"Evaluates its arguments until one returns non-nil, and returns its value.";
const char doc37[] PROGMEM = "(setq symbol value [symbol value]*)\n"
"For each pair of arguments assigns the value of the second argument\n"
"to the variable specified in the first argument.";
const char doc38[] PROGMEM = "(loop forms*)\n"
"Executes its arguments repeatedly until one of the arguments calls (return),\n"
"which then causes an exit from the loop.";
const char doc39[] PROGMEM = "(push item place)\n"
"Modifies the value of place, which should be a list, to add item onto the front of the list,\n"
"and returns the new list.";
const char doc40[] PROGMEM = "(pop place)\n"
"Modifies the value of place, which should be a non-nil list, to remove its first item,\n"
"and returns that item.";
const char doc41[] PROGMEM = "(incf place [number])\n"
"Increments a place, which should have an numeric value, and returns the result.\n"
"The third argument is an optional increment which defaults to 1.";
const char doc42[] PROGMEM = "(decf place [number])\n"
"Decrements a place, which should have an numeric value, and returns the result.\n"
"The third argument is an optional decrement which defaults to 1.";
const char doc43[] PROGMEM = "(setf place value [place value]*)\n"
"For each pair of arguments modifies a place to the result of evaluating value.";
const char doc44[] PROGMEM = "(dolist (var list [result]) form*)\n"
"Sets the local variable var to each element of list in turn, and executes the forms.\n"
"It then returns result, or nil if result is omitted.";
const char doc45[] PROGMEM = "(dotimes (var number [result]) form*)\n"
"Executes the forms number times, with the local variable var set to each integer from 0 to number-1 in turn.\n"
"It then returns result, or nil if result is omitted.";
const char doc46[] PROGMEM = "(do ((var [init [step]])*) (end-test result*) form*)\n"
"Accepts an arbitrary number of iteration vars, which are initialised to init and stepped by step sequentially.\n"
"The forms are executed until end-test is true. It returns result.";
const char doc47[] PROGMEM = "(do* ((var [init [step]])*) (end-test result*) form*)\n"
"Accepts an arbitrary number of iteration vars, which are initialised to init and stepped by step in parallel.\n"
"The forms are executed until end-test is true. It returns result.";
const char doc48[] PROGMEM = "(trace [function]*)\n"
"Turns on tracing of up to TRACEMAX user-defined functions,\n"
"and returns a list of the functions currently being traced.";
const char doc49[] PROGMEM = "(untrace [function]*)\n"
"Turns off tracing of up to TRACEMAX user-defined functions, and returns a list of the functions untraced.\n"
"If no functions are specified it untraces all functions.";
const char doc50[] PROGMEM = "(for-millis ([number]) form*)\n"
"Executes the forms and then waits until a total of number milliseconds have elapsed.\n"
"Returns the total number of milliseconds taken.";
const char doc51[] PROGMEM = "(time form)\n"
"Prints the value returned by the form, and the time taken to evaluate the form\n"
"in milliseconds or seconds.";
const char doc52[] PROGMEM = "(with-output-to-string (str) form*)\n"
"Returns a string containing the output to the stream variable str.";
const char doc53[] PROGMEM = "(with-serial (str port [baud]) form*)\n"
"Evaluates the forms with str bound to a serial-stream using port.\n"
"The optional baud gives the baud rate divided by 100, default 96.";
const char doc54[] PROGMEM = "(with-i2c (str [port] address [read-p]) form*)\n"
"Evaluates the forms with str bound to an i2c-stream defined by address.\n"
"If read-p is nil or omitted the stream is written to, otherwise it specifies the number of bytes\n"
"to be read from the stream. If port is omitted it defaults to 0, otherwise it specifies the port, 0 or 1.";
const char doc55[] PROGMEM = "(with-spi (str pin [clock] [bitorder] [mode]) form*)\n"
"Evaluates the forms with str bound to an spi-stream.\n"
"The parameters specify the enable pin, clock in kHz (default 4000),\n"
"bitorder 0 for LSBFIRST and 1 for MSBFIRST (default 1), and SPI mode (default 0).";
const char doc56[] PROGMEM = "(with-sd-card (str filename [mode]) form*)\n"
"Evaluates the forms with str bound to an sd-stream reading from or writing to the file filename.\n"
"If mode is omitted the file is read, otherwise 0 means read, 1 write-append, or 2 write-overwrite.";
const char doc57[] PROGMEM = "(progn form*)\n"
"Evaluates several forms grouped together into a block, and returns the result of evaluating the last form.";
const char doc58[] PROGMEM = "(if test then [else])\n"
"Evaluates test. If it's non-nil the form then is evaluated and returned;\n"
"otherwise the form else is evaluated and returned.";
const char doc59[] PROGMEM = "(cond ((test form*) (test form*) ... ))\n"
"Each argument is a list consisting of a test optionally followed by one or more forms.\n"
"If the test evaluates to non-nil the forms are evaluated, and the last value is returned as the result of the cond.\n"
"If the test evaluates to nil, none of the forms are evaluated, and the next argument is processed in the same way.";
const char doc60[] PROGMEM = "(when test form*)\n"
"Evaluates the test. If it's non-nil the forms are evaluated and the last value is returned.";
const char doc61[] PROGMEM = "(unless test form*)\n"
"Evaluates the test. If it's nil the forms are evaluated and the last value is returned.";
const char doc62[] PROGMEM = "(case keyform ((key form*) (key form*) ... ))\n"
"Evaluates a keyform to produce a test key, and then tests this against a series of arguments,\n"
"each of which is a list containing a key optionally followed by one or more forms.";
const char doc63[] PROGMEM = "(and item*)\n"
"Evaluates its arguments until one returns nil, and returns the last value.";
const char doc64[] PROGMEM = "(not item)\n"
"Returns t if its argument is nil, or nil otherwise. Equivalent to null.";
const char doc66[] PROGMEM = "(cons item item)\n"
"If the second argument is a list, cons returns a new list with item added to the front of the list.\n"
"If the second argument isn't a list cons returns a dotted pair.";
const char doc67[] PROGMEM = "(atom item)\n"
"Returns t if its argument is a single number, symbol, or nil.";
const char doc68[] PROGMEM = "(listp item)\n"
"Returns t if its argument is a list.";
const char doc69[] PROGMEM = "(consp item)\n"
"Returns t if its argument is a non-null list.";
const char doc70[] PROGMEM = "(symbolp item)\n"
"Returns t if its argument is a symbol.";
const char doc71[] PROGMEM = "(arrayp item)\n"
"Returns t if its argument is an array.";
const char doc72[] PROGMEM = "(boundp item)\n"
"Returns t if its argument is a symbol with a value.";
const char doc73[] PROGMEM = "(keywordp item)\n"
"Returns t if its argument is a built-in or user-defined keyword.";
const char doc74[] PROGMEM = "(set symbol value [symbol value]*)\n"
"For each pair of arguments, assigns the value of the second argument to the value of the first argument.";
const char doc75[] PROGMEM = "(streamp item)\n"
"Returns t if its argument is a stream.";
const char doc76[] PROGMEM = "(equal item item)\n"
"Tests whether the two arguments are the same symbol, same character, equal numbers,\n"
"or point to the same cons, and returns t or nil as appropriate.";
const char doc77[] PROGMEM = "(caar list)";
const char doc78[] PROGMEM = "(cadr list)";
const char doc80[] PROGMEM = "(cdar list)\n"
"Equivalent to (cdr (car list)).";
const char doc81[] PROGMEM = "(cddr list)\n"
"Equivalent to (cdr (cdr list)).";
const char doc82[] PROGMEM = "(caaar list)\n"
"Equivalent to (car (car (car list))).";
const char doc83[] PROGMEM = "(caadr list)\n"
"Equivalent to (car (car (cdar list))).";
const char doc84[] PROGMEM = "(cadar list)\n"
"Equivalent to (car (cdr (car list))).";
const char doc85[] PROGMEM = "(caddr list)\n"
"Equivalent to (car (cdr (cdr list))).";
const char doc87[] PROGMEM = "(cdaar list)\n"
"Equivalent to (cdar (car (car list))).";
const char doc88[] PROGMEM = "(cdadr list)\n"
"Equivalent to (cdr (car (cdr list))).";
const char doc89[] PROGMEM = "(cddar list)\n"
"Equivalent to (cdr (cdr (car list))).";
const char doc90[] PROGMEM = "(cdddr list)\n"
"Equivalent to (cdr (cdr (cdr list))).";
const char doc91[] PROGMEM = "(length item)\n"
"Returns the number of items in a list, the length of a string, or the length of a one-dimensional array.";
const char doc92[] PROGMEM = "(array-dimensions item)\n"
"Returns a list of the dimensions of an array.";
const char doc93[] PROGMEM = "(list item*)\n"
"Returns a list of the values of its arguments.";
const char doc94[] PROGMEM = "(copy-list list)\n"
"Returns a copy of a list.";
const char doc95[] PROGMEM = "(make-array size [:initial-element element] [:element-type 'bit])\n"
"If size is an integer it creates a one-dimensional array with elements from 0 to size-1.\n"
"If size is a list of n integers it creates an n-dimensional array with those dimensions.\n"
"If :element-type 'bit is specified the array is a bit array.";
const char doc96[] PROGMEM = "(reverse list)\n"
"Returns a list with the elements of list in reverse order.";
const char doc97[] PROGMEM = "(assoc key list [:test function])\n"
"Looks up a key in an association list of (key . value) pairs, using eq or the specified test function,\n"
"and returns the matching pair, or nil if no pair is found.";
const char doc98[] PROGMEM = "(member item list [:test function])\n"
"Searches for an item in a list, using eq or the specified test function, and returns the list starting\n"
"from the first occurrence of the item, or nil if it is not found.";
const char doc99[] PROGMEM = "(apply function list)\n"
"Returns the result of evaluating function, with the list of arguments specified by the second parameter.";
const char doc100[] PROGMEM = "(funcall function argument*)\n"
"Evaluates function with the specified arguments.";
const char doc101[] PROGMEM = "(append list*)\n"
"Joins its arguments, which should be lists, into a single list.";
const char doc102[] PROGMEM = "(mapc function list1 [list]*)\n"
"Applies the function to each element in one or more lists, ignoring the results.\n"
"It returns the first list argument.";
const char doc103[] PROGMEM = "(mapl function list1 [list]*)\n"
"Applies the function to one or more lists and then successive cdrs of those lists,\n"
"ignoring the results. It returns the first list argument.";
const char doc104[] PROGMEM = "(mapcar function list1 [list]*)\n"
"Applies the function to each element in one or more lists, and returns the resulting list.";
const char doc105[] PROGMEM = "(mapcan function list1 [list]*)\n"
"Applies the function to each element in one or more lists. The results should be lists,\n"
"and these are destructively concatenated together to give the value returned.";
const char doc106[] PROGMEM = "(maplist function list1 [list]*)\n"
"Applies the function to one or more lists and then successive cdrs of those lists,\n"
"and returns the resulting list.";
const char doc107[] PROGMEM = "(mapcon function list1 [list]*)\n"
"Applies the function to one or more lists and then successive cdrs of those lists,\n"
"and these are destructively concatenated together to give the value returned.";
const char doc108[] PROGMEM = "(+ number*)\n"
"Adds its arguments together.\n"
"If each argument is an integer, and the running total doesn't overflow, the result is an integer,\n"
"otherwise a floating-point number.";
const char doc109[] PROGMEM = "(- number*)\n"
"If there is one argument, negates the argument.\n"
"If there are two or more arguments, subtracts the second and subsequent arguments from the first argument.\n"
"If each argument is an integer, and the running total doesn't overflow, returns the result as an integer,\n"
"otherwise a floating-point number.";
const char doc110[] PROGMEM = "(* number*)\n"
"Multiplies its arguments together.\n"
"If each argument is an integer, and the running total doesn't overflow, the result is an integer,\n"
"otherwise it's a floating-point number.";
const char doc111[] PROGMEM = "(/ number*)\n"
"Divides the first argument by the second and subsequent arguments.\n"
"If each argument is an integer, and each division produces an exact result, the result is an integer;\n"
"otherwise it's a floating-point number.";
const char doc112[] PROGMEM = "(mod number number)\n"
"Returns its first argument modulo the second argument.\n"
"If both arguments are integers the result is an integer; otherwise it's a floating-point number.";
const char doc113[] PROGMEM = "(rem number number)\n"
"Returns the remainder from dividing the first argument by the second argument.\n"
"If both arguments are integers the result is an integer; otherwise it's a floating-point number.";
const char doc114[] PROGMEM = "(1+ number)\n"
"Adds one to its argument and returns it.\n"
"If the argument is an integer the result is an integer if possible;\n"
"otherwise it's a floating-point number.";
const char doc115[] PROGMEM = "(1- number)\n"
"Subtracts one from its argument and returns it.\n"
"If the argument is an integer the result is an integer if possible;\n"
"otherwise it's a floating-point number.";
const char doc116[] PROGMEM = "(abs number)\n"
"Returns the absolute, positive value of its argument.\n"
"If the argument is an integer the result will be returned as an integer if possible,\n"
"otherwise a floating-point number.";
const char doc117[] PROGMEM = "(random number)\n"
"If number is an integer returns a random number between 0 and one less than its argument.\n"
"Otherwise returns a floating-point number between zero and number.";
const char doc118[] PROGMEM = "(max number*)\n"
"Returns the maximum of one or more arguments.";
const char doc119[] PROGMEM = "(min number*)\n"
"Returns the minimum of one or more arguments.";
const char doc120[] PROGMEM = "(/= number*)\n"
"Returns t if none of the arguments are equal, or nil if two or more arguments are equal.";
const char doc121[] PROGMEM = "(= number*)\n"
"Returns t if all the arguments, which must be numbers, are numerically equal, and nil otherwise.";
const char doc122[] PROGMEM = "(< number*)\n"
"Returns t if each argument is less than the next argument, and nil otherwise.";
const char doc123[] PROGMEM = "(<= number*)\n"
"Returns t if each argument is less than or equal to the next argument, and nil otherwise.";
const char doc124[] PROGMEM = "(> number*)\n"
"Returns t if each argument is greater than the next argument, and nil otherwise.";
const char doc125[] PROGMEM = "(>= number*)\n"
"Returns t if each argument is greater than or equal to the next argument, and nil otherwise.";
const char doc126[] PROGMEM = "(plusp number)\n"
"Returns t if the argument is greater than zero, or nil otherwise.";
const char doc127[] PROGMEM = "(minusp number)\n"
"Returns t if the argument is less than zero, or nil otherwise.";
const char doc128[] PROGMEM = "(zerop number)\n"
"Returns t if the argument is zero.";
const char doc129[] PROGMEM = "(oddp number)\n"
"Returns t if the integer argument is odd.";
const char doc130[] PROGMEM = "(evenp number)\n"
"Returns t if the integer argument is even.";
const char doc131[] PROGMEM = "(integerp number)\n"
"Returns t if the argument is an integer.";
const char doc132[] PROGMEM = "(numberp number)\n"
"Returns t if the argument is a number.";
const char doc133[] PROGMEM = "(float number)\n"
"Returns its argument converted to a floating-point number.";
const char doc134[] PROGMEM = "(floatp number)\n"
"Returns t if the argument is a floating-point number.";
const char doc135[] PROGMEM = "(sin number)\n"
"Returns sin(number).";
const char doc136[] PROGMEM = "(cos number)\n"
"Returns cos(number).";
const char doc137[] PROGMEM = "(tan number)\n"
"Returns tan(number).";
const char doc138[] PROGMEM = "(asin number)\n"
"Returns asin(number).";
const char doc139[] PROGMEM = "(acos number)\n"
"Returns acos(number).";
const char doc140[] PROGMEM = "(atan number1 [number2])\n"
"Returns the arc tangent of number1/number2, in radians. If number2 is omitted it defaults to 1.";
const char doc141[] PROGMEM = "(sinh number)\n"
"Returns sinh(number).";
const char doc142[] PROGMEM = "(cosh number)\n"
"Returns cosh(number).";
const char doc143[] PROGMEM = "(tanh number)\n"
"Returns tanh(number).";
const char doc144[] PROGMEM = "(exp number)\n"
"Returns exp(number).";
const char doc145[] PROGMEM = "(sqrt number)\n"
"Returns sqrt(number).";
const char doc146[] PROGMEM = "(log number [base])\n"
"Returns the logarithm of number to the specified base. If base is omitted it defaults to e.";
const char doc147[] PROGMEM = "(expt number power)\n"
"Returns number raised to the specified power.\n"
"Returns the result as an integer if the arguments are integers and the result will be within range,\n"
"otherwise a floating-point number.";
const char doc148[] PROGMEM = "(ceiling number [divisor])\n"
"Returns ceil(number/divisor). If omitted, divisor is 1.";
const char doc149[] PROGMEM = "(floor number [divisor])\n"
"Returns floor(number/divisor). If omitted, divisor is 1.";
const char doc150[] PROGMEM = "(truncate number [divisor])\n"
"Returns the integer part of number/divisor. If divisor is omitted it defaults to 1.";
const char doc151[] PROGMEM = "(round number [divisor])\n"
"Returns the integer closest to number/divisor. If divisor is omitted it defaults to 1.";
const char doc152[] PROGMEM = "(char-code character)\n"
"Returns the ASCII code for a character, as an integer.";
const char doc153[] PROGMEM = "(code-char integer)\n"
"Returns the character for the specified ASCII code.";
const char doc154[] PROGMEM = "(characterp item)\n"
"Returns t if the argument is a character and nil otherwise.";
const char doc155[] PROGMEM = "(stringp item)\n"
"Returns t if the argument is a string and nil otherwise.";
const char doc156[] PROGMEM = "(string= string string)\n"
"Returns t if the two strings are the same, or nil otherwise.";
const char doc157[] PROGMEM = "(string< string string)\n"
"Returns the index to the first mismatch if the first string is alphabetically less than the second string,\n"
"or nil otherwise.";
const char doc158[] PROGMEM = "(string> string string)\n"
"Returns the index to the first mismatch if the first string is alphabetically greater than the second string,\n"
"or nil otherwise.";
const char doc159[] PROGMEM = "(string/= string string)\n"
"Returns the index to the first mismatch if the two strings are not the same, or nil otherwise.";
const char doc160[] PROGMEM = "(string<= string string)\n"
"Returns the index to the first mismatch if the first string is alphabetically less than or equal to\n"
"the second string, or nil otherwise.";
const char doc161[] PROGMEM = "(string>= string string)\n"
"Returns the index to the first mismatch if the first string is alphabetically greater than or equal to\n"
"the second string, or nil otherwise.";
const char doc162[] PROGMEM = "(sort list test)\n"
"Destructively sorts list according to the test function, using an insertion sort, and returns the sorted list.";
const char doc163[] PROGMEM = "(concatenate 'string string*)\n"
"Joins together the strings given in the second and subsequent arguments, and returns a single string.";
const char doc164[] PROGMEM = "(subseq seq start [end])\n"
"Returns a subsequence of a list or string from item start to item end-1.";
const char doc165[] PROGMEM = "(search pattern target [:test function])\n"
"Returns the index of the first occurrence of pattern in target, or nil if it's not found.\n"
"The target can be a list or string. If it's a list a test function can be specified; default eq.";
const char doc166[] PROGMEM = "(read-from-string string)\n"
"Reads an atom or list from the specified string and returns it.";
const char doc167[] PROGMEM = "(princ-to-string item)\n"
"Prints its argument to a string, and returns the string.\n"
"Characters and strings are printed without quotation marks or escape characters.";
const char doc168[] PROGMEM = "(prin1-to-string item [stream])\n"
"Prints its argument to a string, and returns the string.\n"
"Characters and strings are printed with quotation marks and escape characters,\n"
"in a format that will be suitable for read-from-string.";
const char doc169[] PROGMEM = "(logand [value*])\n"
"Returns the bitwise & of the values.";
const char doc170[] PROGMEM = "(logior [value*])\n"
"Returns the bitwise | of the values.";
const char doc171[] PROGMEM = "(logxor [value*])\n"
"Returns the bitwise ^ of the values.";
const char doc172[] PROGMEM = "(lognot value)\n"
"Returns the bitwise logical NOT of the value.";
const char doc173[] PROGMEM = "(ash value shift)\n"
"Returns the result of bitwise shifting value by shift bits. If shift is positive, value is shifted to the left.";
const char doc174[] PROGMEM = "(logbitp bit value)\n"
"Returns t if bit number bit in value is a '1', and nil if it is a '0'.";
const char doc175[] PROGMEM = "(eval form*)\n"
"Evaluates its argument an extra time.";
const char doc176[] PROGMEM = "(return [value])\n"
"Exits from a (dotimes ...), (dolist ...), or (loop ...) loop construct and returns value.";
const char doc177[] PROGMEM = "(globals)\n"
"Returns a list of global variables.";
const char doc178[] PROGMEM = "(locals)\n"
"Returns an association list of local variables and their values.";
const char doc179[] PROGMEM = "(makunbound symbol)\n"
"Removes the value of the symbol from GlobalEnv and returns the symbol.";
const char doc180[] PROGMEM = "(break)\n"
"Inserts a breakpoint in the program. When evaluated prints Break! and reenters the REPL.";
const char doc181[] PROGMEM = "(read [stream])\n"
"Reads an atom or list from the serial input and returns it.\n"
"If stream is specified the item is read from the specified stream.";
const char doc182[] PROGMEM = "(prin1 item [stream])\n"
"Prints its argument, and returns its value.\n"
"Strings are printed with quotation marks and escape characters.";
const char doc183[] PROGMEM = "(print item [stream])\n"
"Prints its argument with quotation marks and escape characters, on a new line, and followed by a space.\n"
"If stream is specified the argument is printed to the specified stream.";
const char doc184[] PROGMEM = "(princ item [stream])\n"
"Prints its argument, and returns its value.\n"
"Characters and strings are printed without quotation marks or escape characters.";
const char doc185[] PROGMEM = "(terpri [stream])\n"
"Prints a new line, and returns nil.\n"
"If stream is specified the new line is written to the specified stream.";
const char doc186[] PROGMEM = "(read-byte stream)\n"
"Reads a byte from a stream and returns it.";
const char doc187[] PROGMEM = "(read-line [stream])\n"
"Reads characters from the serial input up to a newline character, and returns them as a string, excluding the newline.\n"
"If stream is specified the line is read from the specified stream.";
const char doc188[] PROGMEM = "(write-byte number [stream])\n"
"Writes a byte to a stream.";
const char doc189[] PROGMEM = "(write-string string [stream])\n"
"Writes a string. If stream is specified the string is written to the stream.";
const char doc190[] PROGMEM = "(write-line string [stream])\n"
"Writes a string terminated by a newline character. If stream is specified the string is written to the stream.";
const char doc191[] PROGMEM = "(restart-i2c stream [read-p])\n"
"Restarts an i2c-stream.\n"
"If read-p is nil or omitted the stream is written to.\n"
"If read-p is an integer it specifies the number of bytes to be read from the stream.";
const char doc192[] PROGMEM = "(gc [print time])\n"
"Forces a garbage collection and prints the number of objects collected, and the time taken.";
const char doc193[] PROGMEM = "(room)\n"
"Returns the number of free Lisp cells remaining.";
const char doc194[] PROGMEM = "(backtrace [on])\n"
"Sets the state of backtrace according to the boolean flag 'on',\n"
"or with no argument displays the current state of backtrace.";
const char doc195[] PROGMEM = "(save-image [symbol])\n"
"Saves the current uLisp image to non-volatile memory or SD card so it can be loaded using load-image.";
const char doc196[] PROGMEM = "(load-image [filename])\n"
"Loads a saved uLisp image from non-volatile memory or SD card.";
const char doc197[] PROGMEM = "(cls)\n"
"Prints a clear-screen character.";
const char doc198[] PROGMEM = "(digitalread pin)\n"
"Reads the state of the specified Arduino pin number and returns t (high) or nil (low).";
const char doc199[] PROGMEM = "(analogreadresolution bits)\n"
"Specifies the resolution for the analogue inputs on platforms that support it.\n"
"The default resolution on all platforms is 10 bits.";
const char doc200[] PROGMEM = "(analogwrite pin value)\n"
"Writes the value to the specified Arduino pin number.";
const char doc201[] PROGMEM = "(delay number)\n"
"Delays for a specified number of milliseconds.";
const char doc202[] PROGMEM = "(millis)\n"
"Returns the time in milliseconds that uLisp has been running.";
const char doc203[] PROGMEM = "(sleep secs)\n"
"Puts the processor into a low-power sleep mode for secs.\n"
"Only supported on some platforms. On other platforms it does delay(1000*secs).";
const char doc204[] PROGMEM = "(note [pin] [note] [octave])\n"
"Generates a square wave on pin.\n"
"note represents the note in the well-tempered scale.\n"
"The argument octave can specify an octave; default 0.";
const char doc205[] PROGMEM = "(edit 'function)\n"
"Calls the Lisp tree editor to allow you to edit a function definition.";
const char doc206[] PROGMEM = "(pprint item [str])\n"
"Prints its argument, using the pretty printer, to display it formatted in a structured way.\n"
"If str is specified it prints to the specified stream. It returns no value.";
const char doc207[] PROGMEM = "(pprintall [str])\n"
"Pretty-prints the definition of every function and variable defined in the uLisp workspace.\n"
"If str is specified it prints to the specified stream. It returns no value.";
const char doc208[] PROGMEM = "(require 'symbol)\n"
"Loads the definition of a function defined with defun, or a variable defined with defvar, from the Lisp Library.\n"
"It returns t if it was loaded, or nil if the symbol is already defined or isn't defined in the Lisp Library.";
const char doc209[] PROGMEM = "(list-library)\n"
"Prints a list of the functions defined in the List Library.";
const char doc210[] PROGMEM = "(? item)\n"
"Prints the documentation string of a built-in or user-defined function.";
const char doc211[] PROGMEM = "(documentation 'symbol [type])\n"
"Returns the documentation string of a built-in or user-defined function. The type argument is ignored.";
const char doc212[] PROGMEM = "(apropos item)\n"
"Prints the user-defined and built-in functions whose names contain the specified string or symbol.";
const char doc213[] PROGMEM = "(apropos-list item)\n"
"Returns a list of user-defined and built-in functions whose names contain the specified string or symbol.";
const char doc214[] PROGMEM = "(unwind-protect form1 [forms]*)\n"
"Evaluates form1 and forms in order and returns the value of form1,\n"
"but guarantees to evaluate forms even if an error occurs in form1.";
const char doc215[] PROGMEM = "(ignore-errors [forms]*)\n"
"Evaluates forms ignoring errors.";
const char doc216[] PROGMEM = "(error controlstring [arguments]*)\n"
"Signals an error. The message is printed by format using the controlstring and arguments.";
const char doc217[] PROGMEM = "(directory)\n"
"Returns a list of the filenames of the files on the SD card.";
const char doc218[] PROGMEM = "(with-client (str [address port]) form*)\n"
"Evaluates the forms with str bound to a wifi-stream.";
const char doc219[] PROGMEM = "(available stream)\n"
"Returns the number of bytes available for reading from the wifi-stream, or zero if no bytes are available.";
const char doc220[] PROGMEM = "(wifi-server)\n"
"Starts a Wi-Fi server running. It returns nil.";
const char doc221[] PROGMEM = "(wifi-softap ssid [password channel hidden])\n"
"Set up a soft access point to establish a Wi-Fi network.\n"
"Returns the IP address as a string or nil if unsuccessful.";
const char doc222[] PROGMEM = "(connected stream)\n"
"Returns t or nil to indicate if the client on stream is connected.";
const char doc223[] PROGMEM = "(wifi-localip)\n"
"Returns the IP address of the local network as a string.";
const char doc224[] PROGMEM = "(wifi-connect [ssid pass])\n"
"Connects to the Wi-Fi network ssid using password pass. It returns the IP address as a string.";
const char doc225[] PROGMEM = "(with-gfx (str) form*)\n"
"Evaluates the forms with str bound to an gfx-stream so you can print text\n"
"to the graphics display using the standard uLisp print commands.";
const char doc226[] PROGMEM = "(draw-pixel x y [colour])\n"
"Draws a pixel at coordinates (x,y) in colour, or white if omitted.";
const char doc227[] PROGMEM = "(draw-line x0 y0 x1 y1 [colour])\n"
"Draws a line from (x0,y0) to (x1,y1) in colour, or white if omitted.";
const char doc228[] PROGMEM = "(draw-rect x y w h [colour])\n"
"Draws an outline rectangle with its top left corner at (x,y), with width w,\n"
"and with height h. The outline is drawn in colour, or white if omitted.";
const char doc229[] PROGMEM = "(fill-rect x y w h [colour])\n"
"Draws a filled rectangle with its top left corner at (x,y), with width w,\n"
"and with height h. The outline is drawn in colour, or white if omitted.";
const char doc230[] PROGMEM = "(draw-circle x y r [colour])\n"
"Draws an outline circle with its centre at (x, y) and with radius r.\n"
"The circle is drawn in colour, or white if omitted.";
const char doc231[] PROGMEM = "(fill-circle x y r [colour])\n"
"Draws a filled circle with its centre at (x, y) and with radius r.\n"
"The circle is drawn in colour, or white if omitted.";
const char doc232[] PROGMEM = "(draw-round-rect x y w h radius [colour])\n"
"Draws an outline rounded rectangle with its top left corner at (x,y), with width w,\n"
"height h, and corner radius radius. The outline is drawn in colour, or white if omitted.";
const char doc233[] PROGMEM = "(fill-round-rect x y w h radius [colour])\n"
"Draws a filled rounded rectangle with its top left corner at (x,y), with width w,\n"
"height h, and corner radius radius. The outline is drawn in colour, or white if omitted.";
const char doc234[] PROGMEM = "(draw-triangle x0 y0 x1 y1 x2 y2 [colour])\n"
"Draws an outline triangle between (x1,y1), (x2,y2), and (x3,y3).\n"
"The outline is drawn in colour, or white if omitted.";
const char doc235[] PROGMEM = "(fill-triangle x0 y0 x1 y1 x2 y2 [colour])\n"
"Draws a filled triangle between (x1,y1), (x2,y2), and (x3,y3).\n"
"The outline is drawn in colour, or white if omitted.";
const char doc236[] PROGMEM = "(draw-char x y char [colour background size])\n"
"Draws the character char with its top left corner at (x,y).\n"
"The character is drawn in a 5 x 7 pixel font in colour against background,\n"
"which default to white and black respectively.\n"
"The character can optionally be scaled by size.";
const char doc237[] PROGMEM = "(set-cursor x y)\n"
"Sets the start point for text plotting to (x, y).";
const char doc238[] PROGMEM = "(set-text-color colour [background])\n"
"Sets the text colour for text plotted using (with-gfx ...).";
const char doc239[] PROGMEM = "(set-text-size scale)\n"
"Scales text by the specified size, default 1.";
const char doc240[] PROGMEM = "(set-text-wrap boolean)\n"
"Specified whether text wraps at the right-hand edge of the display; the default is t.";
const char doc241[] PROGMEM = "(fill-screen [colour])\n"
"Fills or clears the screen with colour, default black.";
const char doc242[] PROGMEM = "(set-rotation option)\n"
"Sets the display orientation for subsequent graphics commands; values are 0, 1, 2, or 3.";
const char doc243[] PROGMEM = "(invert-display boolean)\n"
"Mirror-images the display.";
const char doc244[] PROGMEM = "(get-key)\n"
"Waits for a key press and returns it as a character.";

// Built-in symbol lookup table
const tbl_entry_t lookup_table[] PROGMEM = {
  { string0, NULL, 0000, doc0 },
  { string1, NULL, 0000, doc1 },
  { string2, NULL, 0000, doc2 },
  { string3, NULL, 0000, doc3 },
  { string4, NULL, 0000, doc4 },
  { string5, NULL, 0000, NULL },
  { string6, NULL, 0000, NULL },
  { string7, NULL, 0000, NULL },
  { string8, NULL, 0000, NULL },
  { string9, NULL, 0000, NULL },
  { string10, NULL, 0000, NULL },
  { string11, NULL, 0000, NULL },
  { string12, NULL, 0000, doc12 },
  { string13, NULL, 0017, doc13 },
  { string14, NULL, 0017, doc14 },
  { string15, NULL, 0017, doc15 },
  { string16, NULL, 0017, NULL },
  { string17, NULL, 0007, NULL },
  { string18, NULL, 0000, NULL },
  { string19, sp_quote, 0311, NULL },
  { string20, sp_defun, 0327, doc20 },
  { string21, sp_defvar, 0313, doc21 },
  { string22, fn_eq, 0222, doc22 },
  { string23, fn_car, 0211, doc23 },
  { string24, fn_car, 0211, NULL },
  { string25, fn_cdr, 0211, doc25 },
  { string26, fn_cdr, 0211, NULL },
  { string27, fn_nth, 0222, doc27 },
  { string28, fn_aref, 0227, doc28 },
  { string29, fn_char, 0222, doc29 },
  { string30, fn_stringfn, 0211, doc30 },
  { string31, fn_pinmode, 0222, doc31 },
  { string32, fn_digitalwrite, 0222, doc32 },
  { string33, fn_analogread, 0211, doc33 },
  { string34, fn_register, 0212, doc34 },
  { string35, fn_format, 0227, doc35 },
  { string36, sp_or, 0307, doc36 },
  { string37, sp_setq, 0327, doc37 },
  { string38, sp_loop, 0307, doc38 },
  { string39, sp_push, 0322, doc39 },
  { string40, sp_pop, 0311, doc40 },
  { string41, sp_incf, 0312, doc41 },
  { string42, sp_decf, 0312, doc42 },
  { string43, sp_setf, 0327, doc43 },
  { string44, sp_dolist, 0317, doc44 },
  { string45, sp_dotimes, 0317, doc45 },
  { string46, sp_do, 0327, doc46 },
  { string47, sp_dostar, 0317, doc47 },
  { string48, sp_trace, 0301, doc48 },
  { string49, sp_untrace, 0301, doc49 },
  { string50, sp_formillis, 0317, doc50 },
  { string51, sp_time, 0311, doc51 },
  { string52, sp_withoutputtostring, 0317, doc52 },
  { string53, sp_withserial, 0317, doc53 },
  { string54, sp_withi2c, 0317, doc54 },
  { string55, sp_withspi, 0317, doc55 },
  { string56, sp_withsdcard, 0327, doc56 },
  { string57, tf_progn, 0107, doc57 },
  { string58, tf_if, 0123, doc58 },
  { string59, tf_cond, 0107, doc59 },
  { string60, tf_when, 0117, doc60 },
  { string61, tf_unless, 0117, doc61 },
  { string62, tf_case, 0117, doc62 },
  { string63, tf_and, 0107, doc63 },
  { string64, fn_not, 0211, doc64 },
  { string65, fn_not, 0211, NULL },
  { string66, fn_cons, 0222, doc66 },
  { string67, fn_atom, 0211, doc67 },
  { string68, fn_listp, 0211, doc68 },
  { string69, fn_consp, 0211, doc69 },
  { string70, fn_symbolp, 0211, doc70 },
  { string71, fn_arrayp, 0211, doc71 },
  { string72, fn_boundp, 0211, doc72 },
  { string73, fn_keywordp, 0211, doc73 },
  { string74, fn_setfn, 0227, doc74 },
  { string75, fn_streamp, 0211, doc75 },
  { string76, fn_equal, 0222, doc76 },
  { string77, fn_caar, 0211, doc77 },
  { string78, fn_cadr, 0211, doc78 },
  { string79, fn_cadr, 0211, NULL },
  { string80, fn_cdar, 0211, doc80 },
  { string81, fn_cddr, 0211, doc81 },
  { string82, fn_caaar, 0211, doc82 },
  { string83, fn_caadr, 0211, doc83 },
  { string84, fn_cadar, 0211, doc84 },
  { string85, fn_caddr, 0211, doc85 },
  { string86, fn_caddr, 0211, NULL },
  { string87, fn_cdaar, 0211, doc87 },
  { string88, fn_cdadr, 0211, doc88 },
  { string89, fn_cddar, 0211, doc89 },
  { string90, fn_cdddr, 0211, doc90 },
  { string91, fn_length, 0211, doc91 },
  { string92, fn_arraydimensions, 0211, doc92 },
  { string93, fn_list, 0207, doc93 },
  { string94, fn_copylist, 0211, doc94 },
  { string95, fn_makearray, 0215, doc95 },
  { string96, fn_reverse, 0211, doc96 },
  { string97, fn_assoc, 0224, doc97 },
  { string98, fn_member, 0224, doc98 },
  { string99, fn_apply, 0227, doc99 },
  { string100, fn_funcall, 0217, doc100 },
  { string101, fn_append, 0207, doc101 },
  { string102, fn_mapc, 0227, doc102 },
  { string103, fn_mapl, 0227, doc103 },
  { string104, fn_mapcar, 0227, doc104 },
  { string105, fn_mapcan, 0227, doc105 },
  { string106, fn_maplist, 0227, doc106 },
  { string107, fn_mapcon, 0227, doc107 },
  { string108, fn_add, 0207, doc108 },
  { string109, fn_subtract, 0217, doc109 },
  { string110, fn_multiply, 0207, doc110 },
  { string111, fn_divide, 0217, doc111 },
  { string112, fn_mod, 0222, doc112 },
  { string113, fn_rem, 0222, doc113 },
  { string114, fn_oneplus, 0211, doc114 },
  { string115, fn_oneminus, 0211, doc115 },
  { string116, fn_abs, 0211, doc116 },
  { string117, fn_random, 0211, doc117 },
  { string118, fn_maxfn, 0217, doc118 },
  { string119, fn_minfn, 0217, doc119 },
  { string120, fn_noteq, 0217, doc120 },
  { string121, fn_numeq, 0217, doc121 },
  { string122, fn_less, 0217, doc122 },
  { string123, fn_lesseq, 0217, doc123 },
  { string124, fn_greater, 0217, doc124 },
  { string125, fn_greatereq, 0217, doc125 },
  { string126, fn_plusp, 0211, doc126 },
  { string127, fn_minusp, 0211, doc127 },
  { string128, fn_zerop, 0211, doc128 },
  { string129, fn_oddp, 0211, doc129 },
  { string130, fn_evenp, 0211, doc130 },
  { string131, fn_integerp, 0211, doc131 },
  { string132, fn_numberp, 0211, doc132 },
  { string133, fn_floatfn, 0211, doc133 },
  { string134, fn_floatp, 0211, doc134 },
  { string135, fn_sin, 0211, doc135 },
  { string136, fn_cos, 0211, doc136 },
  { string137, fn_tan, 0211, doc137 },
  { string138, fn_asin, 0211, doc138 },
  { string139, fn_acos, 0211, doc139 },
  { string140, fn_atan, 0212, doc140 },
  { string141, fn_sinh, 0211, doc141 },
  { string142, fn_cosh, 0211, doc142 },
  { string143, fn_tanh, 0211, doc143 },
  { string144, fn_exp, 0211, doc144 },
  { string145, fn_sqrt, 0211, doc145 },
  { string146, fn_log, 0212, doc146 },
  { string147, fn_expt, 0222, doc147 },
  { string148, fn_ceiling, 0212, doc148 },
  { string149, fn_floor, 0212, doc149 },
  { string150, fn_truncate, 0212, doc150 },
  { string151, fn_round, 0212, doc151 },
  { string152, fn_charcode, 0211, doc152 },
  { string153, fn_codechar, 0211, doc153 },
  { string154, fn_characterp, 0211, doc154 },
  { string155, fn_stringp, 0211, doc155 },
  { string156, fn_stringeq, 0222, doc156 },
  { string157, fn_stringless, 0222, doc157 },
  { string158, fn_stringgreater, 0222, doc158 },
  { string159, fn_stringnoteq, 0222, doc159 },
  { string160, fn_stringlesseq, 0222, doc160 },
  { string161, fn_stringgreatereq, 0222, doc161 },
  { string162, fn_sort, 0222, doc162 },
  { string163, fn_concatenate, 0217, doc163 },
  { string164, fn_subseq, 0223, doc164 },
  { string165, fn_search, 0224, doc165 },
  { string166, fn_readfromstring, 0211, doc166 },
  { string167, fn_princtostring, 0211, doc167 },
  { string168, fn_prin1tostring, 0211, doc168 },
  { string169, fn_logand, 0207, doc169 },
  { string170, fn_logior, 0207, doc170 },
  { string171, fn_logxor, 0207, doc171 },
  { string172, fn_lognot, 0211, doc172 },
  { string173, fn_ash, 0222, doc173 },
  { string174, fn_logbitp, 0222, doc174 },
  { string175, fn_eval, 0211, doc175 },
  { string176, fn_return, 0201, doc176 },
  { string177, fn_globals, 0200, doc177 },
  { string178, fn_locals, 0200, doc178 },
  { string179, fn_makunbound, 0211, doc179 },
  { string180, fn_break, 0200, doc180 },
  { string181, fn_read, 0201, doc181 },
  { string182, fn_prin1, 0212, doc182 },
  { string183, fn_print, 0212, doc183 },
  { string184, fn_princ, 0212, doc184 },
  { string185, fn_terpri, 0201, doc185 },
  { string186, fn_readbyte, 0202, doc186 },
  { string187, fn_readline, 0201, doc187 },
  { string188, fn_writebyte, 0212, doc188 },
  { string189, fn_writestring, 0212, doc189 },
  { string190, fn_writeline, 0212, doc190 },
  { string191, fn_restarti2c, 0212, doc191 },
  { string192, fn_gc, 0201, doc192 },
  { string193, fn_room, 0200, doc193 },
  { string194, fn_backtrace, 0201, doc194 },
  { string195, fn_saveimage, 0201, doc195 },
  { string196, fn_loadimage, 0201, doc196 },
  { string197, fn_cls, 0200, doc197 },
  { string198, fn_digitalread, 0211, doc198 },
  { string199, fn_analogreadresolution, 0211, doc199 },
  { string200, fn_analogwrite, 0222, doc200 },
  { string201, fn_delay, 0211, doc201 },
  { string202, fn_millis, 0200, doc202 },
  { string203, fn_sleep, 0201, doc203 },
  { string204, fn_note, 0203, doc204 },
  { string205, fn_edit, 0211, doc205 },
  { string206, fn_pprint, 0212, doc206 },
  { string207, fn_pprintall, 0201, doc207 },
  { string208, fn_require, 0211, doc208 },
  { string209, fn_listlibrary, 0200, doc209 },
  { string210, sp_help, 0311, doc210 },
  { string211, fn_documentation, 0212, doc211 },
  { string212, fn_apropos, 0211, doc212 },
  { string213, fn_aproposlist, 0211, doc213 },
  { string214, sp_unwindprotect, 0307, doc214 },
  { string215, sp_ignoreerrors, 0307, doc215 },
  { string216, sp_error, 0317, doc216 },
  { string217, fn_directory, 0200, doc217 },
  { string218, sp_withclient, 0317, doc218 },
  { string219, fn_available, 0211, doc219 },
  { string220, fn_wifiserver, 0200, doc220 },
  { string221, fn_wifisoftap, 0204, doc221 },
  { string222, fn_connected, 0211, doc222 },
  { string223, fn_wifilocalip, 0200, doc223 },
  { string224, fn_wificonnect, 0203, doc224 },
  { string225, sp_withgfx, 0317, doc225 },
  { string226, fn_drawpixel, 0223, doc226 },
  { string227, fn_drawline, 0245, doc227 },
  { string228, fn_drawrect, 0245, doc228 },
  { string229, fn_fillrect, 0245, doc229 },
  { string230, fn_drawcircle, 0234, doc230 },
  { string231, fn_fillcircle, 0234, doc231 },
  { string232, fn_drawroundrect, 0256, doc232 },
  { string233, fn_fillroundrect, 0256, doc233 },
  { string234, fn_drawtriangle, 0267, doc234 },
  { string235, fn_filltriangle, 0267, doc235 },
  { string236, fn_drawchar, 0236, doc236 },
  { string237, fn_setcursor, 0222, doc237 },
  { string238, fn_settextcolor, 0212, doc238 },
  { string239, fn_settextsize, 0211, doc239 },
  { string240, fn_settextwrap, 0211, doc240 },
  { string241, fn_fillscreen, 0201, doc241 },
  { string242, fn_setrotation, 0211, doc242 },
  { string243, fn_invertdisplay, 0211, doc243 },
  { string244, fn_getkey, 0200, doc244 },
  { string245, (fn_ptr_type)LED_BUILTIN, 0, NULL },
  { string246, (fn_ptr_type)HIGH, DIGITALWRITE, NULL },
  { string247, (fn_ptr_type)LOW, DIGITALWRITE, NULL },
  { string248, (fn_ptr_type)INPUT, PINMODE, NULL },
  { string249, (fn_ptr_type)INPUT_PULLUP, PINMODE, NULL },
  { string250, (fn_ptr_type)INPUT_PULLDOWN, PINMODE, NULL },
  { string251, (fn_ptr_type)OUTPUT, PINMODE, NULL },
};

#if !defined(extensions)
// Table cross-reference functions

tbl_entry_t *tables[] = {lookup_table, NULL};
const unsigned int tablesizes[] = { arraysize(lookup_table), 0 };

const tbl_entry_t *table (int n) {
  return tables[n];
}

unsigned int tablesize (int n) {
  return tablesizes[n];
}
#endif

// Table lookup functions

builtin_t lookupbuiltin (char* c) {
  unsigned int start = tablesize(0);
  for (int n=1; n>=0; n--) {
    int entries = tablesize(n);
    for (int i=0; i<entries; i++) {
      if (strcasecmp(c, (char*)(table(n)[i].string)) == 0)
        return (builtin_t)(start + i);
    }
    start = 0;
  }
  return ENDFUNCTIONS;
}

intptr_t lookupfn (builtin_t name) {
  bool n = name<tablesize(0);
  return (intptr_t)table(n?0:1)[n?name:name-tablesize(0)].fptr;
}

uint8_t getminmax (builtin_t name) {
  bool n = name<tablesize(0);
  return table(n?0:1)[n?name:name-tablesize(0)].minmax;
}

void checkminmax (builtin_t name, int nargs) {
  if (!(name < ENDFUNCTIONS)) error2("not a builtin");
  uint8_t minmax = getminmax(name);
  if (nargs<((minmax >> 3) & 0x07)) error2(toofewargs);
  if ((minmax & 0x07) != 0x07 && nargs>(minmax & 0x07)) error2(toomanyargs);
}

char *lookupdoc (builtin_t name) {
  bool n = name<tablesize(0);
  return (char*)table(n?0:1)[n?name:name-tablesize(0)].doc;
}

bool findsubstring (char *part, builtin_t name) {
  bool n = name<tablesize(0);
  return (strstr(table(n?0:1)[n?name:name-tablesize(0)].string, part) != NULL);
}

void testescape () {
  if (Serial.available() && Serial.read() == '~') error2("escape!");
}

bool colonp (symbol_t name) {
  if (!longnamep(name)) return false;
  object *form = (object *)name;
  if (form == NULL) return false;
  return (((form->chars)>>((sizeof(int)-1)*8) & 0xFF) == ':');
}

bool keywordp (object *obj) {
  if (!(symbolp(obj) && builtinp(obj->name))) return false;
  builtin_t name = builtin(obj->name);
  bool n = name<tablesize(0);
  const char *s = table(n?0:1)[n?name:name-tablesize(0)].string;
  char c = s[0];
  return (c == ':');
}

void backtrace (symbol_t name) {
  Backtrace[TraceTop] = (name == sym(NIL)) ? sym(LAMBDA) : name;
  TraceTop = modbacktrace(TraceTop+1);
  if (TraceStart == TraceTop) TraceStart = modbacktrace(TraceStart+1);
}

// Main evaluator

object *eval (object *form, object *env) {
  bool stackpos;
  int TC=0;
  EVAL:
  // Enough space?
  // Serial.println((uint32_t)StackBottom - (uint32_t)&stackpos); // Find best MAX_STACK value
  if ((uint32_t)StackBottom - (uint32_t)&stackpos > MAX_STACK) { Context = NIL; error2("stack overflow"); }
  if (Freespace <= WORKSPACESIZE>>4) gc(form, env);
  // Escape
  if (tstflag(ESCAPE)) { clrflag(ESCAPE); error2("escape!");}
  if (!tstflag(NOESC)) testescape();

  if (form == NULL) return nil;

  if (form->type >= NUMBER && form->type <= STRING) return form;

  if (symbolp(form)) {
    symbol_t name = form->name;
    if (colonp(name)) return form; // Keyword
    object *pair = value(name, env);
    if (pair != NULL) return cdr(pair);
    pair = value(name, GlobalEnv);
    if (pair != NULL) return cdr(pair);
    else if (builtinp(name)) {
      if (name == sym(FEATURES)) return features();
      return form;
    }
    Context = NIL;
    error("undefined", form);
  }

  // It's a list
  object *function = car(form);
  object *args = cdr(form);

  if (function == NULL) error(illegalfn, function);
  if (!listp(args)) error("can't evaluate a dotted pair", args);

  // List starts with a builtin symbol?
  if (symbolp(function) && builtinp(function->name)) {
    builtin_t name = builtin(function->name);

    if ((name == LET) || (name == LETSTAR)) {
      if (args == NULL) error2(noargument);
      object *assigns = first(args);
      if (!listp(assigns)) error(notalist, assigns);
      object *forms = cdr(args);
      object *newenv = env;
      protect(newenv);
      while (assigns != NULL) {
        object *assign = car(assigns);
        if (!consp(assign)) push(cons(assign,nil), newenv);
        else if (cdr(assign) == NULL) push(cons(first(assign),nil), newenv);
        else push(cons(first(assign), eval(second(assign),env)), newenv);
        car(GCStack) = newenv;
        if (name == LETSTAR) env = newenv;
        assigns = cdr(assigns);
      }
      env = newenv;
      unprotect();
      form = tf_progn(forms,env);
      goto EVAL;
    }

    if (name == LAMBDA) {
      if (env == NULL) return form;
      object *envcopy = NULL;
      while (env != NULL) {
        object *pair = first(env);
        if (pair != NULL) push(pair, envcopy);
        env = cdr(env);
      }
      return cons(bsymbol(CLOSURE), cons(envcopy,args));
    }

    switch(fntype(name)) {    
      case SPECIAL_FORMS:
        Context = name;
        checkargs(args);
        return ((fn_ptr_type)lookupfn(name))(args, env);
  
      case TAIL_FORMS:
        Context = name;
        checkargs(args);
        form = ((fn_ptr_type)lookupfn(name))(args, env);
        TC = 1;
        goto EVAL;
     
      case OTHER_FORMS: error(illegalfn, function);
    }
  }

  // Evaluate the parameters - result in head
  int TCstart = TC;
  object *head;
  if (consp(function) && !(isbuiltin(car(function), LAMBDA) || isbuiltin(car(function), CLOSURE)
    || car(function)->type == CODE)) { Context = NIL; error(illegalfn, function); }
  if (symbolp(function)) {
    object *pair = findpair(function, env);
    if (pair != NULL) head = cons(cdr(pair), NULL); else head = cons(function, NULL);
  } else head = cons(eval(function, env), NULL);
  protect(head); // Don't GC the result list
  object *tail = head;
  form = cdr(form);
  int nargs = 0;

  while (form != NULL){
    object *obj = cons(eval(car(form),env),NULL);
    cdr(tail) = obj;
    tail = obj;
    form = cdr(form);
    nargs++;
  }

  object *fname = function;
  function = car(head);
  args = cdr(head);

  if (symbolp(function)) {
    if (!builtinp(function->name)) { Context = NIL; error(illegalfn, function); }
    builtin_t bname = builtin(function->name);
    Context = bname;
    checkminmax(bname, nargs);
    object *result = ((fn_ptr_type)lookupfn(bname))(args, env);
    unprotect();
    return result;
  }

  if (consp(function)) {
    symbol_t name = sym(NIL);
    if (!listp(fname)) name = fname->name;

    if (isbuiltin(car(function), LAMBDA)) { 
      if (tstflag(BACKTRACE)) backtrace(name);
      form = closure(TCstart, name, function, args, &env);
      unprotect();
      int trace = tracing(name);
      if (trace || tstflag(BACKTRACE)) {
        object *result = eval(form, env);
        if (trace) {
          indent((--(TraceDepth[trace-1]))<<1, ' ', pserial);
          pint(TraceDepth[trace-1], pserial);
          pserial(':'); pserial(' ');
          printobject(fname, pserial); pfstring(" returned ", pserial);
          printobject(result, pserial); pln(pserial);
        }
        if (tstflag(BACKTRACE)) TraceTop = modbacktrace(TraceTop-1);
        return result;
      } else {
        TC = 1;
        goto EVAL;
      }
    }

    if (isbuiltin(car(function), CLOSURE)) {
      function = cdr(function);
      if (tstflag(BACKTRACE)) backtrace(name);
      form = closure(TCstart, name, function, args, &env);
      unprotect();
      if (tstflag(BACKTRACE)) {
        object *result = eval(form, env);
        TraceTop = modbacktrace(TraceTop-1);
        return result;
      } else {
        TC = 1;
        goto EVAL;
      }
    }

  }
  error(illegalfn, fname); return nil;
}

// Print functions

void pserial (char c) {
  LastPrint = c;
  if (!tstflag(NOECHO)) Display(c);         // Don't display on T-Deck when paste in listing
  #if defined (serialmonitor)
  if (c == '\n') Serial.write('\r');
  Serial.write(c);
  #endif
}

const char ControlCodes[] = "Null\0SOH\0STX\0ETX\0EOT\0ENQ\0ACK\0Bell\0Backspace\0Tab\0Newline\0VT\0"
"Page\0Return\0SO\0SI\0DLE\0DC1\0DC2\0DC3\0DC4\0NAK\0SYN\0ETB\0CAN\0EM\0SUB\0Escape\0FS\0GS\0RS\0US\0Space\0";

void pcharacter (uint8_t c, pfun_t pfun) {
  if (!tstflag(PRINTREADABLY)) pfun(c);
  else {
    pfun('#'); pfun('\\');
    if (c <= 32) {
      const char *p = ControlCodes;
      while (c > 0) {p = p + strlen(p) + 1; c--; }
      pfstring(p, pfun);
    } else if (c < 127) pfun(c);
    else pint(c, pfun);
  }
}

void pstring (char *s, pfun_t pfun) {
  while (*s) pfun(*s++);
}

void plispstring (object *form, pfun_t pfun) {
  plispstr(form->name, pfun);
}

void plispstr (symbol_t name, pfun_t pfun) {
  object *form = (object *)name;
  while (form != NULL) {
    int chars = form->chars;
    for (int i=(sizeof(int)-1)*8; i>=0; i=i-8) {
      char ch = chars>>i & 0xFF;
      if (tstflag(PRINTREADABLY) && (ch == '"' || ch == '\\')) pfun('\\');
      if (ch) pfun(ch);
    }
    form = car(form);
  }
}

void printstring (object *form, pfun_t pfun) {
  if (tstflag(PRINTREADABLY)) pfun('"');
  plispstr(form->name, pfun);
  if (tstflag(PRINTREADABLY)) pfun('"');
}

void pbuiltin (builtin_t name, pfun_t pfun) {
  int n = name<tablesize(0);
  const char *s = table(n?0:1)[n?name:name-tablesize(0)].string;
  while (1) {
    char c = *s++;
    if (c == 0) return;
    pfun(c);
  }
}

void pradix40 (symbol_t name, pfun_t pfun) {
  uint32_t x = untwist(name);
  for (int d=102400000; d>0; d = d/40) {
    uint32_t j = x/d;
    char c = fromradix40(j);
    if (c == 0) return;
    pfun(c); x = x - j*d;
  }
}

void printsymbol (object *form, pfun_t pfun) {
  psymbol(form->name, pfun);
}

void psymbol (symbol_t name, pfun_t pfun) {
  if (longnamep(name)) plispstr(name, pfun);
  else {
    uint32_t value = untwist(name);
    if (value < PACKEDS) error2("invalid symbol");
    else if (value >= BUILTINS) pbuiltin((builtin_t)(value-BUILTINS), pfun);
    else pradix40(name, pfun);
  }
}

void pfstring (const char *s, pfun_t pfun) {
  while (1) {
    char c = *s++;
    if (c == 0) return;
    pfun(c);
  }
}

void pint (int i, pfun_t pfun) {
  uint32_t j = i;
  if (i<0) { pfun('-'); j=-i; }
  pintbase(j, 10, pfun);
}

void pintbase (uint32_t i, uint8_t base, pfun_t pfun) {
  int lead = 0; uint32_t p = 1000000000;
  if (base == 2) p = 0x80000000; else if (base == 16) p = 0x10000000;
  for (uint32_t d=p; d>0; d=d/base) {
    uint32_t j = i/d;
    if (j!=0 || lead || d==1) { pfun((j<10) ? j+'0' : j+'W'); lead=1;}
    i = i - j*d;
  }
}

void pmantissa (float f, pfun_t pfun) {
  int sig = floor(log10(f));
  int mul = pow(10, 5 - sig);
  int i = round(f * mul);
  bool point = false;
  if (i == 1000000) { i = 100000; sig++; }
  if (sig < 0) {
    pfun('0'); pfun('.'); point = true;
    for (int j=0; j < - sig - 1; j++) pfun('0');
  }
  mul = 100000;
  for (int j=0; j<7; j++) {
    int d = (int)(i / mul);
    pfun(d + '0');
    i = i - d * mul;
    if (i == 0) {
      if (!point) {
        for (int k=j; k<sig; k++) pfun('0');
        pfun('.'); pfun('0');
      }
      return;
    }
    if (j == sig && sig >= 0) { pfun('.'); point = true; }
    mul = mul / 10;
  }
}

void pfloat (float f, pfun_t pfun) {
  if (isnan(f)) { pfstring("NaN", pfun); return; }
  if (f == 0.0) { pfun('0'); return; }
  if (isinf(f)) { pfstring("Inf", pfun); return; }
  if (f < 0) { pfun('-'); f = -f; }
  // Calculate exponent
  int e = 0;
  if (f < 1e-3 || f >= 1e5) {
    e = floor(log(f) / 2.302585); // log10 gives wrong result
    f = f / pow(10, e);
  }

  pmantissa (f, pfun);

  // Exponent
  if (e != 0) {
    pfun('e');
    pint(e, pfun);
  }
}

inline void pln (pfun_t pfun) {
  pfun('\n');
}

void pfl (pfun_t pfun) {
  if (LastPrint != '\n') pfun('\n');
}

void plist (object *form, pfun_t pfun) {
  pfun('(');
  printobject(car(form), pfun);
  form = cdr(form);
  while (form != NULL && listp(form)) {
    pfun(' ');
    printobject(car(form), pfun);
    form = cdr(form);
  }
  if (form != NULL) {
    pfstring(" . ", pfun);
    printobject(form, pfun);
  }
  pfun(')');
}

void pstream (object *form, pfun_t pfun) {
  pfun('<');
  pfstring(streamname[(form->integer)>>8], pfun);
  pfstring("-stream ", pfun);
  pint(form->integer & 0xFF, pfun);
  pfun('>');
}

void printobject (object *form, pfun_t pfun) {
  if (form == NULL) pfstring("nil", pfun);
  else if (listp(form) && isbuiltin(car(form), CLOSURE)) pfstring("<closure>", pfun);
  else if (listp(form)) plist(form, pfun);
  else if (integerp(form)) pint(form->integer, pfun);
  else if (floatp(form)) pfloat(form->single_float, pfun);
  else if (symbolp(form)) { if (form->name != sym(NOTHING)) printsymbol(form, pfun); }
  else if (characterp(form)) pcharacter(form->chars, pfun);
  else if (stringp(form)) printstring(form, pfun);
  else if (arrayp(form)) printarray(form, pfun);
  else if (streamp(form)) pstream(form, pfun);
  else error2("error in print");
}

void prin1object (object *form, pfun_t pfun) {
  flags_t temp = Flags;
  clrflag(PRINTREADABLY);
  printobject(form, pfun);
  Flags = temp;
}

// T-deck terminal and keyboard support

const int Columns = 53;
const int Leading = 10; // Between 8 and 10
const int Lines = 240/Leading;
const int LastColumn = Columns-1;
const int LastLine = Lines-1;
const char Cursor = 0x5f;

volatile int WritePtr = 0, ReadPtr = 0;
const int KybdBufSize = Columns*Lines;
char KybdBuf[KybdBufSize], ScrollBuf[Columns][Lines];
volatile uint8_t KybdAvailable = 0;
uint8_t Scroll = 0;

// Terminal **********************************************************************************

// Plot character at absolute character cell position
void PlotChar (uint8_t ch, uint8_t line, uint8_t column) {
 #if defined(gfxsupport)
  uint16_t y = line*Leading;
  uint16_t x = column*6;
  ScrollBuf[column][(line+Scroll) % Lines] = ch;
  if (ch & 0x80) {
    tft.drawChar(x, y, ch & 0x7f, COLOR_BLACK, COLOR_GREEN, 1);
  } else {
    tft.drawChar(x, y, ch & 0x7f, COLOR_WHITE, COLOR_BLACK, 1);
  }
#endif
}

// Clears the bottom line and then scrolls the display up by one line
void ScrollDisplay () {
  #if defined(gfxsupport)
  tft.fillRect(0, 240-Leading, 320, 10, COLOR_BLACK);
  for (uint8_t x = 0; x < Columns; x++) {
    char c = ScrollBuf[x][Scroll];
    for (uint8_t y = 0; y < Lines-1; y++) {
      char c2 = ScrollBuf[x][(y+Scroll+1) % Lines];
      if (c != c2) {
        if (c2 & 0x80) {
          tft.drawChar(x*6, y*Leading, c2 & 0x7f, COLOR_BLACK, COLOR_GREEN, 1);
        } else {
          tft.drawChar(x*6, y*Leading, c2 & 0x7f, COLOR_WHITE, COLOR_BLACK, 1);
        }
        c = c2;
      }
    }
  }
  // Tidy up graphics
  for (uint8_t y = 0; y < Lines-1; y++) tft.fillRect(0, y*Leading+8, 320, 2, COLOR_BLACK);
  tft.fillRect(318, 0, 3, 240, COLOR_BLACK);
  for (int x=0; x<Columns; x++) ScrollBuf[x][Scroll] = 0;
  Scroll = (Scroll + 1) % Lines;
  #endif
}

const char VT = 11; // Vertical tab
const char BEEP = 7;

// Prints a character to display, with cursor, handling control characters
void Display (char c) {
  #if defined(gfxsupport)
  static bool displayDisabled = false;
  if (c == 14) displayDisabled = true;
  if (c == 15) displayDisabled = false;
  if (displayDisabled) return;
  static uint8_t line = 0, column = 0;
  static bool invert = false;
  // These characters don't affect the cursor
  if (c == 8) {                    // Backspace
    //PlotChar(' ', line+Scroll, Column); //hide cursor?
    if (column == 0) {
      line--; column = LastColumn;
    } else column--;
    //PlotChar(Cursor, line+Scroll, Column); //show cursor
    return;
  }
  if (c == 9) {                    // Cursor forward
    if (column == LastColumn) {
      line++; column = 0;
    } else column++;
    return;
  }
  if ((c >= 17) && (c <= 20)) {    // Parentheses
    if (c == 17) PlotChar('(', line, column);
    else if (c == 18) PlotChar('(' | 0x80, line, column);
    else if (c == 19) PlotChar(')', line, column);
    else PlotChar(')' | 0x80, line, column);
    return;
  }
  if (c == STX) { invert = true; return; }
  if (c == ETX) { invert = false; return; }
  // Hide cursor
  PlotChar(' ', line, column);
  if (c == 0x7F) {                 // DEL
    if (column == 0) {
      line--; column = LastColumn;
    } else column--;
  } else if ((c & 0x7f) >= 32) {   // Normal character
    if (invert) PlotChar(c | 0x80, line, column++); else PlotChar(c, line, column++);
    if (column > LastColumn) {
      column = 0;
      if (line == LastLine) ScrollDisplay(); else line++;
    }
  // Control characters
  } else if (c == 12) {            // Clear display
    tft.fillScreen(COLOR_BLACK); line = 0; column = 0; Scroll = 0;
    for (int col = 0; col < Columns; col++) {
      for (int row = 0; row < Lines; row++) {
        ScrollBuf[col][row] = 0;
      }
    }
  } else if (c == '\n') {          // Newline
    column = 0;
    if (line == LastLine) ScrollDisplay(); else line++;
  } else if (c == VT) {
    column = 0; Scroll = 0; line = LastLine - 2;
  } else if (c == BEEP) tone(0, 440, 125); // Beep
  // Show cursor
  PlotChar(Cursor, line, column);
 #endif
}

// Keyboard **********************************************************************************

void initkybd () {
  // The second I2C port is for the peripherals, for now just keyboard
  I2Cinit(&Wire1, TDECK_I2C_SDA, TDECK_I2C_SCL, 1);
}

// Parenthesis highlighting
void Highlight (int p, uint8_t invert) {
  if (p) {
    for (int n=0; n < p; n++) Display(8);
    Display(17 + invert);
    for (int n=1; n < p; n++) Display(9);
    Display(19 + invert);
    Display(9);
  }
}
  
void ProcessKey (char c) {
  static int parenthesis = 0;
  if (c == 27) { setflag(ESCAPE); return; }    // Escape key
  // Undo previous parenthesis highlight
  Highlight(parenthesis, 0);
  parenthesis = 0;
  // Edit buffer
  if (c == '\n' || c == '\r') {
    pserial('\n');
    KybdAvailable = 1;
    ReadPtr = 0;
    return;
  }
  if (c == 8 || c == 0x7f) {     // Backspace key
    if (WritePtr > 0) {
      WritePtr--;
      Display(0x7F);
      if (WritePtr) c = KybdBuf[WritePtr-1];
    }
  } else if (WritePtr < KybdBufSize) {
    KybdBuf[WritePtr++] = c;
    Display(c);
  }
  // Do new parenthesis highlight
  if (c == ')') {
    int search = WritePtr-1, level = 0;
    while (search >= 0 && parenthesis == 0) {
      c = KybdBuf[search--];
      if (c == ')') level++;
      if (c == '(') {
        level--;
        if (level == 0) parenthesis = WritePtr-search-1;
      }
    }
    Highlight(parenthesis, 1);
  }
  return;
}
// Read functions

int glibrary () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  char c = LispLibrary[GlobalStringIndex++];
  return (c != 0) ? c : -1; // -1?
}

void loadfromlibrary (object *env) {
  GlobalStringIndex = 0;
  object *line = read(glibrary);
  while (line != NULL) {
    protect(line);
    eval(line, env);
    unprotect();
    line = read(glibrary);
  }
}

int gserial () {
  if (LastChar) {
    char temp = LastChar;
    LastChar = 0;
    return temp;
  }
  #if defined (serialmonitor)
  unsigned long start = millis();
  while (!KybdAvailable) {
    if (millis() - start > 1000) clrflag(NOECHO);
    if (Serial.available()) {
      char temp = Serial.read();
      if (temp != '\n' && !tstflag(NOECHO)) Serial.print(temp);
      return temp;
    } else {
      Wire1.requestFrom(0x55, 1);
      if (Wire1.available()) {
        char temp = Wire1.read();
        if ((temp != 0) && (temp !=255)) {
          temp = touchKeyMod(temp);
          ProcessKey(temp);
        }
      }
    }
    // if(ball_val != 0){
    //   ProcessKey(ball_val);
    //   ball_val = 0;
    // }
  }
  if (ReadPtr != WritePtr) {
    char temp = KybdBuf[ReadPtr++];
    return temp;
  }
  KybdAvailable = 0;
  WritePtr = 0;
  return '\n';
  #else
  while (!KybdAvailable) {
    Wire1.requestFrom(0x55, 1);
    if (Wire1.available()) {
      char temp = Wire1.read();
      if ((temp != 0) && (temp !=255)) {
        temp = touchKeyMod(temp);
        ProcessKey(temp);
      }
    }
    // if(ball_val != 0){
    //   ProcessKey(ball_val);
    //   ball_val = 0;
    // }
  }
  if (ReadPtr != WritePtr) {
    char temp = KybdBuf[ReadPtr++];
    return temp;
  }
  KybdAvailable = 0;
  WritePtr = 0;
  return '\n';
  #endif
}

object *nextitem (gfun_t gfun) {
  int ch = gfun();
  while(issp(ch)) ch = gfun();

  if (ch == ';') {
    do { ch = gfun(); if (ch == ';' || ch == '(') setflag(NOECHO); }
    while(ch != '(');
  }
  if (ch == '\n') ch = gfun();
  if (ch == -1) return nil;
  if (ch == ')') return (object *)KET;
  if (ch == '(') return (object *)BRA;
  if (ch == '\'') return (object *)QUO;

  // Parse string
  if (ch == '"') return readstring('"', true, gfun);

  // Parse symbol, character, or number
  int index = 0, base = 10, sign = 1;
  char buffer[BUFFERSIZE];
  int bufmax = BUFFERSIZE-3; // Max index
  unsigned int result = 0;
  bool isfloat = false;
  float fresult = 0.0;

  if (ch == '+') {
    buffer[index++] = ch;
    ch = gfun();
  } else if (ch == '-') {
    sign = -1;
    buffer[index++] = ch;
    ch = gfun();
  } else if (ch == '.') {
    buffer[index++] = ch;
    ch = gfun();
    if (ch == ' ') return (object *)DOT;
    isfloat = true;
  }

  // Parse reader macros
  else if (ch == '#') {
    ch = gfun();
    char ch2 = ch & ~0x20; // force to upper case
    if (ch == '\\') { // Character
      base = 0; ch = gfun();
      if (issp(ch) || isbr(ch)) return character(ch);
      else LastChar = ch;
    } else if (ch == '|') {
      do { while (gfun() != '|'); }
      while (gfun() != '#');
      return nextitem(gfun);
    } else if (ch2 == 'B') base = 2;
    else if (ch2 == 'O') base = 8;
    else if (ch2 == 'X') base = 16;
    else if (ch == '\'') return nextitem(gfun);
    else if (ch == '.') {
      setflag(NOESC);
      object *result = eval(read(gfun), NULL);
      clrflag(NOESC);
      return result;
    }
    else if (ch == '(') { LastChar = ch; return readarray(1, read(gfun)); }
    else if (ch == '*') return readbitarray(gfun);
    else if (ch >= '1' && ch <= '9' && (gfun() & ~0x20) == 'A') return readarray(ch - '0', read(gfun));
    else error2("illegal character after #");
    ch = gfun();
  }
  int valid; // 0=undecided, -1=invalid, +1=valid
  if (ch == '.') valid = 0; else if (digitvalue(ch)<base) valid = 1; else valid = -1;
  bool isexponent = false;
  int exponent = 0, esign = 1;
  float divisor = 10.0;

  while(!issp(ch) && !isbr(ch) && index < bufmax) {
    buffer[index++] = ch;
    if (base == 10 && ch == '.' && !isexponent) {
      isfloat = true;
      fresult = result;
    } else if (base == 10 && (ch == 'e' || ch == 'E')) {
      if (!isfloat) { isfloat = true; fresult = result; }
      isexponent = true;
      if (valid == 1) valid = 0; else valid = -1;
    } else if (isexponent && ch == '-') {
      esign = -esign;
    } else if (isexponent && ch == '+') {
    } else {
      int digit = digitvalue(ch);
      if (digitvalue(ch)<base && valid != -1) valid = 1; else valid = -1;
      if (isexponent) {
        exponent = exponent * 10 + digit;
      } else if (isfloat) {
        fresult = fresult + digit / divisor;
        divisor = divisor * 10.0;
      } else {
        result = result * base + digit;
      }
    }
    ch = gfun();
  }

  buffer[index] = '\0';
  if (isbr(ch)) LastChar = ch;
  if (isfloat && valid == 1) return makefloat(fresult * sign * pow(10, exponent * esign));
  else if (valid == 1) {
    if (base == 10 && result > ((unsigned int)INT_MAX+(1-sign)/2))
      return makefloat((float)result*sign);
    return number(result*sign);
  } else if (base == 0) {
    if (index == 1) return character(buffer[0]);
    const char *p = ControlCodes; char c = 0;
    while (c < 33) {
      if (strcasecmp(buffer, p) == 0) return character(c);
      p = p + strlen(p) + 1; c++;
    }
    if (index == 3) return character((buffer[0]*10+buffer[1])*10+buffer[2]-5328);
    error2("unknown character");
  }

  builtin_t x = lookupbuiltin(buffer);
  if (x == NIL) return nil;
  if (x != ENDFUNCTIONS) return bsymbol(x);
  if (index <= 6 && valid40(buffer)) return intern(twist(pack40(buffer)));
  return internlong(buffer);
}

object *readrest (gfun_t gfun) {
  object *item = nextitem(gfun);
  object *head = NULL;
  object *tail = NULL;

  while (item != (object *)KET) {
    if (item == (object *)BRA) {
      item = readrest(gfun);
    } else if (item == (object *)QUO) {
      item = cons(bsymbol(QUOTE), cons(read(gfun), NULL));
    } else if (item == (object *)DOT) {
      tail->cdr = read(gfun);
      if (readrest(gfun) != NULL) error2("malformed list");
      return head;
    } else {
      object *cell = cons(item, NULL);
      if (head == NULL) head = cell;
      else tail->cdr = cell;
      tail = cell;
      item = nextitem(gfun);
    }
  }
  return head;
}

object *read (gfun_t gfun) {
  object *item = nextitem(gfun);
  if (item == (object *)KET) error2("incomplete list");
  if (item == (object *)BRA) return readrest(gfun);
  if (item == (object *)DOT) return read(gfun);
  if (item == (object *)QUO) return cons(bsymbol(QUOTE), cons(read(gfun), NULL));
  return item;
}

// Setup

void initenv () {
  GlobalEnv = NULL;
  tee = bsymbol(TEE);
}

void initBoard () {
  pinMode(TDECK_SDCARD_CS, OUTPUT);
  pinMode(TDECK_LORA_CS, OUTPUT);
  pinMode(TDECK_TFT_CS, OUTPUT);
  digitalWrite(TDECK_SDCARD_CS, HIGH);
  digitalWrite(TDECK_LORA_CS, HIGH);
  digitalWrite(TDECK_TFT_CS, HIGH);
  pinMode(TDECK_SPI_MISO, INPUT_PULLUP);
  SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI); //SD
 
}

void initgfx () {
  //turn on the peripherals
  pinMode(TDECK_PERI_POWERON, OUTPUT);
  digitalWrite(TDECK_PERI_POWERON, HIGH);
  //init screen, has different name than the adafruit library
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(COLOR_BLACK);
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
}

void initsound () {
  I2S.setAllPins(7, 5, 6, 6, 6); // sckPin, fsPin, sdPin, outSdPin, inSdPin
}

void setup () {
  Serial.begin(9600);
  int start = millis();
  while ((millis() - start) < 5000) { if (Serial) break; }
  #if defined(BOARD_HAS_PSRAM)
  if (!psramInit()) { Serial.print("the PSRAM couldn't be initialized"); for(;;); }
  Workspace = (object*) ps_malloc(WORKSPACESIZE*8);
  if (!Workspace) { Serial.print("the Workspace couldn't be allocated"); for(;;); }
  #endif
  int stackhere = 0; StackBottom = &stackhere;
  initworkspace();
  initenv();
  initsleep();
  initBoard();
  initgfx();
  initkybd();
  initsound();
  initTouch();
  inittrackball();

  pfstring(PSTR("uLisp 4.7a "), pserial); pln(pserial);
}

// Read/Evaluate/Print loop

void repl (object *env) {
  for (;;) {
    randomSeed(micros());
    #if defined(printfreespace)
    if (!tstflag(NOECHO)) gc(NULL, env);
    pint(Freespace+1, pserial);
    #endif
    if (BreakLevel) {
      pfstring(" : ", pserial);
      pint(BreakLevel, pserial);
    }
    pserial('>'); pserial(' ');
    Context = NIL;
    object *line = read(gserial);
    // Break handling
    if (BreakLevel) {
      if (line == nil || line == bsymbol(COLONC)) {
        pln(pserial); return;
      } else if (line == bsymbol(COLONA)) {
        pln(pserial); pln(pserial);
        GCStack = NULL;
        longjmp(*handler, 1);
      } else if (line == bsymbol(COLONB)) {
        pln(pserial); printbacktrace();
        line = bsymbol(NOTHING);
      }
    }
    if (line == (object *)KET) error2("unmatched right bracket");
    protect(line);
    pfl(pserial);
    line = eval(line, env);
    pfl(pserial);
    printobject(line, pserial);
    unprotect();
    pfl(pserial);
    pln(pserial);
  }
}

void loop () {
  if (!setjmp(toplevel_handler)) {
    #if defined(resetautorun)
    volatile int autorun = 12; // Fudge to keep code size the same
    #else
    volatile int autorun = 13;
    #endif
    if (autorun == 12) autorunimage();
  }
  ulisperror();
  repl(NULL);
}

void ulisperror () {
  // Come here after error
  delay(100); while (Serial.available()) Serial.read();
  clrflag(NOESC); BreakLevel = 0; TraceStart = 0; TraceTop = 0;
  for (int i=0; i<TRACEMAX; i++) TraceDepth[i] = 0;
  #if defined(sdcardsupport)
  SDpfile.close(); SDgfile.close();
  #endif
  #if defined(lisplibrary)
  if (!tstflag(LIBRARYLOADED)) { setflag(LIBRARYLOADED); loadfromlibrary(NULL); }
  #endif
  client.stop();
}


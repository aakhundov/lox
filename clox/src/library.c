#include "library.h"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#define CHECK(condition, ...)                                                                      \
  do {                                                                                             \
    if (!(condition)) {                                                                            \
      format_library_error(result, __VA_ARGS__);                                                   \
      return false;                                                                                \
    }                                                                                              \
  } while (0)

__attribute__((format(printf, 2, 3))) static inline void
format_library_error(clox_native_result_t *result, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  (void)vsnprintf(result->error_msg, sizeof(result->error_msg), fmt, ap);
  va_end(ap);
}

// The splitmix64 constants, as its author published them.
#define SPLITMIX64_INCREMENT UINT64_C(0x9E3779B97F4A7C15)
#define SPLITMIX64_FIRST_FACTOR UINT64_C(0xBF58476D1CE4E5B9)
#define SPLITMIX64_SECOND_FACTOR UINT64_C(0x94D049BB133111EB)
#define SPLITMIX64_FIRST_SHIFT 30
#define SPLITMIX64_SECOND_SHIFT 27
#define SPLITMIX64_THIRD_SHIFT 31

// A double holds this many bits of mantissa exactly, so a draw made of that
// many bits is a distinct multiple of 2^-53 and none of them rounds up to 1.
#define DOUBLE_MANTISSA_BITS 53
#define DOUBLE_MANTISSA_DROPPED (64 - DOUBLE_MANTISSA_BITS)
#define DOUBLE_MANTISSA_SCALE 0x1p-53

// One draw in [0, 1), out of the calling VM's own state rather than libc's.
// rand() is the only generator ISO C guarantees, and its state is process-wide:
// two VMs would share one stream, and anything else in the process calling
// rand() would move it. The generator below is splitmix64, which is a few lines
// and carries all its state in one integer.
static double random_double(clox_vm_t *vm) {
  vm->rng_state += SPLITMIX64_INCREMENT;

  uint64_t bits = vm->rng_state;
  bits = (bits ^ (bits >> SPLITMIX64_FIRST_SHIFT)) * SPLITMIX64_FIRST_FACTOR;
  bits = (bits ^ (bits >> SPLITMIX64_SECOND_SHIFT)) * SPLITMIX64_SECOND_FACTOR;
  bits ^= bits >> SPLITMIX64_THIRD_SHIFT;

  return (double)(bits >> DOUBLE_MANTISSA_DROPPED) * DOUBLE_MANTISSA_SCALE;
}

static bool clox_library_fn_clock(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result, clox_vm_t *vm) {
  (void)vm;
  (void)args;
  (void)arg_count;

  result->value = CLOX_NUMBER((double)clock() / CLOCKS_PER_SEC);
  return true;
}

static bool clox_library_fn_sleep(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result, clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_INTEGER(args[0]), "first argument must be integer");

  double seconds = CLOX_AS_NUMBER(args[0]);

  CHECK(seconds >= 0 && seconds <= UINT_MAX, "first argument out of range");

  sleep((unsigned int)seconds);

  result->value = CLOX_NIL;
  return true;
}

// Wall-clock seconds since the epoch, to clock()'s CPU seconds. Resolution is
// one second: time_t is all ISO C offers, and it matches sleep()'s grain.
static bool clox_library_fn_time(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                 clox_vm_t *vm) {
  (void)vm;
  (void)args;
  (void)arg_count;

  result->value = CLOX_NUMBER((double)time(NULL));
  return true;
}

// Always fails, and that is the whole mechanism: the VM has no unwinding path,
// so a C exit() here would halt with the interpreter's entire heap still live.
// Failing routes the halt through the ordinary runtime-error return instead,
// which tears the VM down on the way out and ends a REPL line rather than the
// session.
static bool clox_library_fn_exit(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                 clox_vm_t *vm) {
  (void)vm;
  (void)args;
  (void)arg_count;

  format_library_error(result, "exited");
  return false;
}

// Collects the calling VM's heap there and then, rather than at the threshold
// the allocator would have reached on its own, and answers with the bytes it
// reclaimed. Everything the run still needs is reachable while a native is
// running -- the arguments included, which sit on the stack until the call
// returns -- so the collection is safe wherever the program puts the call.
static bool clox_library_fn_gc(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                               clox_vm_t *vm) {
  (void)args;
  (void)arg_count;

  size_t before = vm->allocator->allocated_size;
  clox_collect_garbage(vm->allocator);
  // a collection only ever frees, so the difference is never negative
  size_t reclaimed = before - vm->allocator->allocated_size;

  result->value = CLOX_NUMBER((double)reclaimed);
  return true;
}

// Seeds the calling VM, and only it: a second VM in the same process keeps the
// stream it was on.
static bool clox_library_fn_seed(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                 clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_INTEGER(args[0]), "first argument must be integer");

  double seed = CLOX_AS_NUMBER(args[0]);

  CHECK(seed >= 0 && seed <= UINT_MAX, "first argument out of range");

  vm->rng_state = (uint64_t)seed;

  result->value = CLOX_NIL;
  return true;
}

static bool clox_library_fn_random(size_t arg_count, clox_value_t *args,
                                   clox_native_result_t *result, clox_vm_t *vm) {
  (void)args;
  (void)arg_count;

  result->value = CLOX_NUMBER(random_double(vm));
  return true;
}

static bool clox_library_fn_randint(size_t arg_count, clox_value_t *args,
                                    clox_native_result_t *result, clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_INTEGER(args[0]), "first argument must be integer");

  double bound = CLOX_AS_NUMBER(args[0]);

  CHECK(bound >= 1, "first argument must be positive");

  // random_double() is strictly below 1, so the product stays below bound
  result->value = CLOX_NUMBER(floor(random_double(vm) * bound));
  return true;
}

// No domain checks below: OP_DIVIDE already lets 1/0 through as an infinity,
// so the language's arithmetic is IEEE all the way down and sqrt(-1) has no
// business being the one operation that raises a runtime error instead.
static bool clox_library_fn_sqrt(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                 clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(sqrt(CLOX_AS_NUMBER(args[0])));
  return true;
}

static bool clox_library_fn_pow(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");
  CHECK(CLOX_IS_NUMBER(args[1]), "second argument must be number");

  result->value = CLOX_NUMBER(pow(CLOX_AS_NUMBER(args[0]), CLOX_AS_NUMBER(args[1])));
  return true;
}

static bool clox_library_fn_abs(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(fabs(CLOX_AS_NUMBER(args[0])));
  return true;
}

static bool clox_library_fn_floor(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result, clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(floor(CLOX_AS_NUMBER(args[0])));
  return true;
}

static bool clox_library_fn_ceil(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                 clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(ceil(CLOX_AS_NUMBER(args[0])));
  return true;
}

// fmin / fmax rather than a bare comparison: they return the non-NaN operand,
// so one NaN in the middle of the arguments cannot swallow the whole result.
static bool clox_library_fn_min(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)vm;
  CHECK(arg_count > 0, "function expects at least one argument");
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  double smallest = CLOX_AS_NUMBER(args[0]);
  for (size_t i = 1; i < arg_count; i++) {
    CHECK(CLOX_IS_NUMBER(args[i]), "argument %zu must be number", i + 1);
    smallest = fmin(smallest, CLOX_AS_NUMBER(args[i]));
  }

  result->value = CLOX_NUMBER(smallest);
  return true;
}

static bool clox_library_fn_max(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)vm;
  CHECK(arg_count > 0, "function expects at least one argument");
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  double largest = CLOX_AS_NUMBER(args[0]);
  for (size_t i = 1; i < arg_count; i++) {
    CHECK(CLOX_IS_NUMBER(args[i]), "argument %zu must be number", i + 1);
    largest = fmax(largest, CLOX_AS_NUMBER(args[i]));
  }

  result->value = CLOX_NUMBER(largest);
  return true;
}

// Length in bytes, not characters: strings are byte sequences here, and the
// scanner does nothing to decode UTF-8.
static bool clox_library_fn_len(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  result->value = CLOX_NUMBER((double)CLOX_AS_STRING(args[0])->length);
  return true;
}

static bool clox_library_fn_ord(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)vm;
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  const clox_string_t *string = CLOX_AS_STRING(args[0]);
  CHECK(string->length == 1, "first argument must be one byte long");

  // char may be signed: go through unsigned char so the result is 0..255
  result->value = CLOX_NUMBER((double)(unsigned char)string->chars[0]);
  return true;
}

static bool clox_library_fn_chr(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_INTEGER(args[0]), "first argument must be integer");

  double code = CLOX_AS_NUMBER(args[0]);

  // 0 is left out: a string is NUL-terminated, so none can hold a NUL byte
  CHECK(code >= 1 && code <= UCHAR_MAX, "first argument out of range");

  char byte = (char)(unsigned char)code;

  result->value = CLOX_STRING_COPY(vm->allocator, &byte, 1);
  return true;
}

// The bytes [start, start + length) of the string, clamped to what is there: a
// start past the end reads as the empty string, and a length running past it
// stops at the end, rather than either being an error.
static bool clox_library_fn_substr(size_t arg_count, clox_value_t *args,
                                   clox_native_result_t *result, clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");
  CHECK(CLOX_IS_INTEGER(args[1]), "second argument must be integer");
  CHECK(CLOX_IS_INTEGER(args[2]), "third argument must be integer");

  double start = CLOX_AS_NUMBER(args[1]);
  double length = CLOX_AS_NUMBER(args[2]);

  CHECK(start >= 0, "second argument must not be negative");
  CHECK(length >= 0, "third argument must not be negative");

  const clox_string_t *string = CLOX_AS_STRING(args[0]);
  size_t from = start < (double)string->length ? (size_t)start : string->length;
  size_t remaining = string->length - from;
  size_t count = length < (double)remaining ? (size_t)length : remaining;

  result->value = CLOX_STRING_COPY(vm->allocator, string->chars + from, count);
  return true;
}

// Every byte of the string under (transform). The argument is on the VM's stack
// for as long as the native runs, so a collection the allocation below sets off
// cannot reclaim the string being read.
static clox_value_t map_string(clox_allocator_t *alloc, const clox_string_t *string,
                               int transform(int)) {
  char *chars = CLOX_ARRAY_ALLOCATE(alloc, char, string->length + 1);
  for (size_t i = 0; i < string->length; i++) {
    chars[i] = (char)transform((unsigned char)string->chars[i]);
  }
  chars[string->length] = '\0';

  return CLOX_STRING_MOVE(alloc, chars, string->length);
}

// ASCII is all upper() and lower() change: toupper and tolower go by the
// locale, and the interpreter never leaves the C one it starts in.
static bool clox_library_fn_upper(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result, clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  result->value = map_string(vm->allocator, CLOX_AS_STRING(args[0]), toupper);
  return true;
}

static bool clox_library_fn_lower(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result, clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  result->value = map_string(vm->allocator, CLOX_AS_STRING(args[0]), tolower);
  return true;
}

// The string without the whitespace at either end of it, whitespace being what
// isspace() calls one: space, tab, newline, carriage return, form feed, vertical
// tab.
static bool clox_library_fn_trim(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                 clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  const clox_string_t *string = CLOX_AS_STRING(args[0]);

  size_t start = 0;
  while (start < string->length && isspace((unsigned char)string->chars[start])) {
    start++;
  }
  size_t end = string->length;
  while (end > start && isspace((unsigned char)string->chars[end - 1])) {
    end--;
  }

  result->value = CLOX_STRING_COPY(vm->allocator, string->chars + start, end - start);
  return true;
}

static bool clox_library_fn_repeat(size_t arg_count, clox_value_t *args,
                                   clox_native_result_t *result, clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");
  CHECK(CLOX_IS_INTEGER(args[1]), "second argument must be integer");

  double times = CLOX_AS_NUMBER(args[1]);

  CHECK(times >= 0, "second argument must not be negative");
  // strictly below, so the conversion below stays in range
  CHECK(times < (double)SIZE_MAX, "second argument out of range");

  const clox_string_t *string = CLOX_AS_STRING(args[0]);
  size_t count = (size_t)times;

  CHECK(string->length == 0 || count <= (SIZE_MAX - 1) / string->length, "result is too long");
  size_t total = string->length * count;

  char *chars = CLOX_ARRAY_ALLOCATE(vm->allocator, char, total + 1);
  for (size_t i = 0; i < count; i++) {
    memcpy(chars + (i * string->length), string->chars, string->length);
  }
  chars[total] = '\0';

  result->value = CLOX_STRING_MOVE(vm->allocator, chars, total);
  return true;
}

// Whether (from) stands at (index) of (string).
static inline bool matches_at(const clox_string_t *string, size_t index,
                              const clox_string_t *from) {
  return index + from->length <= string->length &&
         memcmp(string->chars + index, from->chars, from->length) == 0;
}

// Every occurrence of (from) in the string becomes (to), left to right. What a
// replacement writes is not looked at again, so replace("aa", "a", "aa") ends
// after one pass rather than running away.
static bool clox_library_fn_replace(size_t arg_count, clox_value_t *args,
                                    clox_native_result_t *result, clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");
  CHECK(CLOX_IS_STRING(args[1]), "second argument must be string");
  CHECK(CLOX_IS_STRING(args[2]), "third argument must be string");

  const clox_string_t *string = CLOX_AS_STRING(args[0]);
  const clox_string_t *from = CLOX_AS_STRING(args[1]);
  const clox_string_t *to = CLOX_AS_STRING(args[2]);

  // an empty needle stands everywhere, which no replacement can answer
  CHECK(from->length > 0, "second argument must not be empty");

  size_t occurrences = 0;
  for (size_t i = 0; i + from->length <= string->length;) {
    if (matches_at(string, i, from)) {
      occurrences++;
      i += from->length;
    } else {
      i++;
    }
  }

  // what the occurrences leave behind cannot overflow; what they bring can
  size_t kept = string->length - (occurrences * from->length);
  CHECK(to->length == 0 || occurrences <= (SIZE_MAX - 1 - kept) / to->length, "result is too long");
  size_t total = kept + (occurrences * to->length);

  char *chars = CLOX_ARRAY_ALLOCATE(vm->allocator, char, total + 1);
  size_t written = 0;
  for (size_t i = 0; i < string->length;) {
    if (matches_at(string, i, from)) {
      memcpy(chars + written, to->chars, to->length);
      written += to->length;
      i += from->length;
    } else {
      chars[written] = string->chars[i];
      written++;
      i++;
    }
  }
  assert(written == total);
  chars[total] = '\0';

  result->value = CLOX_STRING_MOVE(vm->allocator, chars, total);
  return true;
}

// The name type() reports. A closure and the function it wraps are one thing to
// a program, so both read as "function"; a native keeps a name of its own, as
// the printer keeps <nt clock> apart from <fn f> too.
static const char *value_type_name(clox_value_t val) {
  switch (val.type) {
  case VAL_BOOL:
    return "bool";
  case VAL_NIL:
    return "nil";
  case VAL_NUMBER:
  case VAL_SIZE:
    return "number";
  case VAL_OBJECT:
    switch (CLOX_OBJECT_TYPE(val)) {
    case OBJ_STRING:
      return "string";
    case OBJ_FUNCTION:
    case OBJ_CLOSURE:
    case OBJ_BOUND_METHOD:
      return "function";
    case OBJ_NATIVE:
      return "native";
    case OBJ_CLASS:
      return "class";
    case OBJ_INSTANCE:
      // type of instance is the class name
      return CLOX_AS_INSTANCE(val)->class_->name->chars;
    case OBJ_UPVALUE:
      break; // never a value a program holds
    }
    break;
  }

  assert(false); // every type a program can hold is named above
  return "";
}

static bool clox_library_fn_type(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                 clox_vm_t *vm) {
  (void)arg_count;

  const char *name = value_type_name(args[0]);

  result->value = CLOX_STRING_COPY(vm->allocator, name, strlen(name));
  return true;
}

// The text print writes for an object, sized when (buffer) is NULL and written
// otherwise, as snprintf does. This and format_value below mirror
// clox_object_fprintf and clox_value_fprintf: what a value prints as and what
// str() returns of it must not drift apart.
static int format_object(char *buffer, size_t size, clox_value_t val) {
  switch (CLOX_OBJECT_TYPE(val)) {
  case OBJ_STRING:
    return snprintf(buffer, size, "%s", CLOX_AS_CSTRING(val));
  case OBJ_FUNCTION: {
    const clox_function_t *function = CLOX_AS_FUNCTION(val);
    if (strcmp(function->name, CLOX_SCRIPT_NAME) == 0) {
      return snprintf(buffer, size, "%s", CLOX_SCRIPT_NAME);
    }
    return snprintf(buffer, size, "<fn %s>", function->name);
  }
  case OBJ_NATIVE:
    return snprintf(buffer, size, "<nt %s>", CLOX_AS_NATIVE(val)->name);
  case OBJ_CLOSURE:
    return format_object(buffer, size, CLOX_OBJECT(CLOX_AS_CLOSURE(val)->function));
  case OBJ_CLASS:
    return snprintf(buffer, size, "<cl %s>", CLOX_AS_CLASS(val)->name->chars);
  case OBJ_INSTANCE:
    return snprintf(buffer, size, "<in %s>", CLOX_AS_INSTANCE(val)->class_->name->chars);
  case OBJ_BOUND_METHOD:
    return format_object(buffer, size, CLOX_AS_BOUND_METHOD(val)->method);
  case OBJ_UPVALUE:
    break; // never a value a program holds
  }

  assert(false); // every object a program can hold is written above
  return 0;
}

static int format_value(char *buffer, size_t size, clox_value_t val) {
  switch (val.type) {
  case VAL_BOOL:
    return snprintf(buffer, size, "%s", CLOX_AS_BOOL(val) ? "true" : "false");
  case VAL_NIL:
    return snprintf(buffer, size, "nil");
  case VAL_NUMBER:
    return snprintf(buffer, size, "%g", CLOX_AS_NUMBER(val));
  case VAL_SIZE:
    return snprintf(buffer, size, "%zu", CLOX_AS_SIZE(val));
  case VAL_OBJECT:
    return format_object(buffer, size, val);
  }
}

static bool clox_library_fn_str(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                                clox_vm_t *vm) {
  (void)arg_count;

  if (CLOX_IS_STRING(args[0])) {
    result->value = args[0]; // already the text str() would build
    return true;
  }

  // sized first and written second: a function's name is as long as it is, and
  // no fixed buffer would hold every one of them
  int needed = format_value(NULL, 0, args[0]);
  assert(needed >= 0);
  size_t length = (size_t)needed;

  char *chars = CLOX_ARRAY_ALLOCATE(vm->allocator, char, length + 1);
  (void)format_value(chars, length + 1, args[0]);

  result->value = CLOX_STRING_MOVE(vm->allocator, chars, length);
  return true;
}

#define READ_LINE_INITIAL_CAPACITY 128

// One line of standard input, without the newline that ends it. Input that ends
// before a newline arrives reads as what came before it, so end of input on its
// own reads as the empty string, exactly as an empty line does.
static bool clox_library_fn_read_line(size_t arg_count, clox_value_t *args,
                                      clox_native_result_t *result, clox_vm_t *vm) {
  (void)args;
  (void)arg_count;

  clox_allocator_t *alloc = vm->allocator;
  size_t capacity = READ_LINE_INITIAL_CAPACITY;
  char *chars = CLOX_ARRAY_ALLOCATE(alloc, char, capacity);
  size_t length = 0;

  for (int c = fgetc(stdin); c != EOF && c != '\n'; c = fgetc(stdin)) {
    if (c == '\0') {
      CLOX_ARRAY_FREE(alloc, char, chars, capacity);
      format_library_error(result, "input holds a NUL byte");
      return false;
    }

    if (length + 1 == capacity) { // room for the NUL is kept back
      size_t new_capacity = CLOX_ARRAY_GROW_SIZE(capacity);
      chars = CLOX_ARRAY_GROW(alloc, char, chars, capacity, new_capacity);
      capacity = new_capacity;
    }

    chars[length] = (char)c;
    length++;
  }
  chars[length] = '\0';

  // a moved string owns a buffer exactly one longer than its text
  chars = CLOX_ARRAY_GROW(alloc, char, chars, capacity, length + 1);

  result->value = CLOX_STRING_MOVE(alloc, chars, length);
  return true;
}

// The whole file at (path) as a buffer of (*length + 1) bytes from (alloc),
// which the caller comes to own. Returns what went wrong instead, and has freed
// what it took, when anything did.
static const char *read_whole_file(clox_allocator_t *alloc, const char *path, char **chars,
                                   size_t *length) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return "could not open file";
  }

  // the length is taken from the file itself rather than read up to: a file the
  // program cannot seek is not one it can read whole either
  if (fseek(file, 0, SEEK_END) != 0) {
    (void)fclose(file);
    return "could not read file";
  }
  long end = ftell(file);
  if (end < 0) {
    (void)fclose(file);
    return "could not read file";
  }
  if (fseek(file, 0, SEEK_SET) != 0) {
    (void)fclose(file);
    return "could not read file";
  }

  size_t size = (size_t)end;
  char *buffer = CLOX_ARRAY_ALLOCATE(alloc, char, size + 1);
  size_t read = fread(buffer, 1, size, file);
  (void)fclose(file);

  if (read != size) {
    CLOX_ARRAY_FREE(alloc, char, buffer, size + 1);
    return "could not read file";
  }
  // the buffer is (size + 1) long by construction; all the analyzer sees is
  // that size came in from outside the program
  // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
  buffer[size] = '\0';

  if (memchr(buffer, '\0', size) != NULL) {
    CLOX_ARRAY_FREE(alloc, char, buffer, size + 1);
    return "NUL byte in file";
  }

  *chars = buffer;
  *length = size;
  return NULL;
}

static bool clox_library_fn_read_file(size_t arg_count, clox_value_t *args,
                                      clox_native_result_t *result, clox_vm_t *vm) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  const char *path = CLOX_AS_CSTRING(args[0]);
  char *chars = NULL;
  size_t length = 0;

  const char *failure = read_whole_file(vm->allocator, path, &chars, &length);
  CHECK(failure == NULL, "%s \"%s\"", failure, path);

  result->value = CLOX_STRING_MOVE(vm->allocator, chars, length);
  return true;
}

clox_library_fn_t const clox_library_fns[] = {
#define X(fn_name, fn_arity)                                                                       \
  {.name = #fn_name, .arity = (fn_arity), .fn = clox_library_fn_##fn_name},
#include "library.def"
#undef X
};

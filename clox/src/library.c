#include "library.h"

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "object.h"
#include "value.h"

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

// One draw in [0, 1). rand() is the only generator ISO C guarantees, so it is
// what the library uses: random() and arc4random() are POSIX and BSD, and go
// undeclared under a strict -std=c17 on glibc. RAND_MAX + 1.0 is exact for
// every RAND_MAX a conforming implementation can pick.
static double random_double(void) {
  // the CLI and clangd report this one diagnostic under different alias names
  // NOLINTNEXTLINE(cert-msc30-c,cert-msc50-cpp,misc-predictable-rand,clang-analyzer-security.insecureAPI.rand)
  return (double)rand() / ((double)RAND_MAX + 1.0);
}

static bool clox_library_fn_clock(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result) {
  (void)args;
  (void)arg_count;

  result->value = CLOX_NUMBER((double)clock() / CLOCKS_PER_SEC);
  return true;
}

static bool clox_library_fn_sleep(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result) {
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
static bool clox_library_fn_time(size_t arg_count, clox_value_t *args,
                                 clox_native_result_t *result) {
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
static bool clox_library_fn_exit(size_t arg_count, clox_value_t *args,
                                 clox_native_result_t *result) {
  (void)args;
  (void)arg_count;

  format_library_error(result, "exited");
  return false;
}

static bool clox_library_fn_seed(size_t arg_count, clox_value_t *args,
                                 clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_INTEGER(args[0]), "first argument must be integer");

  double seed = CLOX_AS_NUMBER(args[0]);

  CHECK(seed >= 0 && seed <= UINT_MAX, "first argument out of range");

  srand((unsigned int)seed);

  result->value = CLOX_NIL;
  return true;
}

static bool clox_library_fn_random(size_t arg_count, clox_value_t *args,
                                   clox_native_result_t *result) {
  (void)args;
  (void)arg_count;

  result->value = CLOX_NUMBER(random_double());
  return true;
}

static bool clox_library_fn_random_int(size_t arg_count, clox_value_t *args,
                                       clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_INTEGER(args[0]), "first argument must be integer");

  double bound = CLOX_AS_NUMBER(args[0]);

  CHECK(bound >= 1, "first argument must be positive");

  // random_double() is strictly below 1, so the product stays below bound
  result->value = CLOX_NUMBER(floor(random_double() * bound));
  return true;
}

// No domain checks below: OP_DIVIDE already lets 1/0 through as an infinity,
// so the language's arithmetic is IEEE all the way down and sqrt(-1) has no
// business being the one operation that raises a runtime error instead.
static bool clox_library_fn_sqrt(size_t arg_count, clox_value_t *args,
                                 clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(sqrt(CLOX_AS_NUMBER(args[0])));
  return true;
}

static bool clox_library_fn_pow(size_t arg_count, clox_value_t *args,
                                clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");
  CHECK(CLOX_IS_NUMBER(args[1]), "second argument must be number");

  result->value = CLOX_NUMBER(pow(CLOX_AS_NUMBER(args[0]), CLOX_AS_NUMBER(args[1])));
  return true;
}

static bool clox_library_fn_abs(size_t arg_count, clox_value_t *args,
                                clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(fabs(CLOX_AS_NUMBER(args[0])));
  return true;
}

static bool clox_library_fn_floor(size_t arg_count, clox_value_t *args,
                                  clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(floor(CLOX_AS_NUMBER(args[0])));
  return true;
}

static bool clox_library_fn_ceil(size_t arg_count, clox_value_t *args,
                                 clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_NUMBER(args[0]), "first argument must be number");

  result->value = CLOX_NUMBER(ceil(CLOX_AS_NUMBER(args[0])));
  return true;
}

// fmin / fmax rather than a bare comparison: they return the non-NaN operand,
// so one NaN in the middle of the arguments cannot swallow the whole result.
static bool clox_library_fn_min(size_t arg_count, clox_value_t *args,
                                clox_native_result_t *result) {
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

static bool clox_library_fn_max(size_t arg_count, clox_value_t *args,
                                clox_native_result_t *result) {
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
static bool clox_library_fn_len(size_t arg_count, clox_value_t *args,
                                clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  result->value = CLOX_NUMBER((double)CLOX_AS_STRING(args[0])->length);
  return true;
}

static bool clox_library_fn_ord(size_t arg_count, clox_value_t *args,
                                clox_native_result_t *result) {
  (void)arg_count;
  CHECK(CLOX_IS_STRING(args[0]), "first argument must be string");

  const clox_string_t *string = CLOX_AS_STRING(args[0]);
  CHECK(string->length == 1, "first argument must be one byte long");

  // char may be signed: go through unsigned char so the result is 0..255
  result->value = CLOX_NUMBER((double)(unsigned char)string->chars[0]);
  return true;
}

// The type predicates stand in for a type() that would have to return a
// string, which a native cannot allocate.
static bool clox_library_fn_is_bool(size_t arg_count, clox_value_t *args,
                                    clox_native_result_t *result) {
  (void)arg_count;

  result->value = CLOX_BOOL(CLOX_IS_BOOL(args[0]));
  return true;
}

static bool clox_library_fn_is_nil(size_t arg_count, clox_value_t *args,
                                   clox_native_result_t *result) {
  (void)arg_count;

  result->value = CLOX_BOOL(CLOX_IS_NIL(args[0]));
  return true;
}

static bool clox_library_fn_is_number(size_t arg_count, clox_value_t *args,
                                      clox_native_result_t *result) {
  (void)arg_count;

  result->value = CLOX_BOOL(CLOX_IS_NUMBER(args[0]));
  return true;
}

static bool clox_library_fn_is_string(size_t arg_count, clox_value_t *args,
                                      clox_native_result_t *result) {
  (void)arg_count;

  result->value = CLOX_BOOL(CLOX_IS_STRING(args[0]));
  return true;
}

clox_library_fn_t const clox_library_fns[] = {
#define X(fn_name, fn_arity)                                                                       \
  {.name = #fn_name, .arity = (fn_arity), .fn = clox_library_fn_##fn_name},
#include "library.def"
#undef X
};

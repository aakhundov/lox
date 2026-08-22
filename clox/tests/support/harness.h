#ifndef CLOX_TEST_HARNESS_H
#define CLOX_TEST_HARNESS_H

#include <stddef.h>
#include <stdio.h>

#include "common.h"
#include "error.h"
#include "object.h"
#include "value.h"

#define CLOX_TEST_MESSAGE_SIZE 256
#define CLOX_TEST_MAX_PRINTED 16
#define CLOX_TEST_MAX_ERRORS 8

// Collects what a run printed. count keeps rising past CLOX_TEST_MAX_PRINTED
// so a test can tell "more than fits" from "exactly this many".
typedef struct {
  size_t count;
  clox_value_t values[CLOX_TEST_MAX_PRINTED];
} clox_test_printed_t;

// Collects what a run reported as errors. Messages are copied, since the
// handler only owns its message for the duration of the call.
typedef struct {
  size_t count;
  char messages[CLOX_TEST_MAX_ERRORS][CLOX_TEST_MESSAGE_SIZE];
  clox_pos_t positions[CLOX_TEST_MAX_ERRORS];
} clox_test_errors_t;

// matches clox_print_fn_t; ctx is a clox_test_printed_t
void clox_test_print_fn(clox_value_t val, void *ctx);

// matches clox_error_handler_t; ctx is a clox_test_errors_t
void clox_test_error_handler(clox_error_info_t error, void *ctx);

// Interns a NUL-terminated C string in the allocator's string table. Table
// keys are compared by identity, so equal content must yield the same key.
const clox_string_t *clox_test_intern(clox_allocator_t *alloc, const char *chars);

// Interns one distinct key per index, for tests needing more
// keys than are worth spelling out one by one.
const clox_string_t *clox_test_intern_indexed(clox_allocator_t *alloc, size_t index);

// Opens a stream that writes into buffer, and closes it leaving NUL-terminated
// text behind. Text longer than the buffer is truncated.
FILE *clox_test_open_buffer(char *buffer, size_t size);
const char *clox_test_close_buffer(FILE *stream, char *buffer, size_t size);

// Renders val the way the interpreter prints it, into buffer, and returns it.
const char *clox_test_value_string(char (*buffer)[CLOX_TEST_MESSAGE_SIZE], clox_value_t val);

// Renders val the way the interpreter reprs it -- as Lox source -- into
// buffer, and returns it. A repr longer than the buffer is truncated.
const char *clox_test_value_repr_string(char (*buffer)[CLOX_TEST_MESSAGE_SIZE], clox_value_t val);

// Renders both values into buffer as "expected <a>, got <b>", and returns it.
// A value longer than the buffer is truncated: this is a failure message, not
// a rendering of the value.
const char *clox_test_values_message(char (*buffer)[CLOX_TEST_MESSAGE_SIZE], clox_value_t expected,
                                     clox_value_t actual);

// Compares two values and prints both when they differ. The message is built
// by the call passed as utest's msg argument, so it costs nothing until a
// comparison actually fails. Requires <utest.h> at the use site.
#define CLOX_TEST_VALUES_EQ(utest_macro, expected, actual)                                         \
  do {                                                                                             \
    clox_value_t clox_test_expected = (expected);                                                  \
    clox_value_t clox_test_actual = (actual);                                                      \
    char clox_test_message[CLOX_TEST_MESSAGE_SIZE];                                                \
    utest_macro(                                                                                   \
        clox_value_equals(clox_test_expected, clox_test_actual),                                   \
        clox_test_values_message(&clox_test_message, clox_test_expected, clox_test_actual));       \
  } while (0)

#define EXPECT_VALUE_EQ(expected, actual) CLOX_TEST_VALUES_EQ(EXPECT_TRUE_MSG, expected, actual)
#define ASSERT_VALUE_EQ(expected, actual) CLOX_TEST_VALUES_EQ(ASSERT_TRUE_MSG, expected, actual)

#endif

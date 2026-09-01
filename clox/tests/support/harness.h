#ifndef CLOX_TEST_HARNESS_H
#define CLOX_TEST_HARNESS_H

#include <stddef.h>
#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "error.h"
#include "object.h"
#include "value.h"

// A build that collects at every allocation is the wrong ground for a test
// that says where the collection happens, or that sets a mark by hand and
// expects it to still be there after allocating: a sweep clears every mark it
// passes. Such a test skips itself rather than being compiled out, so the
// stress run names what it did not cover instead of running quietly without
// it. Requires <utest.h> at the use site, and works in a fixture's setup as
// well as in a test body -- utest runs no teardown for a test its setup
// skipped, so the skip belongs before anything is initialized.
#if CLOX_STRESS_GC
#define CLOX_TEST_SKIP_UNDER_STRESS()                                                              \
  UTEST_SKIP("collections here have to happen only where the test asks for them")
#else
#define CLOX_TEST_SKIP_UNDER_STRESS() ((void)0)
#endif

#define CLOX_TEST_MESSAGE_SIZE 256
#define CLOX_TEST_MAX_PRINTED 16
#define CLOX_TEST_MAX_ERRORS 8
#define CLOX_TEST_NAME_SIZE 64

// Collects what a run printed. One print can carry several values; they are
// collected flat, so count is a total of values and not of print calls. count
// keeps rising past CLOX_TEST_MAX_PRINTED so a test can tell "more than fits"
// from "exactly this many".
typedef struct clox_test_printed_t {
  size_t count;
  clox_value_t values[CLOX_TEST_MAX_PRINTED];
} clox_test_printed_t;

// One frame of the stack an error reports: where it happened, the name of the
// function it happened in, and the file name and source text that function was
// compiled from.
typedef struct clox_test_frame_t {
  clox_pos_t pos;
  char fn_name[CLOX_TEST_NAME_SIZE];
  char file_name[CLOX_TEST_NAME_SIZE];
  // kept by pointer rather than copied, so a test can compare it
  // against the very buffer it handed the compiler
  const char *source;
} clox_test_frame_t;

// Collects what a run reported as errors. Messages, function names and file
// names are copied, since the handler only owns what it is handed for the
// duration of the call. Frames come in the order the error carried them, innermost first,
// so stacks[i][0] is where error i happened and stack_sizes[i] is how far out
// it was traced.
typedef struct clox_test_errors_t {
  size_t count;
  char messages[CLOX_TEST_MAX_ERRORS][CLOX_TEST_MESSAGE_SIZE];
  size_t stack_sizes[CLOX_TEST_MAX_ERRORS];
  clox_test_frame_t stacks[CLOX_TEST_MAX_ERRORS][CLOX_MAX_ERROR_STACK_SIZE];
} clox_test_errors_t;

// matches clox_print_fn_t; ctx is a clox_test_printed_t
void clox_test_print_fn(const clox_value_t *vals, size_t n, void *ctx);

// matches clox_error_handler_t; ctx is a clox_test_errors_t
void clox_test_error_handler(const clox_error_info_t *error, void *ctx);

// Interns a NUL-terminated C string in the allocator's string table. Table
// keys are compared by identity, so equal content must yield the same key.
const clox_string_t *clox_test_intern(clox_allocator_t *alloc, const char *chars);

// Interns one distinct key per index, for tests needing more
// keys than are worth spelling out one by one.
const clox_string_t *clox_test_intern_indexed(clox_allocator_t *alloc, size_t index);

// Roots an object for as long as its allocator lives.
//
// A test that holds an object of its own is in a position no caller inside
// clox is ever in: the interpreter reaches everything it owns from the value
// stack, the globals, a call frame or a compile frame, and a test reaches its
// objects from a C local the collector cannot see. Under a build that collects
// at every allocation, such an object is swept the moment the test allocates
// again. Keeping it is the harness standing in for the root a real caller
// would have had, and it is only ever correct for that: an object clox handed
// back and is about to take again is clox's to root, and a keep there would
// hide the bug rather than fix the test.
//
// There is no matching release. The durable stack is freed with the allocator,
// and a fixture's allocator does not outlive its test.
void clox_test_keep(clox_allocator_t *alloc, const void *object);

// clox_test_intern and clox_test_intern_indexed, for the common case of a key
// the test goes on to hold. Interning alone hands back an object nothing roots,
// and the very next allocation -- the table's own storage, often enough -- is
// free to take it.
const clox_string_t *clox_test_intern_kept(clox_allocator_t *alloc, const char *chars);
const clox_string_t *clox_test_intern_indexed_kept(clox_allocator_t *alloc, size_t index);

// CLOX_STRING_COPY for a string the test then holds as a value: the same
// keeping, for the tests that work in clox_value_t rather than in objects.
clox_value_t clox_test_string_kept(clox_allocator_t *alloc, const char *chars, size_t length);

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

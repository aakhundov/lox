#include "harness.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "error.h"
#include "object.h"
#include "value.h"

#define MAX_INDEXED_KEY_LENGTH 32

const clox_string_t *clox_test_intern(clox_allocator_t *alloc, const char *chars) {
  return clox_string_copy(alloc, chars, strlen(chars));
}

const clox_string_t *clox_test_intern_indexed(clox_allocator_t *alloc, size_t index) {
  char chars[MAX_INDEXED_KEY_LENGTH + 1];
  int length = snprintf(chars, sizeof(chars), "key_%zu", index);
  assert(length > 0 && (size_t)length <= MAX_INDEXED_KEY_LENGTH);

  return clox_string_copy(alloc, chars, (size_t)length);
}

FILE *clox_test_open_buffer(char *buffer, size_t size) {
  assert(size > 1);
  // fmemopen writes its terminating NUL only when one still fits, so it is
  // given one byte less than the buffer holds and clox_test_close_buffer sets
  // the last byte. Text that fills the buffer exactly is unterminated otherwise.
  return fmemopen(buffer, size - 1, "w");
}

const char *clox_test_close_buffer(FILE *stream, char *buffer, size_t size) {
  (void)fclose(stream);
  buffer[size - 1] = '\0';

  return buffer;
}

const char *clox_test_value_string(char (*buffer)[CLOX_TEST_MESSAGE_SIZE], clox_value_t val) {
  FILE *stream = clox_test_open_buffer(*buffer, CLOX_TEST_MESSAGE_SIZE);
  if (stream == NULL) {
    return "<could not render the value>";
  }

  clox_value_fprintf(stream, val);

  return clox_test_close_buffer(stream, *buffer, CLOX_TEST_MESSAGE_SIZE);
}

const char *clox_test_value_repr_string(char (*buffer)[CLOX_TEST_MESSAGE_SIZE], clox_value_t val) {
  FILE *stream = clox_test_open_buffer(*buffer, CLOX_TEST_MESSAGE_SIZE);
  if (stream == NULL) {
    return "<could not render the value>";
  }

  clox_value_repr_fprintf(stream, val);

  return clox_test_close_buffer(stream, *buffer, CLOX_TEST_MESSAGE_SIZE);
}

const char *clox_test_values_message(char (*buffer)[CLOX_TEST_MESSAGE_SIZE], clox_value_t expected,
                                     clox_value_t actual) {
  FILE *stream = clox_test_open_buffer(*buffer, CLOX_TEST_MESSAGE_SIZE);
  if (stream == NULL) {
    return "<could not render the values>";
  }

  (void)fprintf(stream, "expected ");
  clox_value_fprintf(stream, expected);
  (void)fprintf(stream, ", got ");
  clox_value_fprintf(stream, actual);

  return clox_test_close_buffer(stream, *buffer, CLOX_TEST_MESSAGE_SIZE);
}

void clox_test_print_fn(const clox_value_t *vals, size_t n, void *ctx) {
  clox_test_printed_t *printed = ctx;
  for (size_t i = 0; i < n; i++) {
    if (printed->count < CLOX_TEST_MAX_PRINTED) {
      printed->values[printed->count] = vals[i];
    }
    printed->count++;
  }
}

void clox_test_error_handler(const clox_error_info_t *error, void *ctx) {
  clox_test_errors_t *errors = ctx;
  if (errors->count < CLOX_TEST_MAX_ERRORS) {
    // the message is only alive during this call
    (void)snprintf(errors->messages[errors->count], CLOX_TEST_MESSAGE_SIZE, "%s", error->message);

    assert(error->num_locations <= CLOX_MAX_ERROR_STACK_SIZE);
    errors->stack_sizes[errors->count] = error->num_locations;
    for (size_t i = 0; i < error->num_locations; i++) {
      clox_test_frame_t *frame = &errors->stacks[errors->count][i];
      frame->pos = error->positions[i];
      // a name belongs to a function object, which the run being
      // watched may outlive no longer than the allocator holding it
      (void)snprintf(frame->fn_name, CLOX_TEST_NAME_SIZE, "%s", error->function_names[i]);
      (void)snprintf(frame->file_name, CLOX_TEST_NAME_SIZE, "%s", error->file_names[i]);
      // a source is the caller's own buffer, which outlives the run
      frame->source = error->sources[i];
    }
  }
  errors->count++;
}

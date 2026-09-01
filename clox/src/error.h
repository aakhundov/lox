#ifndef CLOX_ERROR_H
#define CLOX_ERROR_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "common.h"

typedef enum clox_exit_code_t {
  CLOX_EX_OK = 0,
  CLOX_EX_USAGE = 64,
  CLOX_EX_DATAERR = 65,
  CLOX_EX_NOINPUT = 66,
  CLOX_EX_SOFTWARE = 70,
  CLOX_EX_OSERR = 71,
  CLOX_EX_IOERR = 74,
} clox_exit_code_t;

#define CLOX_MAX_ERROR_STACK_SIZE 16

typedef struct clox_error_info_t {
  // message is guaranteed to be alive
  // only during the callback call
  const char *message;
  size_t num_locations;
  clox_pos_t positions[CLOX_MAX_ERROR_STACK_SIZE];
  const char *function_names[CLOX_MAX_ERROR_STACK_SIZE];
  const char *file_names[CLOX_MAX_ERROR_STACK_SIZE];
  const char *sources[CLOX_MAX_ERROR_STACK_SIZE];
} clox_error_info_t;

typedef void clox_error_handler_t(const clox_error_info_t *error, void *ctx);

#define MAX_ERROR_LENGTH 1024

#define CLOX_FATAL_ERROR(msg, code) clox_fatal_error((msg), (code), __FILE__, __LINE__)

_Noreturn void clox_fatal_error(const char *message, clox_exit_code_t code, const char *file,
                                int line);

static inline __attribute__((format(printf, 2, 0))) int
clox_format_error(char (*buffer)[MAX_ERROR_LENGTH + 1], const char *fmt, va_list ap) {
  int len = vsnprintf(*buffer, sizeof(*buffer), fmt, ap);
  if (len > MAX_ERROR_LENGTH) {
    // truncate incomplete error message with with ...
    memset(*buffer + (MAX_ERROR_LENGTH - 3), '.', 3);
    len = MAX_ERROR_LENGTH;
  }
  return len;
}

#endif

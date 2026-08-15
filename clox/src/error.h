#ifndef CLOX_ERROR_H
#define CLOX_ERROR_H

#include "common.h"

typedef enum {
  CLOX_EX_OK = 0,
  CLOX_EX_USAGE = 64,
  CLOX_EX_DATAERR = 65,
  CLOX_EX_NOINPUT = 66,
  CLOX_EX_SOFTWARE = 70,
  CLOX_EX_OSERR = 71,
  CLOX_EX_IOERR = 74,
} clox_exit_code_t;

typedef struct {
  const char *message;
  clox_pos_t pos;
} clox_error_info_t;

#define CLOX_FATAL_ERROR(msg, code) clox_fatal_error((msg), (code), __FILE__, __LINE__)

_Noreturn void clox_fatal_error(const char *message, clox_exit_code_t code, const char *file,
                                int line);

#endif

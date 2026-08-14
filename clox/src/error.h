#ifndef CLOX_ERROR_H
#define CLOX_ERROR_H

#include "common.h"

typedef struct {
  const char *message;
  clox_pos_t pos;
} clox_error_info_t;

#define CLOX_FATAL_ERROR(msg, code) clox_fatal_error((msg), (code), __FILE__, __LINE__)

_Noreturn void clox_fatal_error(const char *message, int code, const char *file, int line);

#endif

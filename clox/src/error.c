#include "error.h"

#include <stdio.h>
#include <stdlib.h>

_Noreturn void clox_fatal_error(const char *message, clox_exit_code_t code, const char *file,
                                int line) {
  (void)fprintf(stderr, "Fatal error (at %s:%d): %s\n", file, line, message);
  exit((int)code);
}

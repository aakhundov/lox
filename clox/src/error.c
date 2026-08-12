#include "error.h"

#include <stdio.h>
#include <stdlib.h>

_Noreturn void clox_fatal_error(const char *message, int code, const char *file, int line) {
  // NOLINTNEXTLINE(cert-err33-c)
  fprintf(stderr, "Fatal error (at %s:%d): %s\n", file, line, message);
  exit(code);
}

#include "error.h"

#include <stdio.h>
#include <stdlib.h>

_Noreturn void clox_fatal_error(const char *message, const char *file, int line) {
  // NOLINTNEXTLINE(cert-err33-c)
  fprintf(stderr, "Fatal error (at %s:%d): %s\n", file, line, message);
  exit(1);
}

#ifndef CLOX_LIBRARY_H
#define CLOX_LIBRARY_H

#include <stddef.h>

#include "object.h"

enum {
  CLOX_LIBRARY_SIZE = 0
#define X(name, arity) +1 // NOLINT(bugprone-macro-parentheses)
#include "library.def"
#undef X
};

typedef struct {
  const char *name;
  size_t arity;
  clox_native_fn_t *fn;
} clox_library_fn_t;

extern const clox_library_fn_t clox_library_fns[CLOX_LIBRARY_SIZE];

#endif

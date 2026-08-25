#ifndef CLOX_LIBRARY_H
#define CLOX_LIBRARY_H

#include "object.h"

enum {
  CLOX_LIBRARY_SIZE = 0
#define X(name) +1 // NOLINT(bugprone-macro-parentheses)
#include "natives.def"
#undef X
};

extern const char *clox_library_fn_names[CLOX_LIBRARY_SIZE];
extern clox_native_fn_t *clox_library_fns[CLOX_LIBRARY_SIZE];

#endif

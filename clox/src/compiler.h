#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include <stdbool.h>

#include "chunk.h"
#include "error.h"

// modifies source internally, but guarantees identical content on return
bool clox_compile(char *source, clox_chunk_t *chunk, clox_error_handler_t *error_handler,
                  void *error_ctx);

#endif

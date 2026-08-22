#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#include "chunk.h"
#include "error.h"
#include "object.h"
#include "scanner.h"

#define CLOX_MAX_LOCALS (UCHAR_MAX + 1)

typedef struct {
  clox_token_t name;
  size_t depth;
  bool initialized;
} clox_local_t;

typedef struct {
  // output
  clox_chunk_t *chunk;
  // parser
  clox_scanner_t scanner;
  clox_token_t previous;
  clox_token_t current;
  bool had_error;
  bool panic_mode;
  size_t parser_depth;
  // locals
  clox_local_t locals[CLOX_MAX_LOCALS];
  size_t local_count;
  size_t scope_depth;
  // string allocator
  clox_allocator_t *allocator;
  // error handling
  clox_error_handler_t *error_handler;
  void *error_ctx;
} clox_compiler_t;

void clox_compiler_init(clox_compiler_t *compiler, clox_allocator_t *alloc);
void clox_compiler_free(clox_compiler_t *compiler);
void clox_compiler_set_error_handler(clox_compiler_t *compiler, clox_error_handler_t *error_handler,
                                     void *error_ctx);
void clox_compiler_reset_error_handler(clox_compiler_t *compiler);

// modifies source internally, but guarantees identical content on return
bool clox_compile(clox_compiler_t *compiler, char *source, clox_chunk_t *chunk);

#endif

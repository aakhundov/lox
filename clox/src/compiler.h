#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#include "error.h"
#include "object.h"
#include "scanner.h"

#define CLOX_MAX_ARITY UCHAR_MAX
#define CLOX_MAX_LOCALS (UCHAR_MAX + 1)

typedef struct {
  clox_token_t name;
  size_t depth;
  bool initialized;
} clox_local_t;

typedef struct {
  bool inside;
  size_t scope;
  size_t start;
  size_t exit_patch;
} clox_loop_state_t;

typedef enum {
  FUNCTION_SCRIPT,
  FUNCTION_FUNCTION,
} clox_function_type_t;

typedef struct clox_compile_frame_t {
  // locals
  clox_local_t locals[CLOX_MAX_LOCALS];
  size_t local_count;
  size_t scope_depth;
  // loops
  clox_loop_state_t loop;
  // function
  clox_function_t *function;
  clox_function_type_t type;
  // linked list of frames
  struct clox_compile_frame_t *enclosing;
} clox_compile_frame_t;

typedef struct {
  // parser
  clox_scanner_t scanner;
  clox_token_t previous;
  clox_token_t current;
  bool had_error;
  bool panic_mode;
  size_t parser_depth;
  size_t declaration_depth;
  // current frame
  clox_compile_frame_t *frame;
  // compile-time allocator
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
bool clox_compile(clox_compiler_t *compiler, char *source, clox_function_t **function);

#endif

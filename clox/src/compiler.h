#ifndef CLOX_COMPILER_H
#define CLOX_COMPILER_H

#include "chunk.h"
#include "error.h"

typedef enum {
  CLOX_COMPILE_OK,
} clox_compile_status_t;

typedef struct {
  clox_compile_status_t status;
  clox_error_info_t error;
} clox_compile_result_t;

clox_compile_result_t clox_compile(const char *source, clox_chunk_t *chunk);

#endif

#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include <stdbool.h>
#include <stddef.h>

#include "common.h"
#include "value.h"

typedef enum {
#define X(name) OP_##name,
#include "opcodes.def"
#undef X
  OP_CODE_COUNT,
} clox_op_code_t;

extern const char *const clox_op_code_names[];

typedef unsigned char clox_byte_t;

typedef struct {
  size_t length;
  size_t capacity;
  clox_byte_t *code;
  clox_pos_t *positions;
  clox_value_array_t constants;
} clox_chunk_t;

void clox_chunk_init(clox_chunk_t *chunk);
void clox_chunk_write(clox_chunk_t *chunk, clox_byte_t byte, clox_pos_t pos);
void clox_chunk_free(clox_chunk_t *chunk);

bool clox_write_constant(clox_chunk_t *chunk, clox_value_t val, clox_pos_t pos);
clox_value_t clox_read_constant(const clox_chunk_t *chunk, clox_op_code_t opcode,
                                const clox_byte_t **ipp);

#endif

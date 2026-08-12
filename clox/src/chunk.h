#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include <stddef.h>

#include "value.h"

typedef enum {
#define X(opcode) OP_##opcode,
#include "opcodes.def"
#undef X
  OP_CODE_COUNT,
} clox_op_code_t;

extern const char *const clox_op_code_names[];

typedef unsigned char clox_byte_t;

typedef struct {
  unsigned int line;
  unsigned int column;
} clox_pos_t;

typedef struct {
  size_t length;
  size_t capacity;
  clox_byte_t *code;
  clox_pos_t *positions;
  clox_value_array_t constants;
} clox_chunk_t;

void clox_init_chunk(clox_chunk_t *chunk);
void clox_write_chunk(clox_chunk_t *chunk, clox_byte_t byte, clox_pos_t pos);
void clox_free_chunk(clox_chunk_t *chunk);

size_t clox_add_constant(clox_chunk_t *chunk, clox_value_t value);
size_t clox_write_constant(clox_chunk_t *chunk, clox_value_t value, clox_pos_t pos);
size_t clox_read_constant(const clox_chunk_t *chunk, size_t offset, clox_value_t *value);

#endif

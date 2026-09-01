#ifndef CLOX_CHUNK_H
#define CLOX_CHUNK_H

#include <stdbool.h>
#include <stddef.h>

#include "common.h"
#include "table.h"
#include "value.h"

typedef enum clox_op_code_t {
#define X(name) OP_##name,
#define XC(short, long) X(short) X(long)
#include "opcodes.def"
#undef XC
#undef X
  OP_CODE_COUNT,
} clox_op_code_t;

enum {
  CONST_OP_CODE_COUNT = 0
#define X(name)
#define XC(short, long) +2 // NOLINT(bugprone-macro-parentheses)
#include "opcodes.def"
#undef XC
#undef X
};

extern const char *const clox_op_code_names[];

typedef struct clox_allocator_t clox_allocator_t;

typedef struct clox_chunk_t {
  size_t length;
  size_t capacity;
  clox_byte_t *code;
  clox_pos_t *positions;
  clox_value_array_t constants;
  clox_table_t string_constants;
  clox_allocator_t *allocator;
} clox_chunk_t;

void clox_chunk_init(clox_chunk_t *chunk, clox_allocator_t *alloc);
void clox_chunk_write(clox_chunk_t *chunk, clox_byte_t byte, clox_pos_t pos);
void clox_chunk_free(clox_chunk_t *chunk);

// writes either the opcode or its long version + the constant
// index depending on whether the latest constant index size is
// short or long; the passed opcode must be the short version
bool clox_write_constant(clox_chunk_t *chunk, clox_op_code_t opcode, clox_value_t val,
                         clox_pos_t pos);

// reads the constant dependning on the passed opcode being
// long or short; ipp is assumed to point at the constant value:
// the opcode is already read
clox_value_t clox_read_constant(const clox_chunk_t *chunk, clox_op_code_t opcode,
                                const clox_byte_t **ipp);

#endif

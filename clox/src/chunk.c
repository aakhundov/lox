#include "chunk.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>

#include "common.h"
#include "error.h"
#include "memory.h"
#include "value.h"

const char *const clox_op_code_names[] = {
#define X(opcode) "OP_" #opcode,
#include "opcodes.def"
#undef X
};

_Static_assert(CLOX_ARRAY_SIZE(clox_op_code_names) == OP_CODE_COUNT,
               "op code names array size mismatch");

void clox_init_chunk(clox_chunk_t *chunk) {
  chunk->code = NULL;
  chunk->positions = NULL;
  chunk->capacity = 0;
  chunk->length = 0;

  clox_init_value_array(&chunk->constants);
}

void clox_write_chunk(clox_chunk_t *chunk, clox_byte_t byte, clox_pos_t pos) {
  if (chunk->length == chunk->capacity) {
    size_t new_capacity = CLOX_GROW_SIZE(chunk->capacity);
    chunk->code = CLOX_GROW_ARRAY(clox_byte_t, chunk->code, chunk->capacity, new_capacity);
    chunk->positions = CLOX_GROW_ARRAY(clox_pos_t, chunk->positions, chunk->capacity, new_capacity);
    chunk->capacity = new_capacity;
  }

  chunk->code[chunk->length] = byte;
  chunk->positions[chunk->length] = pos;
  chunk->length++;
}

void clox_free_chunk(clox_chunk_t *chunk) {
  CLOX_FREE_ARRAY(clox_byte_t, chunk->code, chunk->capacity);
  CLOX_FREE_ARRAY(clox_pos_t, chunk->positions, chunk->capacity);

  chunk->code = NULL;
  chunk->positions = NULL;
  chunk->capacity = 0;
  chunk->length = 0;

  clox_free_value_array(&chunk->constants);
}

size_t clox_add_constant(clox_chunk_t *chunk, clox_value_t value) {
  size_t index = chunk->constants.length; // where new value will land
  clox_write_value_array(&chunk->constants, value);
  return index;
}

_Static_assert(sizeof(size_t) >= 3, "sizeof(size_t) < 3");

#define THREE_BYTE_MAX                                                                             \
  (((size_t)UCHAR_MAX << (2 * CHAR_BIT)) | ((size_t)UCHAR_MAX << CHAR_BIT) | UCHAR_MAX)

size_t clox_write_constant(clox_chunk_t *chunk, clox_value_t value, clox_pos_t pos) {
  size_t index = clox_add_constant(chunk, value);

  if (index <= UCHAR_MAX) {
    // 1-byte index
    clox_write_chunk(chunk, OP_CONSTANT, pos);
    clox_write_chunk(chunk, (clox_byte_t)index, pos);
  } else if (index <= THREE_BYTE_MAX) {
    // 3-byte index
    clox_write_chunk(chunk, OP_CONSTANT_LONG, pos);
    clox_write_chunk(chunk, (clox_byte_t)(index >> (2 * CHAR_BIT)), pos);
    clox_write_chunk(chunk, (clox_byte_t)(index >> CHAR_BIT), pos);
    clox_write_chunk(chunk, (clox_byte_t)index, pos);
  } else {
    CLOX_FATAL_ERROR("constant limit exceeded");
  }

  return index;
}

size_t clox_read_constant(const clox_chunk_t *chunk, size_t offset, clox_value_t *value) {
  assert(offset < chunk->length);
  clox_byte_t byte = chunk->code[offset++];
  clox_op_code_t opcode = (clox_op_code_t)byte;
  assert(opcode == OP_CONSTANT || opcode == OP_CONSTANT_LONG);

  size_t index = 0;
  if (opcode == OP_CONSTANT) {
    // 1-byte index
    assert(offset < chunk->length);
    index = chunk->code[offset++];
  } else {
    // 3-byte index
    assert(chunk->length >= 3 && offset <= chunk->length - 3);
    index |= ((size_t)chunk->code[offset++] << (2 * CHAR_BIT));
    index |= ((size_t)chunk->code[offset++] << CHAR_BIT);
    index |= chunk->code[offset++];
  }

  assert(index < chunk->constants.length);
  *value = chunk->constants.values[index];
  return offset; // incremented above
}

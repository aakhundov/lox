#include "chunk.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "memory.h"
#include "value.h"

const char *const clox_op_code_names[] = {
#define X(name) [OP_##name] = "OP_" #name,
#include "opcodes.def"
#undef X
};

_Static_assert(CLOX_ARRAY_SIZE(clox_op_code_names) == OP_CODE_COUNT,
               "op code names array size mismatch");

static size_t add_constant(clox_chunk_t *chunk, clox_value_t val) {
  size_t index = chunk->constants.length; // where new value will land
  clox_value_array_write(&chunk->constants, val);
  return index;
}

static clox_value_t pop_constant(clox_chunk_t *chunk) {
  return clox_value_array_pop(&chunk->constants);
}

void clox_chunk_init(clox_chunk_t *chunk) {
  chunk->code = NULL;
  chunk->positions = NULL;
  chunk->capacity = 0;
  chunk->length = 0;

  clox_value_array_init(&chunk->constants);
}

void clox_chunk_write(clox_chunk_t *chunk, clox_byte_t byte, clox_pos_t pos) {
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

void clox_chunk_free(clox_chunk_t *chunk) {
  CLOX_FREE_ARRAY(clox_byte_t, chunk->code, chunk->capacity);
  CLOX_FREE_ARRAY(clox_pos_t, chunk->positions, chunk->capacity);

  chunk->code = NULL;
  chunk->positions = NULL;
  chunk->capacity = 0;
  chunk->length = 0;

  clox_value_array_free(&chunk->constants);
}

// at least three bytes of long constant index in size_t
_Static_assert(sizeof(size_t) >= 3, "sizeof(size_t) < 3");

#define THREE_BYTE_MAX                                                                             \
  (((size_t)UCHAR_MAX << (2 * CHAR_BIT)) | ((size_t)UCHAR_MAX << CHAR_BIT) | UCHAR_MAX)

bool clox_write_constant(clox_chunk_t *chunk, clox_value_t val, clox_pos_t pos) {
  size_t index = add_constant(chunk, val);

  if (index <= UCHAR_MAX) {
    // 1-byte index
    clox_chunk_write(chunk, OP_CONSTANT, pos);
    clox_chunk_write(chunk, (clox_byte_t)index, pos);
    return true;
  }
  if (index <= THREE_BYTE_MAX) {
    // 3-byte index
    clox_chunk_write(chunk, OP_CONSTANT_LONG, pos);
    clox_chunk_write(chunk, (clox_byte_t)(index >> (2 * CHAR_BIT)), pos);
    clox_chunk_write(chunk, (clox_byte_t)(index >> CHAR_BIT), pos);
    clox_chunk_write(chunk, (clox_byte_t)index, pos);
    return true;
  }

  // failure: constant limit exceeded
  pop_constant(chunk); // leave chunk intact
  return false;
}

clox_value_t clox_read_constant(const clox_chunk_t *chunk, clox_op_code_t opcode,
                                const clox_byte_t **ipp) {
  // ptr to constant index's first byte
  const clox_byte_t *ip = *ipp;
  // the index pointer is after the chunk's code
  assert((uintptr_t)ip >= (uintptr_t)chunk->code);
  // the index pointer belongs to the chunk's code
  assert((uintptr_t)ip < (uintptr_t)chunk->code + chunk->length);
  // cast is safe due to the assertions above
  size_t offset = (size_t)(ip - chunk->code);
  (void)offset; // used only in asserts

  assert(opcode == OP_CONSTANT || opcode == OP_CONSTANT_LONG);

  size_t index = 0;
  if (opcode == OP_CONSTANT) {
    // 1-byte index
    assert(offset < chunk->length);
    index = *ip++;
  } else {
    // 3-byte index
    assert(chunk->length >= 3 && offset < chunk->length - 2);
    index |= ((size_t)*ip++ << (2 * CHAR_BIT));
    index |= ((size_t)*ip++ << CHAR_BIT);
    index |= *ip++;
  }

  *ipp = ip; // save the byte move
  assert(index < chunk->constants.length);
  return chunk->constants.values[index];
}

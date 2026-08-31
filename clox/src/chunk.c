#include "chunk.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define X(name)
#define XC(short, long)                                                                            \
  _Static_assert(OP_##short < CONST_OP_CODE_COUNT, "");                                            \
  _Static_assert(OP_##long < CONST_OP_CODE_COUNT, "");                                             \
  _Static_assert(OP_##short + 1 == OP_##long, "");
#include "opcodes.def"
#undef XC
#undef X

const char *const clox_op_code_names[] = {
#define X(name) [OP_##name] = "OP_" #name,
#define XC(short, long) X(short) X(long)
#include "opcodes.def"
#undef XC
#undef X
};

_Static_assert(OP_CODE_COUNT <= UCHAR_MAX + 1, "opcodes must be single-byte");
_Static_assert(CLOX_ARRAY_SIZE(clox_op_code_names) == OP_CODE_COUNT,
               "op code names array size mismatch");

static inline size_t add_constant(clox_chunk_t *c, clox_value_t val, bool *cached) {
  if (CLOX_IS_STRING(val)) {
    clox_value_t cached_index;
    if (clox_table_get(&c->string_constants, CLOX_AS_STRING(val), &cached_index)) {
      // return the old cached index
      *cached = true;
      return CLOX_AS_SIZE(cached_index);
    }
  }

  size_t index = c->constants.length; // where new value will land
  clox_value_array_write(&c->constants, val);

  if (CLOX_IS_STRING(val)) {
    // add the new index to the cache
    clox_table_set(&c->string_constants, CLOX_AS_STRING(val), CLOX_SIZE(index));
  }

  *cached = false;
  return index;
}

static inline clox_value_t pop_constant(clox_chunk_t *c) {
  clox_value_t val = clox_value_array_pop(&c->constants);

  if (CLOX_IS_STRING(val)) {
    size_t val_index = c->constants.length; // where the value was

    clox_value_t cached_index;
    bool found = clox_table_get(&c->string_constants, CLOX_AS_STRING(val), &cached_index);
    assert(found); // the string constant was added before => must exist in the cache
    (void)found;   // used only in asserts

    if (CLOX_AS_SIZE(cached_index) == val_index) {
      // the value was the string value, its index was cached
      // there are no more values with this content in constants
      // delete the stale index from the cache
      clox_table_delete(&c->string_constants, CLOX_AS_STRING(val));
    }
  }

  return val;
}

void clox_chunk_init(clox_chunk_t *chunk, clox_allocator_t *alloc) {
  chunk->code = NULL;
  chunk->positions = NULL;
  chunk->capacity = 0;
  chunk->length = 0;
  chunk->allocator = alloc;

  clox_value_array_init(&chunk->constants, alloc);
  clox_table_init(&chunk->string_constants, alloc);
}

void clox_chunk_write(clox_chunk_t *chunk, clox_byte_t byte, clox_pos_t pos) {
  if (chunk->length == chunk->capacity) {
    size_t new_capacity = CLOX_ARRAY_GROW_SIZE(chunk->capacity);
    chunk->code =
        CLOX_ARRAY_GROW(chunk->allocator, clox_byte_t, chunk->code, chunk->capacity, new_capacity);
    chunk->positions = CLOX_ARRAY_GROW(chunk->allocator, clox_pos_t, chunk->positions,
                                       chunk->capacity, new_capacity);
    chunk->capacity = new_capacity;
  }

  chunk->code[chunk->length] = byte;
  chunk->positions[chunk->length] = pos;
  chunk->length++;
}

void clox_chunk_free(clox_chunk_t *chunk) {
  CLOX_ARRAY_FREE(chunk->allocator, clox_byte_t, chunk->code, chunk->capacity);
  CLOX_ARRAY_FREE(chunk->allocator, clox_pos_t, chunk->positions, chunk->capacity);

  chunk->code = NULL;
  chunk->positions = NULL;
  chunk->capacity = 0;
  chunk->length = 0;
  chunk->allocator = NULL;

  clox_value_array_free(&chunk->constants);
  clox_table_free(&chunk->string_constants);
}

// at least three bytes of long constant index in size_t
_Static_assert(sizeof(size_t) >= 3, "sizeof(size_t) < 3");

#define THREE_BYTE_MAX                                                                             \
  (((size_t)UCHAR_MAX << (2 * CHAR_BIT)) | ((size_t)UCHAR_MAX << CHAR_BIT) | UCHAR_MAX)

bool clox_write_constant(clox_chunk_t *chunk, clox_op_code_t opcode, clox_value_t val,
                         clox_pos_t pos) {
  // short (even) constant opcode
  assert(opcode < CONST_OP_CODE_COUNT && opcode % 2 == 0);

  bool index_was_cached;
  size_t index = add_constant(chunk, val, &index_was_cached);

  if (index <= UCHAR_MAX) {
    // 1-byte index
    clox_chunk_write(chunk, (clox_byte_t)opcode, pos); // short opcode
    clox_chunk_write(chunk, (clox_byte_t)index, pos);
    return true;
  }
  if (index <= THREE_BYTE_MAX) {
    // 3-byte index
    clox_chunk_write(chunk, (clox_byte_t)(opcode + 1), pos); // long opcode
    clox_chunk_write(chunk, (clox_byte_t)(index >> (2 * CHAR_BIT)), pos);
    clox_chunk_write(chunk, (clox_byte_t)(index >> CHAR_BIT), pos);
    clox_chunk_write(chunk, (clox_byte_t)index, pos);
    return true;
  }

  // failure: constant limit exceeded
  if (!index_was_cached) {
    pop_constant(chunk); // leave chunk intact
  }
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

  // short (even) or long (odd) constant opcode
  assert(opcode < CONST_OP_CODE_COUNT);

  size_t index = 0;
  if (opcode % 2 == 0) { // short opcode
    // 1-byte index
    assert(offset < chunk->length);
    index = *ip++;
  } else { // long opcode
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

#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "value.h"

#include "support/harness.h"

#define MANY_BYTES 64
// one more than a single-byte constant index can address
#define OVER_BYTE_INDEX 256

static const clox_pos_t POS = {.line = 1, .col = 1};

struct chunk {
  clox_chunk_t chunk;
};

UTEST_F_SETUP(chunk) {
  clox_chunk_init(&utest_fixture->chunk);
}

UTEST_F_TEARDOWN(chunk) {
  clox_chunk_free(&utest_fixture->chunk);
}

UTEST_F(chunk, starts_empty) {
  EXPECT_EQ((size_t)0, utest_fixture->chunk.length);
  EXPECT_EQ((size_t)0, utest_fixture->chunk.constants.length);
}

UTEST_F(chunk, write_appends_bytes_in_order) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  clox_chunk_write(chunk, OP_NIL, POS);
  clox_chunk_write(chunk, OP_NOT, POS);
  clox_chunk_write(chunk, OP_RETURN, POS);

  ASSERT_EQ((size_t)3, chunk->length);
  EXPECT_EQ(OP_NIL, chunk->code[0]);
  EXPECT_EQ(OP_NOT, chunk->code[1]);
  EXPECT_EQ(OP_RETURN, chunk->code[2]);
}

UTEST_F(chunk, write_keeps_one_position_per_byte) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  clox_chunk_write(chunk, OP_NIL, (clox_pos_t){.line = 1, .col = 2});
  clox_chunk_write(chunk, OP_RETURN, (clox_pos_t){.line = 30, .col = 4});

  ASSERT_EQ((size_t)2, chunk->length);
  EXPECT_EQ((size_t)1, chunk->positions[0].line);
  EXPECT_EQ((size_t)2, chunk->positions[0].col);
  EXPECT_EQ((size_t)30, chunk->positions[1].line);
  EXPECT_EQ((size_t)4, chunk->positions[1].col);
}

UTEST_F(chunk, many_writes_all_survive_the_growth) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  for (size_t i = 0; i < MANY_BYTES; i++) {
    clox_chunk_write(chunk, (clox_byte_t)i, (clox_pos_t){.line = i + 1, .col = 1});
  }

  ASSERT_EQ((size_t)MANY_BYTES, chunk->length);
  for (size_t i = 0; i < MANY_BYTES; i++) {
    ASSERT_EQ((clox_byte_t)i, chunk->code[i]);
    ASSERT_EQ(i + 1, chunk->positions[i].line);
  }
}

UTEST_F(chunk, free_empties_it_and_leaves_it_reusable) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  clox_chunk_write(chunk, OP_RETURN, POS);
  ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER(1.0), POS));

  clox_chunk_free(chunk);
  ASSERT_EQ((size_t)0, chunk->length);
  ASSERT_EQ((size_t)0, chunk->constants.length);

  clox_chunk_write(chunk, OP_NIL, POS);
  EXPECT_EQ((size_t)1, chunk->length);
  EXPECT_EQ(OP_NIL, chunk->code[0]);
}

UTEST_F(chunk, a_constant_is_read_back_as_it_was_written) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER(42.0), POS));
  EXPECT_EQ(OP_CONSTANT, chunk->code[offset]);

  const clox_byte_t *ip = chunk->code + offset + 1;
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), clox_read_constant(chunk, chunk->code[offset], &ip));
  EXPECT_EQ(offset + 2, (size_t)(ip - chunk->code));
}

UTEST_F(chunk, a_constant_carries_the_position_it_was_written_with) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER(1.0), (clox_pos_t){.line = 7, .col = 9}));

  ASSERT_TRUE(chunk->length > 0);
  for (size_t i = 0; i < chunk->length; i++) {
    ASSERT_EQ((size_t)7, chunk->positions[i].line);
    ASSERT_EQ((size_t)9, chunk->positions[i].col);
  }
}

UTEST_F(chunk, a_constant_past_the_single_byte_range_takes_the_long_form) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER((double)i), POS));
  }

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, CLOX_BOOL(true), POS));
  EXPECT_EQ(OP_CONSTANT_LONG, chunk->code[offset]);

  const clox_byte_t *ip = chunk->code + offset + 1;
  EXPECT_VALUE_EQ(CLOX_BOOL(true), clox_read_constant(chunk, chunk->code[offset], &ip));
  EXPECT_EQ(offset + 4, (size_t)(ip - chunk->code));
}

UTEST_F(chunk, constants_on_both_sides_of_the_boundary_stay_readable) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  size_t offsets[OVER_BYTE_INDEX + 2];

  for (size_t i = 0; i < OVER_BYTE_INDEX + 2; i++) {
    offsets[i] = chunk->length;
    ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER((double)i), POS));
  }

  for (size_t i = 0; i < OVER_BYTE_INDEX + 2; i++) {
    const clox_byte_t *ip = chunk->code + offsets[i] + 1;
    ASSERT_VALUE_EQ(CLOX_NUMBER((double)i),
                    clox_read_constant(chunk, chunk->code[offsets[i]], &ip));
  }
}

UTEST(opcodes, every_opcode_has_a_name) {
  for (size_t opcode = 0; opcode < OP_CODE_COUNT; opcode++) {
    ASSERT_TRUE(clox_op_code_names[opcode] != NULL);
    ASSERT_TRUE(strncmp(clox_op_code_names[opcode], "OP_", 3) == 0);
    ASSERT_TRUE(strlen(clox_op_code_names[opcode]) > 3);
  }
}

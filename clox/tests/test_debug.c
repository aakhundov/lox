#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "value.h"

#include "support/harness.h"

#define TEXT_SIZE 4096
// one more than a single-byte constant index can address
#define OVER_BYTE_INDEX 256

static const clox_pos_t POS = {.line = 1, .col = 1};

struct debug {
  clox_chunk_t chunk;
  char text[TEXT_SIZE];
};

UTEST_F_SETUP(debug) {
  clox_chunk_init(&utest_fixture->chunk);
}

UTEST_F_TEARDOWN(debug) {
  clox_chunk_free(&utest_fixture->chunk);
}

// Disassembles one instruction into the fixture's buffer, and returns the
// offset the disassembler moved to.
static size_t disassemble_one(struct debug *fixture, size_t offset) {
  FILE *stream = clox_test_open_buffer(fixture->text, TEXT_SIZE);
  size_t next = clox_disassemble_instruction_fprintf(stream, &fixture->chunk, offset);
  (void)clox_test_close_buffer(stream, fixture->text, TEXT_SIZE);

  return next;
}

UTEST_F(debug, an_instruction_without_operands_advances_by_one_byte) {
  clox_chunk_write(&utest_fixture->chunk, OP_RETURN, POS);

  EXPECT_EQ((size_t)1, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_RETURN") != NULL);
}

UTEST_F(debug, a_constant_instruction_advances_past_its_index) {
  ASSERT_TRUE(clox_write_constant(&utest_fixture->chunk, CLOX_NUMBER(42.0), POS));

  EXPECT_EQ((size_t)2, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_CONSTANT") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "42") != NULL);
}

UTEST_F(debug, a_long_constant_instruction_advances_past_its_wider_index) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER((double)i), POS));
  }

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER(-1.0), POS));

  EXPECT_EQ(offset + 4, disassemble_one(utest_fixture, offset));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_CONSTANT_LONG") != NULL);
}

UTEST_F(debug, walking_a_chunk_lands_exactly_on_its_end) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER(1.0), POS));
  clox_chunk_write(chunk, OP_NEGATE, POS);
  ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER(2.0), POS));
  clox_chunk_write(chunk, OP_ADD, POS);
  clox_chunk_write(chunk, OP_RETURN, POS);

  size_t offset = 0;
  size_t instructions = 0;
  while (offset < chunk->length) {
    size_t next = disassemble_one(utest_fixture, offset);
    ASSERT_TRUE(next > offset); // no instruction may stand still
    offset = next;
    instructions++;
  }

  EXPECT_EQ(chunk->length, offset);
  EXPECT_EQ((size_t)5, instructions);
}

UTEST_F(debug, every_opcode_without_operands_disassembles_under_its_own_name) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  for (size_t opcode = 0; opcode < OP_CODE_COUNT; opcode++) {
    if (opcode == OP_CONSTANT || opcode == OP_CONSTANT_LONG) {
      continue; // both carry an index, and have a test of their own
    }

    size_t offset = chunk->length;
    clox_chunk_write(chunk, (clox_byte_t)opcode, POS);

    ASSERT_EQ(offset + 1, disassemble_one(utest_fixture, offset));
    ASSERT_TRUE(strstr(utest_fixture->text, clox_op_code_names[opcode]) != NULL);
  }
}

UTEST_F(debug, a_byte_that_is_not_an_opcode_is_named_unknown_and_stepped_over) {
  clox_chunk_write(&utest_fixture->chunk, (clox_byte_t)0xFF, POS);

  EXPECT_EQ((size_t)1, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "Unknown opcode") != NULL);
}

UTEST_F(debug, a_disassembled_chunk_is_titled_and_lists_its_instructions) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(clox_write_constant(chunk, CLOX_NUMBER(1.0), POS));
  clox_chunk_write(chunk, OP_NOT, POS);
  clox_chunk_write(chunk, OP_RETURN, POS);

  FILE *stream = clox_test_open_buffer(utest_fixture->text, TEXT_SIZE);
  clox_disassemble_chunk_fprintf(stream, chunk, "TITLE");
  (void)clox_test_close_buffer(stream, utest_fixture->text, TEXT_SIZE);

  EXPECT_TRUE(strstr(utest_fixture->text, "TITLE") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_CONSTANT") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_NOT") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_RETURN") != NULL);
}

UTEST_F(debug, an_empty_chunk_disassembles_to_no_instructions) {
  FILE *stream = clox_test_open_buffer(utest_fixture->text, TEXT_SIZE);
  clox_disassemble_chunk_fprintf(stream, &utest_fixture->chunk, "EMPTY");
  (void)clox_test_close_buffer(stream, utest_fixture->text, TEXT_SIZE);

  EXPECT_TRUE(strstr(utest_fixture->text, "OP_") == NULL);
}

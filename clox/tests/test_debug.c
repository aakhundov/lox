#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "object.h"
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

// The opcodes carrying a one-byte operand. Written out rather than derived, so
// a new opcode counts as operand-less here: the test walking those opcodes then
// fails on the stride it did not advance by, instead of going unnoticed.
static bool takes_byte_operand(clox_op_code_t opcode) {
  switch (opcode) {
  case OP_GET_LOCAL:
  case OP_SET_LOCAL:
  case OP_POP_N:
  case OP_PRINT_N:
    return true;
  default:
    return false;
  }
}

static bool takes_jump_operand(clox_op_code_t opcode) {
  switch (opcode) {
  case OP_JUMP_TRUE:
  case OP_JUMP_FALSE:
  case OP_JUMP_FALSE_POP:
  case OP_JUMP:
  case OP_LOOP:
    return true;
  default:
    return false;
  }
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
  ASSERT_TRUE(clox_write_constant(&utest_fixture->chunk, OP_CONSTANT, CLOX_NUMBER(42.0), POS));

  EXPECT_EQ((size_t)2, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_CONSTANT") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "42") != NULL);
}

UTEST_F(debug, a_long_constant_instruction_advances_past_its_wider_index) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER((double)i), POS));
  }

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(-1.0), POS));

  EXPECT_EQ(offset + 4, disassemble_one(utest_fixture, offset));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_CONSTANT_LONG") != NULL);
}

UTEST_F(debug, every_constant_opcode_disassembles_under_its_own_name) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  // short forms are the even opcodes of the constant range
  for (size_t opcode = 0; opcode < CONST_OP_CODE_COUNT; opcode += 2) {
    size_t offset = chunk->length;
    ASSERT_TRUE(
        clox_write_constant(chunk, (clox_op_code_t)opcode, CLOX_NUMBER((double)opcode), POS));

    ASSERT_EQ(offset + 2, disassemble_one(utest_fixture, offset));
    ASSERT_TRUE(strstr(utest_fixture->text, clox_op_code_names[opcode]) != NULL);
  }
}

UTEST_F(debug, a_constant_is_disassembled_as_lox_source) {
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  ASSERT_TRUE(clox_write_constant(&utest_fixture->chunk, OP_CONSTANT,
                                  CLOX_STRING_COPY(&alloc, "text", 4), POS));

  EXPECT_EQ((size_t)2, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "\"text\"") != NULL);

  clox_allocator_free(&alloc);
}

UTEST_F(debug, a_long_global_instruction_advances_past_its_wider_index) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER((double)i), POS));
  }

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, OP_DEF_GLOBAL, CLOX_NUMBER(-1.0), POS));

  EXPECT_EQ(offset + 4, disassemble_one(utest_fixture, offset));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_DEF_GLOBAL_LONG") != NULL);
}

UTEST_F(debug, walking_a_chunk_lands_exactly_on_its_end) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(1.0), POS));
  clox_chunk_write(chunk, OP_NEGATE, POS);
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(2.0), POS));
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
    if (opcode < CONST_OP_CODE_COUNT) {
      continue; // const opcodes carry an operand
    }
    if (takes_byte_operand((clox_op_code_t)opcode) || takes_jump_operand((clox_op_code_t)opcode)) {
      continue; // so do these
    }

    size_t offset = chunk->length;
    clox_chunk_write(chunk, (clox_byte_t)opcode, POS);

    ASSERT_EQ(offset + 1, disassemble_one(utest_fixture, offset));
    ASSERT_TRUE(strstr(utest_fixture->text, clox_op_code_names[opcode]) != NULL);
  }
}

UTEST_F(debug, every_byte_operand_opcode_disassembles_under_its_own_name) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  size_t walked = 0;
  for (size_t opcode = CONST_OP_CODE_COUNT; opcode < OP_CODE_COUNT; opcode++) {
    if (!takes_byte_operand((clox_op_code_t)opcode)) {
      continue;
    }

    size_t offset = chunk->length;
    clox_chunk_write(chunk, (clox_byte_t)opcode, POS);
    clox_chunk_write(chunk, 1, POS); // the operand

    ASSERT_EQ(offset + 2, disassemble_one(utest_fixture, offset));
    ASSERT_TRUE(strstr(utest_fixture->text, clox_op_code_names[opcode]) != NULL);
    walked++;
  }

  // the loop above is vacuous if the operand list ever empties
  EXPECT_TRUE(walked > 0);
}

UTEST_F(debug, a_zero_byte_operand_is_rendered_as_two_hex_digits) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_chunk_write(chunk, OP_GET_LOCAL, POS);
  clox_chunk_write(chunk, 0, POS);

  EXPECT_EQ((size_t)2, disassemble_one(utest_fixture, 0));
  // zero is the case a '#' flag renders without its 0x prefix
  EXPECT_TRUE(strstr(utest_fixture->text, "0x00") != NULL);
}

UTEST_F(debug, the_widest_byte_operand_is_rendered_as_two_hex_digits) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_chunk_write(chunk, OP_POP_N, POS);
  clox_chunk_write(chunk, 255, POS);

  EXPECT_EQ((size_t)2, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "0xff") != NULL);
}

UTEST_F(debug, walking_a_chunk_of_byte_operand_instructions_lands_exactly_on_its_end) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(1.0), POS));
  clox_chunk_write(chunk, OP_GET_LOCAL, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, OP_SET_LOCAL, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, OP_POP_N, POS);
  clox_chunk_write(chunk, 2, POS);
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

UTEST_F(debug, every_jump_opcode_disassembles_under_its_own_name) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  size_t walked = 0;
  for (size_t opcode = CONST_OP_CODE_COUNT; opcode < OP_CODE_COUNT; opcode++) {
    if (!takes_jump_operand((clox_op_code_t)opcode)) {
      continue;
    }

    size_t offset = chunk->length;
    clox_chunk_write(chunk, (clox_byte_t)opcode, POS);
    clox_chunk_write(chunk, 0, POS); // the two operand bytes
    clox_chunk_write(chunk, 0, POS);

    ASSERT_EQ(offset + 3, disassemble_one(utest_fixture, offset));
    ASSERT_TRUE(strstr(utest_fixture->text, clox_op_code_names[opcode]) != NULL);
    walked++;
  }

  // the loop above is vacuous if the operand list ever empties
  EXPECT_TRUE(walked > 0);
}

UTEST_F(debug, a_forward_jump_is_rendered_as_the_offset_it_lands_on) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_chunk_write(chunk, OP_JUMP, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, 5, POS);

  EXPECT_EQ((size_t)3, disassemble_one(utest_fixture, 0));
  // the offset counts from the end of the instruction, not from its start
  EXPECT_TRUE(strstr(utest_fixture->text, "0008") != NULL);
}

UTEST_F(debug, a_loop_is_rendered_as_the_offset_it_returns_to) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  for (size_t i = 0; i < 10; i++) {
    clox_chunk_write(chunk, OP_NIL, POS); // something to jump back over
  }
  clox_chunk_write(chunk, OP_LOOP, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, 7, POS);

  EXPECT_EQ((size_t)13, disassemble_one(utest_fixture, 10));
  EXPECT_TRUE(strstr(utest_fixture->text, "0006") != NULL);
}

UTEST_F(debug, a_jump_operand_wider_than_a_byte_is_read_big_endian) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_chunk_write(chunk, OP_JUMP, POS);
  clox_chunk_write(chunk, 1, POS); // the high byte
  clox_chunk_write(chunk, 2, POS);

  EXPECT_EQ((size_t)3, disassemble_one(utest_fixture, 0));
  // 0x0102 is 258, so the landing offset is 261 rather than 5 or 516
  EXPECT_TRUE(strstr(utest_fixture->text, "0261") != NULL);
}

UTEST_F(debug, walking_a_chunk_of_jump_instructions_lands_exactly_on_its_end) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  clox_chunk_write(chunk, OP_JUMP, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, OP_JUMP_FALSE, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, OP_LOOP, POS);
  clox_chunk_write(chunk, 0, POS);
  clox_chunk_write(chunk, 9, POS);
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
  EXPECT_EQ((size_t)4, instructions);
}

UTEST_F(debug, a_byte_that_is_not_an_opcode_is_named_unknown_and_stepped_over) {
  clox_chunk_write(&utest_fixture->chunk, (clox_byte_t)0xFF, POS);

  EXPECT_EQ((size_t)1, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "Unknown opcode") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "0xff") != NULL);
}

UTEST_F(debug, a_disassembled_chunk_is_titled_and_lists_its_instructions) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(1.0), POS));
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

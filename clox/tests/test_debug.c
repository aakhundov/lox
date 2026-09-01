#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

#define TEXT_SIZE 4096
// one more than a single-byte constant index can address
#define OVER_BYTE_INDEX 256
// What a function disassembled here is compiled under. Nothing reads through
// either: the disassembler prints code, not the text it came from.
#define FILE_NAME "test.lox"
#define SOURCE ""

static const clox_pos_t POS = {.line = 1, .col = 1};

// A chunk on its own is nothing a collection can reach. In the interpreter a
// chunk always belongs to a function, and it is that function being marked
// that reaches the constants inside it -- so a chunk a test owns needs the
// fixture to stand in for the owner, marking exactly what a function's own
// marking would and nothing besides.
static void mark_chunk_constants(clox_allocator_t *alloc, void *ctx) {
  const clox_chunk_t *chunk = ctx;

  for (size_t i = 0; i < chunk->constants.length; i++) {
    clox_mark_value(alloc, chunk->constants.values[i]);
  }
}

struct debug {
  clox_allocator_t alloc;
  clox_chunk_t chunk;
  char text[TEXT_SIZE];
  void *mark_callback_handle;
};

UTEST_F_SETUP(debug) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_chunk_init(&utest_fixture->chunk, &utest_fixture->alloc);
  utest_fixture->mark_callback_handle = clox_register_mark_callback(
      &utest_fixture->alloc, mark_chunk_constants, &utest_fixture->chunk);
}

UTEST_F_TEARDOWN(debug) {
  (void)clox_unregister_mark_callback(&utest_fixture->alloc, utest_fixture->mark_callback_handle);
  // the chunk goes first: its constants are objects the allocator owns
  clox_chunk_free(&utest_fixture->chunk);
  clox_allocator_free(&utest_fixture->alloc);
}

// The opcodes carrying a one-byte operand. Written out rather than derived, so
// a new opcode counts as operand-less here: the test walking those opcodes then
// fails on the stride it did not advance by, instead of going unnoticed.
static bool takes_byte_operand(clox_op_code_t opcode) {
  switch (opcode) {
  case OP_GET_LOCAL:
  case OP_SET_LOCAL:
  case OP_SET_LOCAL_POP:
  case OP_GET_UPVALUE:
  case OP_SET_UPVALUE:
  case OP_SET_UPVALUE_POP:
  case OP_POP_N:
  case OP_PRINT_N:
  case OP_CALL:
    return true;
  default:
    return false;
  }
}

// The constant opcodes carrying more than their index. A closure is followed by
// two bytes per upvalue its function declares, so its stride is not the two
// bytes every other constant instruction takes.
static bool takes_upvalue_operands(clox_op_code_t opcode) {
  return opcode == OP_CLOSURE || opcode == OP_CLOSURE_LONG;
}

// A function to close over, declaring the given number of upvalues. What it
// holds does not matter: only the count is read, to say how many operand pairs
// follow the OP_CLOSURE that names it.
static clox_value_t function_capturing(struct debug *fixture, const char *name,
                                       size_t upvalue_count) {
  clox_function_t *function =
      clox_new_function(&fixture->alloc, name, strlen(name), 0, FILE_NAME, SOURCE);
  function->upvalue_count = upvalue_count;

  return CLOX_OBJECT(function);
}

// Writes the two operand bytes one captured upvalue takes: where it is taken
// from, and the index it is taken at.
static void write_upvalue(struct debug *fixture, bool is_local, clox_byte_t index) {
  clox_chunk_write(&fixture->chunk, is_local ? 1 : 0, POS);
  clox_chunk_write(&fixture->chunk, index, POS);
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
    // a closure reads its constant as the function it is closing over, so that
    // is what it has to be handed; every other constant opcode takes any value
    clox_value_t constant = takes_upvalue_operands((clox_op_code_t)opcode)
                                ? function_capturing(utest_fixture, "named", 0)
                                : CLOX_NUMBER((double)opcode);

    size_t offset = chunk->length;
    ASSERT_TRUE(clox_write_constant(chunk, (clox_op_code_t)opcode, constant, POS));

    ASSERT_EQ(offset + 2, disassemble_one(utest_fixture, offset));
    ASSERT_TRUE(strstr(utest_fixture->text, clox_op_code_names[opcode]) != NULL);
  }
}

UTEST_F(debug, a_closure_capturing_nothing_advances_like_any_constant_instruction) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  ASSERT_TRUE(
      clox_write_constant(chunk, OP_CLOSURE, function_capturing(utest_fixture, "named", 0), POS));

  EXPECT_EQ((size_t)2, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_CLOSURE") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "<cl named>") == NULL); // the function, not a closure
  EXPECT_TRUE(strstr(utest_fixture->text, "<fn named>") != NULL);
}

UTEST_F(debug, a_closure_advances_past_the_two_bytes_each_upvalue_takes) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  ASSERT_TRUE(
      clox_write_constant(chunk, OP_CLOSURE, function_capturing(utest_fixture, "named", 3), POS));
  write_upvalue(utest_fixture, true, 1);
  write_upvalue(utest_fixture, false, 2);
  write_upvalue(utest_fixture, true, 3);

  // the operand count is not in the code: it comes from the function named by
  // the constant, so a disassembler that ignored it would land mid-instruction
  EXPECT_EQ((size_t)8, disassemble_one(utest_fixture, 0));
}

UTEST_F(debug, a_capture_is_rendered_as_where_it_is_taken_from_and_at_which_index) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  ASSERT_TRUE(
      clox_write_constant(chunk, OP_CLOSURE, function_capturing(utest_fixture, "named", 2), POS));
  write_upvalue(utest_fixture, true, 4);  // a local of the enclosing frame
  write_upvalue(utest_fixture, false, 7); // an upvalue of the enclosing closure

  ASSERT_EQ((size_t)6, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "L4") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "U7") != NULL);
}

UTEST_F(debug, a_long_closure_instruction_advances_past_its_wider_index_and_its_upvalues) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER((double)i), POS));
  }

  size_t offset = chunk->length;
  ASSERT_TRUE(
      clox_write_constant(chunk, OP_CLOSURE, function_capturing(utest_fixture, "named", 1), POS));
  write_upvalue(utest_fixture, true, 1);

  // the wider index and the upvalue operands are counted independently
  EXPECT_EQ(offset + 6, disassemble_one(utest_fixture, offset));
  EXPECT_TRUE(strstr(utest_fixture->text, "OP_CLOSURE_LONG") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->text, "L1") != NULL);
}

UTEST_F(debug, walking_a_chunk_of_closure_instructions_lands_exactly_on_its_end) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(
      clox_write_constant(chunk, OP_CLOSURE, function_capturing(utest_fixture, "one", 2), POS));
  write_upvalue(utest_fixture, true, 1);
  write_upvalue(utest_fixture, false, 0);
  ASSERT_TRUE(
      clox_write_constant(chunk, OP_CLOSURE, function_capturing(utest_fixture, "two", 0), POS));
  clox_chunk_write(chunk, OP_CLOSE_UPVALUE, POS);
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

UTEST_F(debug, a_constant_is_disassembled_as_lox_source) {
  ASSERT_TRUE(clox_write_constant(&utest_fixture->chunk, OP_CONSTANT,
                                  CLOX_STRING_COPY(&utest_fixture->alloc, "text", 4), POS));

  EXPECT_EQ((size_t)2, disassemble_one(utest_fixture, 0));
  EXPECT_TRUE(strstr(utest_fixture->text, "\"text\"") != NULL);
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

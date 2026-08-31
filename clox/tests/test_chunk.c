#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "memory.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

#define MANY_BYTES 64
// one more than a single-byte constant index can address
#define OVER_BYTE_INDEX 256

static const clox_pos_t POS = {.line = 1, .col = 1};

struct chunk {
  clox_allocator_t alloc;
  clox_chunk_t chunk;
};

UTEST_F_SETUP(chunk) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_chunk_init(&utest_fixture->chunk, &utest_fixture->alloc);
}

UTEST_F_TEARDOWN(chunk) {
  // the chunk goes first: its storage comes from the allocator
  clox_chunk_free(&utest_fixture->chunk);
  clox_allocator_free(&utest_fixture->alloc);
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

UTEST_F(chunk, free_empties_it_and_a_second_init_makes_it_usable_again) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  clox_chunk_write(chunk, OP_RETURN, POS);
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(1.0), POS));

  clox_chunk_free(chunk);
  ASSERT_EQ((size_t)0, chunk->length);
  ASSERT_EQ((size_t)0, chunk->constants.length);

  // free gives the storage back and keeps no allocator; init is what makes a
  // chunk writable, whether it is the first time or the second
  clox_chunk_init(chunk, &utest_fixture->alloc);

  clox_chunk_write(chunk, OP_NIL, POS);
  EXPECT_EQ((size_t)1, chunk->length);
  EXPECT_EQ(OP_NIL, chunk->code[0]);
}

UTEST_F(chunk, a_constant_is_read_back_as_it_was_written) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(42.0), POS));
  EXPECT_EQ(OP_CONSTANT, chunk->code[offset]);

  const clox_byte_t *ip = chunk->code + offset + 1;
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), clox_read_constant(chunk, chunk->code[offset], &ip));
  EXPECT_EQ(offset + 2, (size_t)(ip - chunk->code));
}

UTEST_F(chunk, a_constant_carries_the_position_it_was_written_with) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(
      clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(1.0), (clox_pos_t){.line = 7, .col = 9}));

  ASSERT_TRUE(chunk->length > 0);
  for (size_t i = 0; i < chunk->length; i++) {
    ASSERT_EQ((size_t)7, chunk->positions[i].line);
    ASSERT_EQ((size_t)9, chunk->positions[i].col);
  }
}

UTEST_F(chunk, a_constant_past_the_single_byte_range_takes_the_long_form) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER((double)i), POS));
  }

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_BOOL(true), POS));
  EXPECT_EQ(OP_CONSTANT_LONG, chunk->code[offset]);

  const clox_byte_t *ip = chunk->code + offset + 1;
  EXPECT_VALUE_EQ(CLOX_BOOL(true), clox_read_constant(chunk, chunk->code[offset], &ip));
  EXPECT_EQ(offset + 4, (size_t)(ip - chunk->code));
}

UTEST_F(chunk, a_constant_takes_the_opcode_it_is_written_with) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, OP_DEF_GLOBAL, CLOX_NUMBER(1.0), POS));
  EXPECT_EQ(OP_DEF_GLOBAL, chunk->code[offset]);

  const clox_byte_t *ip = chunk->code + offset + 1;
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), clox_read_constant(chunk, chunk->code[offset], &ip));
  EXPECT_EQ(offset + 2, (size_t)(ip - chunk->code));
}

UTEST_F(chunk, a_long_index_takes_the_long_form_of_the_opcode_it_was_given) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER((double)i), POS));
  }

  size_t offset = chunk->length;
  ASSERT_TRUE(clox_write_constant(chunk, OP_GET_GLOBAL, CLOX_NUMBER(-1.0), POS));
  EXPECT_EQ(OP_GET_GLOBAL_LONG, chunk->code[offset]);

  const clox_byte_t *ip = chunk->code + offset + 1;
  EXPECT_VALUE_EQ(CLOX_NUMBER(-1.0), clox_read_constant(chunk, chunk->code[offset], &ip));
  EXPECT_EQ(offset + 4, (size_t)(ip - chunk->code));
}

UTEST_F(chunk, every_constant_opcode_reads_back_what_it_wrote) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  // short forms are the even opcodes of the constant range
  for (size_t opcode = 0; opcode < CONST_OP_CODE_COUNT; opcode += 2) {
    size_t offset = chunk->length;
    ASSERT_TRUE(
        clox_write_constant(chunk, (clox_op_code_t)opcode, CLOX_NUMBER((double)opcode), POS));
    ASSERT_EQ((clox_byte_t)opcode, chunk->code[offset]);

    const clox_byte_t *ip = chunk->code + offset + 1;
    ASSERT_VALUE_EQ(CLOX_NUMBER((double)opcode),
                    clox_read_constant(chunk, chunk->code[offset], &ip));
  }
}

UTEST_F(chunk, constants_on_both_sides_of_the_boundary_stay_readable) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  size_t offsets[OVER_BYTE_INDEX + 2];

  for (size_t i = 0; i < OVER_BYTE_INDEX + 2; i++) {
    offsets[i] = chunk->length;
    ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER((double)i), POS));
  }

  for (size_t i = 0; i < OVER_BYTE_INDEX + 2; i++) {
    const clox_byte_t *ip = chunk->code + offsets[i] + 1;
    ASSERT_VALUE_EQ(CLOX_NUMBER((double)i),
                    clox_read_constant(chunk, chunk->code[offsets[i]], &ip));
  }
}

UTEST_F(chunk, the_same_string_constant_is_stored_once) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_allocator_t *alloc = &utest_fixture->alloc;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "text", 4), POS));
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "text", 4), POS));

  EXPECT_EQ((size_t)1, chunk->constants.length);
  ASSERT_EQ((size_t)4, chunk->length);
  EXPECT_EQ(chunk->code[1], chunk->code[3]); // both instructions name one index
}

UTEST_F(chunk, distinct_string_constants_are_stored_apart) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_allocator_t *alloc = &utest_fixture->alloc;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "one", 3), POS));
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "two", 3), POS));

  EXPECT_EQ((size_t)2, chunk->constants.length);
  EXPECT_NE(chunk->code[1], chunk->code[3]);
  EXPECT_STREQ("one", CLOX_AS_CSTRING(chunk->constants.values[chunk->code[1]]));
  EXPECT_STREQ("two", CLOX_AS_CSTRING(chunk->constants.values[chunk->code[3]]));
}

UTEST_F(chunk, a_string_constant_seen_again_keeps_its_first_index) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_allocator_t *alloc = &utest_fixture->alloc;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "one", 3), POS));
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "two", 3), POS));
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "one", 3), POS));

  EXPECT_EQ((size_t)2, chunk->constants.length);
  EXPECT_EQ(chunk->code[1], chunk->code[5]);
}

UTEST_F(chunk, a_string_constant_is_shared_across_the_opcodes_that_name_it) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_allocator_t *alloc = &utest_fixture->alloc;

  ASSERT_TRUE(clox_write_constant(chunk, OP_DEF_GLOBAL, CLOX_STRING_COPY(alloc, "a", 1), POS));
  ASSERT_TRUE(clox_write_constant(chunk, OP_GET_GLOBAL, CLOX_STRING_COPY(alloc, "a", 1), POS));

  EXPECT_EQ((size_t)1, chunk->constants.length);
  EXPECT_EQ(chunk->code[1], chunk->code[3]);
}

UTEST_F(chunk, equal_numbers_are_stored_separately) {
  clox_chunk_t *chunk = &utest_fixture->chunk;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(1.0), POS));
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_NUMBER(1.0), POS));

  EXPECT_EQ((size_t)2, chunk->constants.length);
}

UTEST_F(chunk, a_freed_chunk_forgets_the_strings_it_had_stored) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_allocator_t *alloc = &utest_fixture->alloc;

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "one", 3), POS));
  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "two", 3), POS));
  ASSERT_EQ((clox_byte_t)1, chunk->code[3]); // "two" landed second

  // free hands the storage back and leaves no allocator behind, so a chunk
  // that is to be written to again has to be initialized a second time
  clox_chunk_free(chunk);
  clox_chunk_init(chunk, alloc);

  ASSERT_TRUE(clox_write_constant(chunk, OP_CONSTANT, CLOX_STRING_COPY(alloc, "two", 3), POS));

  EXPECT_EQ((size_t)1, chunk->constants.length);
  EXPECT_EQ((clox_byte_t)0, chunk->code[1]); // and starts over at the first index
  EXPECT_STREQ("two", CLOX_AS_CSTRING(chunk->constants.values[0]));
}

UTEST(opcodes, constant_opcodes_occupy_the_lowest_opcodes) {
  // one short and one long opcode per constant pair
  EXPECT_EQ((size_t)0, (size_t)CONST_OP_CODE_COUNT % 2);
  // the opcodes above them carry no constant
  EXPECT_TRUE(CONST_OP_CODE_COUNT < OP_CODE_COUNT);
}

UTEST(opcodes, each_constant_opcode_is_followed_by_its_long_form) {
  for (size_t opcode = 0; opcode < CONST_OP_CODE_COUNT; opcode += 2) {
    const char *short_name = clox_op_code_names[opcode];
    const char *long_name = clox_op_code_names[opcode + 1];
    size_t length = strlen(short_name);

    ASSERT_TRUE(strncmp(short_name, long_name, length) == 0);
    ASSERT_STREQ("_LONG", long_name + length);
  }
}

UTEST(opcodes, every_opcode_has_a_name) {
  for (size_t opcode = 0; opcode < OP_CODE_COUNT; opcode++) {
    ASSERT_TRUE(clox_op_code_names[opcode] != NULL);
    ASSERT_TRUE(strncmp(clox_op_code_names[opcode], "OP_", 3) == 0);
    ASSERT_TRUE(strlen(clox_op_code_names[opcode]) > 3);
  }
}

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "compiler.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

// room for a declaration per local slot, several times over
#define SOURCE_SIZE 8192
// more digits than a double can hold, and fewer than the source buffer
#define OVERSIZED_DIGITS 400
// one more local than there are slots to hold them
#define OVER_LOCAL_SLOTS (CLOX_MAX_LOCALS + 1)
// the widest offset two bytes can carry
#define TWO_BYTE_MAX ((UCHAR_MAX << CHAR_BIT) | UCHAR_MAX)
// the statement long bodies are built from, two bytes of code each
#define FILLER_STATEMENT "true;"
#define FILLER_STATEMENT_BYTES 2
// statements enough for a body outgrowing a one-byte jump offset
#define OVER_BYTE_JUMP ((UCHAR_MAX / FILLER_STATEMENT_BYTES) + 1)
// statements enough for a body outgrowing a two-byte jump offset
#define OVER_TWO_BYTE_JUMP ((TWO_BYTE_MAX / FILLER_STATEMENT_BYTES) + 1)
// one more value than a one-byte print operand can count
#define OVER_PRINT_ARGS (UCHAR_MAX + 1)

// Compares the whole emitted code array, so a stray or missing byte shows up.
#define EXPECT_CODE(chunk, ...)                                                                    \
  do {                                                                                             \
    const clox_byte_t clox_test_expected[] = {__VA_ARGS__};                                        \
    const size_t clox_test_count = sizeof(clox_test_expected) / sizeof(*clox_test_expected);       \
    ASSERT_EQ(clox_test_count, (chunk)->length);                                                   \
    for (size_t i = 0; i < clox_test_count; i++) {                                                 \
      ASSERT_EQ(clox_test_expected[i], (chunk)->code[i]);                                          \
    }                                                                                              \
  } while (0)

struct compiler {
  clox_allocator_t alloc;
  clox_compiler_t compiler;
  clox_chunk_t chunk;
  clox_test_errors_t errors;
  char source[SOURCE_SIZE]; // clox_compile needs a buffer it may modify
};

UTEST_F_SETUP(compiler) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_compiler_init(&utest_fixture->compiler, &utest_fixture->alloc);
  clox_chunk_init(&utest_fixture->chunk);
  utest_fixture->errors = (clox_test_errors_t){0};
  clox_compiler_set_error_handler(&utest_fixture->compiler, clox_test_error_handler,
                                  &utest_fixture->errors);
}

UTEST_F_TEARDOWN(compiler) {
  clox_compiler_reset_error_handler(&utest_fixture->compiler);
  clox_chunk_free(&utest_fixture->chunk);
  clox_compiler_free(&utest_fixture->compiler);
  clox_allocator_free(&utest_fixture->alloc);
}

static bool compile(struct compiler *fixture, const char *source) {
  (void)snprintf(fixture->source, SOURCE_SIZE, "%s", source);

  return clox_compile(&fixture->compiler, fixture->source, &fixture->chunk);
}

// Renders a block declaring count locals -- "{var v0;var v1;...}" -- into
// buffer. SOURCE_SIZE holds several times the longest block a caller can ask
// for, so nothing here truncates.
static const char *block_of_locals(char (*buffer)[SOURCE_SIZE], size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "{");
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "var v%zu;", i);
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, "}");

  return *buffer;
}

// Renders head followed by a block of count filler statements -- "if (false)
// {true;true;...}" -- into buffer, for the tests that need a body of a known
// size in bytes. The caller keeps count small enough for SOURCE_SIZE.
static const char *long_body(char (*buffer)[SOURCE_SIZE], const char *head, size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "%s{", head);
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, FILLER_STATEMENT);
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, "}");

  return *buffer;
}

// The same body on the heap, for the counts that outgrow SOURCE_SIZE several
// times over. Returns NULL if the text cannot be allocated; the caller owns
// what it gets back, and clox_compile may modify it in place.
static char *long_body_alloc(const char *head, size_t count) {
  size_t size = strlen(head) + (count * strlen(FILLER_STATEMENT)) + 3; // "{", "}", NUL
  char *source = malloc(size);
  if (source == NULL) {
    return NULL;
  }

  size_t written = (size_t)snprintf(source, size, "%s{", head);
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(source + written, size - written, FILLER_STATEMENT);
  }
  (void)snprintf(source + written, size - written, "}");

  return source;
}

// Renders "print 1, 1, ...;" over count values into buffer.
static const char *print_of(char (*buffer)[SOURCE_SIZE], size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "print ");
  for (size_t i = 0; i < count; i++) {
    written +=
        (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "%s1", (i > 0) ? ", " : "");
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, ";");

  return *buffer;
}

// The two-byte big-endian jump operand standing at pos.
static size_t jump_offset(const clox_chunk_t *chunk, size_t pos) {
  return ((size_t)chunk->code[pos] << CHAR_BIT) | chunk->code[pos + 1];
}

UTEST_F(compiler, a_number_becomes_a_constant) {
  ASSERT_TRUE(compile(utest_fixture, "42;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN);
  ASSERT_EQ((size_t)1, utest_fixture->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->chunk.constants.values[0]);
}

UTEST_F(compiler, a_fractional_number_keeps_its_value) {
  ASSERT_TRUE(compile(utest_fixture, "1.5;"));

  ASSERT_EQ((size_t)1, utest_fixture->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.5), utest_fixture->chunk.constants.values[0]);
}

UTEST_F(compiler, true_has_its_own_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "true;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_POP, OP_RETURN);
}

UTEST_F(compiler, false_has_its_own_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "false;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_FALSE, OP_POP, OP_RETURN);
}

UTEST_F(compiler, nil_has_its_own_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "nil;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_NIL, OP_POP, OP_RETURN);
}

UTEST_F(compiler, a_string_becomes_a_constant_holding_its_text) {
  ASSERT_TRUE(compile(utest_fixture, "\"text\";"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN);
  ASSERT_EQ((size_t)1, utest_fixture->chunk.constants.length);

  clox_value_t constant = utest_fixture->chunk.constants.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(constant));
  EXPECT_STREQ("text", CLOX_AS_CSTRING(constant));
}

UTEST_F(compiler, an_empty_string_becomes_an_empty_constant) {
  ASSERT_TRUE(compile(utest_fixture, "\"\";"));

  clox_value_t constant = utest_fixture->chunk.constants.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(constant));
  EXPECT_EQ((size_t)0, CLOX_AS_STRING(constant)->length);
}

UTEST_F(compiler, negation_follows_its_operand) {
  ASSERT_TRUE(compile(utest_fixture, "-1;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_NEGATE, OP_POP, OP_RETURN);
}

UTEST_F(compiler, not_follows_its_operand) {
  ASSERT_TRUE(compile(utest_fixture, "!true;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_NOT, OP_POP, OP_RETURN);
}

UTEST_F(compiler, unary_operators_stack_up) {
  ASSERT_TRUE(compile(utest_fixture, "--1;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_POP, OP_RETURN);
}

UTEST_F(compiler, a_binary_operator_follows_both_operands) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_POP, OP_RETURN);
  ASSERT_EQ((size_t)2, utest_fixture->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->chunk.constants.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->chunk.constants.values[1]);
}

UTEST_F(compiler, each_arithmetic_operator_has_its_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2;"));
  EXPECT_EQ(OP_ADD, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 - 2;"));
  EXPECT_EQ(OP_SUBTRACT, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 * 2;"));
  EXPECT_EQ(OP_MULTIPLY, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 / 2;"));
  EXPECT_EQ(OP_DIVIDE, utest_fixture->chunk.code[4]);
}

UTEST_F(compiler, each_comparison_operator_has_its_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "1 == 2;"));
  EXPECT_EQ(OP_EQUAL, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 != 2;"));
  EXPECT_EQ(OP_NOT_EQUAL, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 > 2;"));
  EXPECT_EQ(OP_GREATER, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 >= 2;"));
  EXPECT_EQ(OP_GREATER_EQUAL, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 < 2;"));
  EXPECT_EQ(OP_LESS, utest_fixture->chunk.code[4]);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "1 <= 2;"));
  EXPECT_EQ(OP_LESS_EQUAL, utest_fixture->chunk.code[4]);
}

UTEST_F(compiler, multiplication_binds_tighter_than_addition) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2 * 3;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2, OP_MULTIPLY,
              OP_ADD, OP_POP, OP_RETURN);
}

UTEST_F(compiler, grouping_overrides_precedence) {
  ASSERT_TRUE(compile(utest_fixture, "(1 + 2) * 3;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT, 2,
              OP_MULTIPLY, OP_POP, OP_RETURN);
}

UTEST_F(compiler, equal_precedence_associates_to_the_left) {
  ASSERT_TRUE(compile(utest_fixture, "1 - 2 - 3;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT, OP_CONSTANT, 2,
              OP_SUBTRACT, OP_POP, OP_RETURN);
}

UTEST_F(compiler, comparison_binds_looser_than_arithmetic) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2 < 3;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT, 2,
              OP_LESS, OP_POP, OP_RETURN);
}

UTEST_F(compiler, the_same_literal_twice_is_stored_twice) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 1;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_POP, OP_RETURN);
  EXPECT_EQ((size_t)2, utest_fixture->chunk.constants.length);
}

UTEST_F(compiler, a_print_statement_emits_print_after_its_expression) {
  ASSERT_TRUE(compile(utest_fixture, "print 1;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_PRINT, OP_RETURN);
}

UTEST_F(compiler, statements_are_emitted_one_after_another) {
  ASSERT_TRUE(compile(utest_fixture, "1; print 2;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_POP, OP_CONSTANT, 1, OP_PRINT, OP_RETURN);
  ASSERT_EQ((size_t)2, utest_fixture->chunk.constants.length);
}

UTEST_F(compiler, an_empty_source_compiles_to_a_bare_return) {
  ASSERT_TRUE(compile(utest_fixture, ""));
  EXPECT_CODE(&utest_fixture->chunk, OP_RETURN);
}

UTEST_F(compiler, a_variable_declaration_defines_a_global_from_its_initializer) {
  ASSERT_TRUE(compile(utest_fixture, "var a = 1;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_DEF_GLOBAL, 1, OP_RETURN);
  ASSERT_EQ((size_t)2, utest_fixture->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->chunk.constants.values[0]);

  clox_value_t name = utest_fixture->chunk.constants.values[1];
  ASSERT_TRUE(CLOX_IS_STRING(name));
  EXPECT_STREQ("a", CLOX_AS_CSTRING(name));
}

UTEST_F(compiler, a_variable_declaration_without_an_initializer_defines_it_as_nil) {
  ASSERT_TRUE(compile(utest_fixture, "var a;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_NIL, OP_DEF_GLOBAL, 0, OP_RETURN);
  ASSERT_EQ((size_t)1, utest_fixture->chunk.constants.length);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[0]));
}

UTEST_F(compiler, reading_a_variable_emits_a_global_get) {
  ASSERT_TRUE(compile(utest_fixture, "a;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_GET_GLOBAL, 0, OP_POP, OP_RETURN);
  ASSERT_EQ((size_t)1, utest_fixture->chunk.constants.length);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[0]));
}

UTEST_F(compiler, assigning_to_a_variable_emits_a_global_set) {
  ASSERT_TRUE(compile(utest_fixture, "a = 1;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_SET_GLOBAL, 1, OP_POP, OP_RETURN);
  ASSERT_EQ((size_t)2, utest_fixture->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->chunk.constants.values[0]);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[1]));
}

UTEST_F(compiler, assignment_associates_to_the_right) {
  ASSERT_TRUE(compile(utest_fixture, "a = b = 1;"));

  // the innermost assignment runs first, and its value flows outward
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_SET_GLOBAL, 1, OP_SET_GLOBAL, 2, OP_POP,
              OP_RETURN);
  ASSERT_EQ((size_t)3, utest_fixture->chunk.constants.length);
  EXPECT_STREQ("b", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[1]));
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[2]));
}

UTEST_F(compiler, a_variable_reads_itself_inside_its_own_initializer) {
  ASSERT_TRUE(compile(utest_fixture, "var a = a;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_GET_GLOBAL, 0, OP_DEF_GLOBAL, 0, OP_RETURN);
}

UTEST_F(compiler, the_same_string_literal_becomes_one_constant) {
  ASSERT_TRUE(compile(utest_fixture, "\"x\" + \"x\";"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 0, OP_ADD, OP_POP, OP_RETURN);
  ASSERT_EQ((size_t)1, utest_fixture->chunk.constants.length);
  EXPECT_STREQ("x", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[0]));
}

UTEST_F(compiler, a_name_read_and_assigned_shares_one_constant) {
  ASSERT_TRUE(compile(utest_fixture, "a = a;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_GET_GLOBAL, 0, OP_SET_GLOBAL, 0, OP_POP, OP_RETURN);
  ASSERT_EQ((size_t)1, utest_fixture->chunk.constants.length);
}

UTEST_F(compiler, a_variable_takes_part_in_expressions) {
  ASSERT_TRUE(compile(utest_fixture, "print a + 1;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_GET_GLOBAL, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT, OP_RETURN);
}

UTEST_F(compiler, a_block_keeps_its_variable_out_of_the_globals) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; }"));

  // one local leaves by the plain OP_POP, not a counted one
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN);
  // the initializer is the only constant: a local is never named
  EXPECT_EQ((size_t)1, utest_fixture->chunk.constants.length);
}

UTEST_F(compiler, a_local_is_read_by_its_slot) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; a; }"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 0, OP_POP, OP_POP, OP_RETURN);
  EXPECT_EQ((size_t)1, utest_fixture->chunk.constants.length);
}

UTEST_F(compiler, a_local_is_assigned_by_its_slot) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; a = 2; }"));

  // the value is left behind, as every assignment is an expression
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SET_LOCAL, 0, OP_POP,
              OP_POP, OP_RETURN);
}

UTEST_F(compiler, locals_take_their_slots_in_declaration_order) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; var b = 2; a; b; }"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_LOCAL, 0, OP_POP,
              OP_GET_LOCAL, 1, OP_POP, OP_POP_N, 2, OP_RETURN);
}

UTEST_F(compiler, a_local_shadows_an_enclosing_one_of_the_same_name) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; { var a = 2; a; } }"));

  // the inner slot wins, and each block pops only what it declared
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_LOCAL, 1, OP_POP,
              OP_POP, OP_POP, OP_RETURN);
}

UTEST_F(compiler, an_inner_block_reaches_the_enclosing_local) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; { a; } }"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 0, OP_POP, OP_POP, OP_RETURN);
}

UTEST_F(compiler, an_inner_block_assigns_to_the_enclosing_local) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; { a = 2; } }"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SET_LOCAL, 0, OP_POP,
              OP_POP, OP_RETURN);
}

UTEST_F(compiler, a_block_declaring_nothing_pops_nothing) {
  ASSERT_TRUE(compile(utest_fixture, "{ 1; }"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN);
}

UTEST_F(compiler, leaving_a_block_pops_its_locals_in_one_instruction) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a; var b; var c; }"));

  EXPECT_CODE(&utest_fixture->chunk, OP_NIL, OP_NIL, OP_NIL, OP_POP_N, 3, OP_RETURN);
}

UTEST_F(compiler, the_counted_pop_starts_at_the_second_local) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a; }"));
  EXPECT_CODE(&utest_fixture->chunk, OP_NIL, OP_POP, OP_RETURN);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(compile(utest_fixture, "{ var a; var b; }"));
  EXPECT_CODE(&utest_fixture->chunk, OP_NIL, OP_NIL, OP_POP_N, 2, OP_RETURN);
}

UTEST_F(compiler, a_declaration_outside_every_block_is_still_a_global) {
  ASSERT_TRUE(compile(utest_fixture, "var a = 1; { var b = 2; }"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_DEF_GLOBAL, 1, OP_CONSTANT, 2, OP_POP,
              OP_RETURN);
  // only the global is named
  ASSERT_EQ((size_t)3, utest_fixture->chunk.constants.length);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[1]));
}

UTEST_F(compiler, filling_every_local_slot_pops_them_in_byte_sized_groups) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, block_of_locals(&source, CLOX_MAX_LOCALS)));

  // one OP_NIL per local, then the pops no single byte could count
  const clox_chunk_t *chunk = &utest_fixture->chunk;
  ASSERT_EQ((size_t)CLOX_MAX_LOCALS + 5, chunk->length);
  EXPECT_EQ(OP_NIL, chunk->code[CLOX_MAX_LOCALS - 1]);
  EXPECT_EQ(OP_POP_N, chunk->code[CLOX_MAX_LOCALS]);
  EXPECT_EQ(UCHAR_MAX, chunk->code[CLOX_MAX_LOCALS + 1]);
  EXPECT_EQ(OP_POP_N, chunk->code[CLOX_MAX_LOCALS + 2]);
  EXPECT_EQ(CLOX_MAX_LOCALS - UCHAR_MAX, chunk->code[CLOX_MAX_LOCALS + 3]);
  EXPECT_EQ(OP_RETURN, chunk->code[CLOX_MAX_LOCALS + 4]);
}

UTEST_F(compiler, a_print_of_several_values_counts_them_in_its_operand) {
  ASSERT_TRUE(compile(utest_fixture, "print 1, 2;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_PRINT_N, 2, OP_RETURN);
  ASSERT_EQ((size_t)2, utest_fixture->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->chunk.constants.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->chunk.constants.values[1]);
}

UTEST_F(compiler, the_values_of_a_print_are_pushed_left_to_right) {
  ASSERT_TRUE(compile(utest_fixture, "print a, b, c;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_GET_GLOBAL, 0, OP_GET_GLOBAL, 1, OP_GET_GLOBAL, 2,
              OP_PRINT_N, 3, OP_RETURN);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[0]));
  EXPECT_STREQ("c", CLOX_AS_CSTRING(utest_fixture->chunk.constants.values[2]));
}

UTEST_F(compiler, a_print_of_one_value_stays_on_the_uncounted_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "print 1;"));
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_PRINT, OP_RETURN);
}

UTEST_F(compiler, the_widest_print_the_operand_can_count_is_accepted) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, print_of(&source, UCHAR_MAX)));

  const clox_chunk_t *chunk = &utest_fixture->chunk;
  // one two-byte constant per value, then the counted print and the return
  size_t print_at = (size_t)UCHAR_MAX * 2;
  ASSERT_EQ(print_at + 3, chunk->length);
  EXPECT_EQ(OP_PRINT_N, chunk->code[print_at]);
  EXPECT_EQ(UCHAR_MAX, chunk->code[print_at + 1]);
}

UTEST_F(compiler, one_print_value_more_than_the_operand_can_count_is_reported) {
  char source[SOURCE_SIZE];
  EXPECT_FALSE(compile(utest_fixture, print_of(&source, OVER_PRINT_ARGS)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "print args") != NULL);
}

UTEST_F(compiler, a_print_of_several_values_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "print 1, 2"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after values") != NULL);
}

UTEST_F(compiler, an_if_jumps_over_its_branch_when_the_condition_fails) {
  ASSERT_TRUE(compile(utest_fixture, "if (true) print 1;"));

  // the conditional jump drops the condition and skips the branch; with no
  // else to skip in turn, the branch closes on nothing
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 3, OP_CONSTANT, 0, OP_PRINT,
              OP_RETURN);
}

UTEST_F(compiler, an_else_branch_is_jumped_over_by_the_then_branch) {
  ASSERT_TRUE(compile(utest_fixture, "if (true) print 1; else print 2;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_CONSTANT, 0, OP_PRINT,
              OP_JUMP, 0, 3, OP_CONSTANT, 1, OP_PRINT, OP_RETURN);
}

UTEST_F(compiler, an_else_belongs_to_the_nearest_if) {
  ASSERT_TRUE(compile(utest_fixture, "if (true) if (false) print 1; else print 2;"));

  // the inner if owns the else, so only the inner then branch closes with a
  // jump; the outer one has no else to jump over
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 13, OP_FALSE, OP_JUMP_FALSE_POP,
              0, 6, OP_CONSTANT, 0, OP_PRINT, OP_JUMP, 0, 3, OP_CONSTANT, 1, OP_PRINT, OP_RETURN);
}

UTEST_F(compiler, a_while_loop_jumps_back_to_its_condition) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) print 1;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_CONSTANT, 0, OP_PRINT,
              OP_LOOP, 0, 10, OP_RETURN);
}

UTEST_F(compiler, a_for_loop_runs_its_increment_between_the_body_and_the_condition) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0; i < 3; i = i + 1) print i;"));

  // the increment is compiled before the body but jumped over on the way in,
  // so the body's back jump reaches the increment and the increment's the
  // condition; leaving the loop pops the variable it declared
  EXPECT_CODE(&utest_fixture->chunk,                                    //
              OP_CONSTANT, 0,                                           // var i = 0
              OP_GET_LOCAL, 0, OP_CONSTANT, 1, OP_LESS,                 // i < 3
              OP_JUMP_FALSE_POP, 0, 20,                                 // exit the loop
              OP_JUMP, 0, 11,                                           // skip the increment
              OP_GET_LOCAL, 0, OP_CONSTANT, 2, OP_ADD, OP_SET_LOCAL, 0, // i = i + 1
              OP_POP,                                                   // drop its value
              OP_LOOP, 0, 22,                                           // back to i < 3
              OP_GET_LOCAL, 0, OP_PRINT,                                // print i
              OP_LOOP, 0, 17,                                           // back to i = i + 1
              OP_POP,                                                   // drop i
              OP_RETURN);
}

UTEST_F(compiler, a_for_loop_without_clauses_only_jumps_back) {
  ASSERT_TRUE(compile(utest_fixture, "for (;;) 1;"));

  // no condition means no way out, and no initializer means nothing to pop
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_POP, OP_LOOP, 0, 6, OP_RETURN);
}

UTEST_F(compiler, a_for_loop_keeps_its_variable_in_a_scope_of_its_own) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0;;) i;"));

  // i is a local, never named in the constants, and popped on the way out
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 0, OP_POP, OP_LOOP, 0, 6, OP_POP,
              OP_RETURN);
  EXPECT_EQ((size_t)1, utest_fixture->chunk.constants.length);
}

UTEST_F(compiler, a_for_initializer_may_be_an_expression_instead_of_a_declaration) {
  ASSERT_TRUE(compile(utest_fixture, "for (a = 0;;) 1;"));

  // an expression initializer runs once and drops its value
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_SET_GLOBAL, 1, OP_POP, OP_CONSTANT, 2,
              OP_POP, OP_LOOP, 0, 6, OP_RETURN);
}

UTEST_F(compiler, a_loop_carrying_no_break_reserves_nothing_for_one) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) 1;"));

  // the way out of a loop is emitted by the first break that needs it, so a
  // body without one costs the same as before there were breaks at all
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_CONSTANT, 0, OP_POP,
              OP_LOOP, 0, 10, OP_RETURN);
}

UTEST_F(compiler, a_break_jumps_past_the_end_of_its_loop) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { break; }"));

  // the break's jump lands after the loop's backward one, which is where the
  // failing condition lands too
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_JUMP, 0, 3, OP_LOOP, 0,
              10, OP_RETURN);
}

UTEST_F(compiler, a_second_break_reaches_the_jump_the_first_one_left) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { break; break; }"));

  // only the first break emits a forward jump; every later one is behind it
  // and reaches it backwards, so no list of sites has to be kept
  EXPECT_CODE(&utest_fixture->chunk,   //
              OP_TRUE,                 // condition
              OP_JUMP_FALSE_POP, 0, 9, // exit the loop
              OP_JUMP, 0, 6,           // first break, out of the loop
              OP_LOOP, 0, 6,           // second break, back to the first
              OP_LOOP, 0, 13,          // back to the condition
              OP_RETURN);
}

UTEST_F(compiler, a_break_pops_the_locals_of_every_block_it_leaves) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { var a; { var b; break; } }"));

  // both locals go in one counted pop before the jump; the pops the blocks
  // themselves emit stay behind for the turn that reaches their closing brace
  EXPECT_CODE(&utest_fixture->chunk,    //
              OP_TRUE,                  // condition
              OP_JUMP_FALSE_POP, 0, 12, // exit the loop
              OP_NIL, OP_NIL,           // var a, var b
              OP_POP_N, 2,              // the break drops both
              OP_JUMP, 0, 5,            // out of the loop
              OP_POP, OP_POP,           // the blocks ending in order
              OP_LOOP, 0, 16,           // back to the condition
              OP_RETURN);
}

UTEST_F(compiler, a_break_leaves_the_for_loop_variable_to_the_loop_itself) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0;;) break;"));

  // the variable belongs to the loop's own scope, not to the body, so the
  // break jumps to the pop that drops it rather than emitting one
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_JUMP, 0, 3, OP_LOOP, 0, 6, OP_POP,
              OP_RETURN);
}

UTEST_F(compiler, a_break_in_a_nested_loop_leaves_only_that_loop) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { while (true) break; }"));

  // the inner break lands on the outer loop's backward jump, so the outer
  // loop keeps running and never reserves a way out of its own
  EXPECT_CODE(&utest_fixture->chunk,    //
              OP_TRUE,                  // outer condition
              OP_JUMP_FALSE_POP, 0, 13, // exit the outer loop
              OP_TRUE,                  // inner condition
              OP_JUMP_FALSE_POP, 0, 6,  // exit the inner loop
              OP_JUMP, 0, 3,            // break, out of the inner loop
              OP_LOOP, 0, 10,           // back to the inner condition
              OP_LOOP, 0, 17,           // back to the outer condition
              OP_RETURN);
}

UTEST_F(compiler, a_continue_jumps_back_to_the_condition_of_a_while) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { continue; }"));

  // the condition is already behind the body, so continue needs no patching
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_LOOP, 0, 7, OP_LOOP, 0,
              10, OP_RETURN);
}

UTEST_F(compiler, a_continue_jumps_back_to_the_increment_of_a_for) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0;; i = 1) continue;"));

  // the increment has to run before the next turn, so continue goes there
  // and not to the condition, exactly where the body's own back jump goes
  EXPECT_CODE(&utest_fixture->chunk,           //
              OP_CONSTANT, 0,                  // var i = 0
              OP_JUMP, 0, 8,                   // skip the increment
              OP_CONSTANT, 1, OP_SET_LOCAL, 0, // i = 1
              OP_POP,                          // drop its value
              OP_LOOP, 0, 11,                  // back to the condition
              OP_LOOP, 0, 11,                  // continue, to the increment
              OP_LOOP, 0, 14,                  // body's own back jump
              OP_POP,                          // drop i
              OP_RETURN);
}

UTEST_F(compiler, a_continue_pops_the_locals_of_every_block_it_leaves) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { var a; continue; }"));

  // the continue drops the local before jumping back, and the pop the block
  // emits at its closing brace stays for the turn that reaches it
  EXPECT_CODE(&utest_fixture->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 9, OP_NIL, OP_POP, OP_LOOP, 0,
              9, OP_POP, OP_LOOP, 0, 13, OP_RETURN);
}

UTEST_F(compiler, and_jumps_past_its_right_operand_when_the_left_is_falsy) {
  ASSERT_TRUE(compile(utest_fixture, "1 and 2;"));

  // the jump keeps the left operand, which becomes the value of the whole
  // expression; the pop only runs when the right operand replaces it
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_JUMP_FALSE, 0, 3, OP_POP, OP_CONSTANT, 1,
              OP_POP, OP_RETURN);
}

UTEST_F(compiler, or_jumps_past_its_right_operand_when_the_left_is_truthy) {
  ASSERT_TRUE(compile(utest_fixture, "1 or 2;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_JUMP_TRUE, 0, 3, OP_POP, OP_CONSTANT, 1,
              OP_POP, OP_RETURN);
}

UTEST_F(compiler, and_binds_tighter_than_or) {
  ASSERT_TRUE(compile(utest_fixture, "1 and 2 or 3;"));

  // the and is complete before the or begins, so its jump lands on the or
  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_JUMP_FALSE, 0, 3, OP_POP, OP_CONSTANT, 1,
              OP_JUMP_TRUE, 0, 3, OP_POP, OP_CONSTANT, 2, OP_POP, OP_RETURN);
}

UTEST_F(compiler, and_binds_looser_than_comparison) {
  ASSERT_TRUE(compile(utest_fixture, "1 < 2 and 3;"));

  EXPECT_CODE(&utest_fixture->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS, OP_JUMP_FALSE, 0, 3,
              OP_POP, OP_CONSTANT, 2, OP_POP, OP_RETURN);
}

UTEST_F(compiler, a_forward_jump_further_than_a_byte_keeps_its_high_byte) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, long_body(&source, "if (false) ", OVER_BYTE_JUMP)));

  const clox_chunk_t *chunk = &utest_fixture->chunk;
  // the condition, then the conditional jump: its operand stands at 2
  ASSERT_EQ(OP_JUMP_FALSE_POP, chunk->code[1]);
  EXPECT_TRUE(chunk->code[2] > 0); // the offset outgrew a single byte
  // it skips the body and the jump closing it, landing on the final return
  EXPECT_EQ(chunk->length - 1, 2 + 2 + jump_offset(chunk, 2));
  EXPECT_EQ(OP_RETURN, chunk->code[chunk->length - 1]);
}

UTEST_F(compiler, a_backward_jump_further_than_a_byte_keeps_its_high_byte) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, long_body(&source, "while (false) ", OVER_BYTE_JUMP)));

  const clox_chunk_t *chunk = &utest_fixture->chunk;
  // the loop instruction closes the body: three bytes, then the return
  size_t loop = chunk->length - 4;
  ASSERT_EQ(OP_LOOP, chunk->code[loop]);
  EXPECT_TRUE(chunk->code[loop + 1] > 0); // the offset outgrew a single byte
  // it returns to the condition, which opens the chunk
  EXPECT_EQ((size_t)0, loop + 3 - jump_offset(chunk, loop + 1));
}

UTEST_F(compiler, a_forward_jump_too_long_to_encode_is_reported) {
  char *source = long_body_alloc("if (false) ", OVER_TWO_BYTE_JUMP);
  ASSERT_TRUE(source != NULL);

  bool compiled = clox_compile(&utest_fixture->compiler, source, &utest_fixture->chunk);
  free(source);

  EXPECT_FALSE(compiled);
  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "forward jump") != NULL);
}

UTEST_F(compiler, a_backward_jump_too_long_to_encode_is_reported) {
  char *source = long_body_alloc("while (false) ", OVER_TWO_BYTE_JUMP);
  ASSERT_TRUE(source != NULL);

  bool compiled = clox_compile(&utest_fixture->compiler, source, &utest_fixture->chunk);
  free(source);

  EXPECT_FALSE(compiled);
  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "backward jump") != NULL);
}

UTEST_F(compiler, an_if_without_parentheses_around_its_condition_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "if true) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "if condition") != NULL);
}

UTEST_F(compiler, a_while_without_parentheses_around_its_condition_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "while true) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "while condition") != NULL);
}

UTEST_F(compiler, a_for_without_parentheses_around_its_clauses_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "for ;;) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "for clauses") != NULL);
}

UTEST_F(compiler, a_for_condition_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "for (; true) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "for condition") != NULL);
}

UTEST_F(compiler, a_declaration_is_not_a_body_a_loop_may_carry) {
  // only a block opens a scope a declaration can live in
  EXPECT_FALSE(compile(utest_fixture, "while (true) var a = 1;"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, a_break_outside_every_loop_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "break;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "break") != NULL);
}

UTEST_F(compiler, a_continue_outside_every_loop_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "continue;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "continue") != NULL);
}

UTEST_F(compiler, a_break_after_the_loop_it_followed_is_reported) {
  // the loop's body is over, so its break is no longer inside anything
  EXPECT_FALSE(compile(utest_fixture, "while (true) 1; break;"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, a_break_inside_a_block_outside_a_loop_is_reported) {
  // a block is not a loop body, however deep the break sits in it
  EXPECT_FALSE(compile(utest_fixture, "{ var a; { break; } }"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, a_break_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "while (true) { break }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "';'") != NULL);
}

UTEST_F(compiler, a_continue_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "while (true) { continue }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "';'") != NULL);
}

UTEST_F(compiler, break_and_continue_are_not_names_a_declaration_may_take) {
  EXPECT_FALSE(compile(utest_fixture, "var break = 1;"));
  EXPECT_FALSE(compile(utest_fixture, "var continue = 1;"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, one_local_more_than_there_are_slots_is_reported) {
  char source[SOURCE_SIZE];
  EXPECT_FALSE(compile(utest_fixture, block_of_locals(&source, OVER_LOCAL_SLOTS)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "locals") != NULL);
}

UTEST_F(compiler, declaring_a_local_twice_in_one_block_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "{ var a; var a; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "already defined") != NULL);
}

UTEST_F(compiler, a_local_may_repeat_a_name_from_an_enclosing_block) {
  EXPECT_TRUE(compile(utest_fixture, "{ var a; { var a; } }"));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(compiler, a_local_read_inside_its_own_initializer_is_reported) {
  // the global form of this compiles and fails at run time instead
  EXPECT_FALSE(compile(utest_fixture, "{ var a = a; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "own initializer") != NULL);
}

UTEST_F(compiler, a_block_without_its_closing_brace_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "{ 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after block") != NULL);
}

UTEST_F(compiler, a_number_too_large_to_represent_is_reported) {
  char digits[OVERSIZED_DIGITS + 1];
  memset(digits, '9', OVERSIZED_DIGITS);
  digits[OVERSIZED_DIGITS] = '\0';

  EXPECT_FALSE(compile(utest_fixture, digits));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)1, utest_fixture->errors.positions[0].col);
}

UTEST_F(compiler, an_unfinished_expression_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "1 +"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strlen(utest_fixture->errors.messages[0]) > 0);
  EXPECT_EQ((size_t)1, utest_fixture->errors.positions[0].line);
}

UTEST_F(compiler, an_unknown_character_is_reported_where_it_stands) {
  EXPECT_FALSE(compile(utest_fixture, "1 + @"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)1, utest_fixture->errors.positions[0].line);
  EXPECT_EQ((size_t)5, utest_fixture->errors.positions[0].col);
}

UTEST_F(compiler, an_unclosed_group_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "(1 + 2"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, trailing_input_after_an_expression_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "1 2"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)3, utest_fixture->errors.positions[0].col);
}

UTEST_F(compiler, an_error_on_a_later_line_carries_that_line) {
  EXPECT_FALSE(compile(utest_fixture, "1 +\n\n@"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)3, utest_fixture->errors.positions[0].line);
}

UTEST_F(compiler, a_statement_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "print 1"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after value") != NULL);
}

UTEST_F(compiler, a_declaration_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "var a = 1"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after variable declaration") != NULL);
}

UTEST_F(compiler, a_declaration_without_a_name_is_reported_where_the_name_should_stand) {
  EXPECT_FALSE(compile(utest_fixture, "var 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "variable name") != NULL);
  EXPECT_EQ((size_t)5, utest_fixture->errors.positions[0].col);
}

UTEST_F(compiler, assigning_to_something_that_is_not_a_variable_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "1 = 2;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "assignment target") != NULL);
  EXPECT_EQ((size_t)3, utest_fixture->errors.positions[0].col);
}

UTEST_F(compiler, assignment_reaching_past_a_binary_operator_is_reported) {
  // '=' binds loosest, so it cannot be the right operand of '+'
  EXPECT_FALSE(compile(utest_fixture, "a + b = 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "assignment target") != NULL);
  EXPECT_EQ((size_t)7, utest_fixture->errors.positions[0].col);
}

UTEST_F(compiler, an_error_in_a_later_statement_is_reported_too) {
  // the first error silences the rest of its statement, not the next one
  EXPECT_FALSE(compile(utest_fixture, "1 + ;\n2 + ;"));

  ASSERT_EQ((size_t)2, utest_fixture->errors.count);
  EXPECT_EQ((size_t)1, utest_fixture->errors.positions[0].line);
  EXPECT_EQ((size_t)2, utest_fixture->errors.positions[1].line);
}

UTEST_F(compiler, one_broken_statement_reports_one_error) {
  EXPECT_FALSE(compile(utest_fixture, "print 1 + ;"));

  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(compiler, a_compiler_without_a_handler_still_reports_failure) {
  clox_compiler_reset_error_handler(&utest_fixture->compiler);

  EXPECT_FALSE(compile(utest_fixture, "1 +"));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

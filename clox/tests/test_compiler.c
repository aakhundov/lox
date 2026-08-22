#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "compiler.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

#define SOURCE_SIZE 512
// more digits than a double can hold, and fewer than the source buffer
#define OVERSIZED_DIGITS 400

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

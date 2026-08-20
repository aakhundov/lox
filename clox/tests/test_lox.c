#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <utest.h>

#include "chunk.h"
#include "compiler.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#include "support/harness.h"

#define SOURCE_SIZE 256

// Source in, printed values and reported errors out: the two halves of the
// interpreter working together, as the REPL and a script file drive them.

struct lox {
  clox_allocator_t alloc;
  clox_compiler_t compiler;
  clox_vm_t vm;
  clox_chunk_t chunk;
  clox_test_printed_t printed;
  clox_test_errors_t errors;
  char source[SOURCE_SIZE];
};

UTEST_F_SETUP(lox) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_compiler_init(&utest_fixture->compiler, &utest_fixture->alloc);
  clox_vm_init(&utest_fixture->vm, &utest_fixture->alloc);
  clox_chunk_init(&utest_fixture->chunk);
  utest_fixture->printed = (clox_test_printed_t){0};
  utest_fixture->errors = (clox_test_errors_t){0};
  clox_compiler_set_error_handler(&utest_fixture->compiler, clox_test_error_handler,
                                  &utest_fixture->errors);
  clox_vm_set_error_handler(&utest_fixture->vm, clox_test_error_handler, &utest_fixture->errors);
  clox_vm_set_print_fn(&utest_fixture->vm, clox_test_print_fn, &utest_fixture->printed);
}

UTEST_F_TEARDOWN(lox) {
  clox_compiler_reset_error_handler(&utest_fixture->compiler);
  clox_vm_reset_error_handler(&utest_fixture->vm);
  clox_vm_set_default_print_fn(&utest_fixture->vm);
  clox_chunk_free(&utest_fixture->chunk);
  clox_vm_free(&utest_fixture->vm);
  clox_compiler_free(&utest_fixture->compiler);
  clox_allocator_free(&utest_fixture->alloc);
}

static bool run(struct lox *fixture, const char *source) {
  (void)snprintf(fixture->source, SOURCE_SIZE, "%s", source);

  if (!clox_compile(&fixture->compiler, fixture->source, &fixture->chunk)) {
    return false;
  }

  return clox_interpret(&fixture->vm, &fixture->chunk);
}

// The single value a successful run printed.
static clox_value_t only_printed(struct lox *fixture) {
  return fixture->printed.values[0];
}

UTEST_F(lox, a_number_evaluates_to_itself) {
  ASSERT_TRUE(run(utest_fixture, "42"));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), only_printed(utest_fixture));
}

UTEST_F(lox, arithmetic_follows_precedence) {
  ASSERT_TRUE(run(utest_fixture, "1 + 2 * 3"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), only_printed(utest_fixture));
}

UTEST_F(lox, grouping_overrides_precedence) {
  ASSERT_TRUE(run(utest_fixture, "(1 + 2) * 3"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(9.0), only_printed(utest_fixture));
}

UTEST_F(lox, subtraction_and_division_associate_to_the_left) {
  ASSERT_TRUE(run(utest_fixture, "10 - 4 - 3"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));

  utest_fixture->printed = (clox_test_printed_t){0};
  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(run(utest_fixture, "12 / 3 / 2"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, unary_minus_applies_to_the_whole_operand) {
  ASSERT_TRUE(run(utest_fixture, "-(2 + 3)"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-5.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_comparison_evaluates_to_a_boolean) {
  ASSERT_TRUE(run(utest_fixture, "1 + 1 < 3"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, equality_between_different_types_is_false) {
  ASSERT_TRUE(run(utest_fixture, "1 == true"));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), only_printed(utest_fixture));
}

UTEST_F(lox, not_of_a_falsey_value_is_true) {
  ASSERT_TRUE(run(utest_fixture, "!nil"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, not_of_zero_is_true) {
  ASSERT_TRUE(run(utest_fixture, "!0"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, strings_concatenate_with_plus) {
  ASSERT_TRUE(run(utest_fixture, "\"one\" + \"two\""));

  clox_value_t result = only_printed(utest_fixture);
  ASSERT_TRUE(CLOX_IS_STRING(result));
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(result));
}

UTEST_F(lox, equal_strings_compare_equal) {
  ASSERT_TRUE(run(utest_fixture, "\"same\" == \"sa\" + \"me\""));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, a_value_renders_the_way_the_repl_shows_it) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  ASSERT_TRUE(run(utest_fixture, "3 / 4"));
  EXPECT_STREQ("0.75", clox_test_value_string(&buffer, only_printed(utest_fixture)));
}

UTEST_F(lox, a_syntax_error_stops_before_anything_runs) {
  EXPECT_FALSE(run(utest_fixture, "1 +"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_type_error_is_reported_at_run_time) {
  EXPECT_FALSE(run(utest_fixture, "\"text\" - 1"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)1, utest_fixture->errors.positions[0].line);
}

UTEST_F(lox, a_runtime_error_points_at_the_operator_not_the_operand) {
  EXPECT_FALSE(run(utest_fixture, "1 +\n2 *\n\"text\""));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)2, utest_fixture->errors.positions[0].line);
  EXPECT_EQ((size_t)3, utest_fixture->errors.positions[0].col);
}

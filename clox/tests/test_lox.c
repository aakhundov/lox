#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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
  ASSERT_TRUE(run(utest_fixture, "print 42;"));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), only_printed(utest_fixture));
}

UTEST_F(lox, arithmetic_follows_precedence) {
  ASSERT_TRUE(run(utest_fixture, "print 1 + 2 * 3;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), only_printed(utest_fixture));
}

UTEST_F(lox, grouping_overrides_precedence) {
  ASSERT_TRUE(run(utest_fixture, "print (1 + 2) * 3;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(9.0), only_printed(utest_fixture));
}

UTEST_F(lox, subtraction_and_division_associate_to_the_left) {
  ASSERT_TRUE(run(utest_fixture, "print 10 - 4 - 3;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));

  utest_fixture->printed = (clox_test_printed_t){0};
  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(run(utest_fixture, "print 12 / 3 / 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, unary_minus_applies_to_the_whole_operand) {
  ASSERT_TRUE(run(utest_fixture, "print -(2 + 3);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-5.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_comparison_evaluates_to_a_boolean) {
  ASSERT_TRUE(run(utest_fixture, "print 1 + 1 < 3;"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, equality_between_different_types_is_false) {
  ASSERT_TRUE(run(utest_fixture, "print 1 == true;"));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), only_printed(utest_fixture));
}

UTEST_F(lox, not_of_a_falsey_value_is_true) {
  ASSERT_TRUE(run(utest_fixture, "print !nil;"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, not_of_zero_is_true) {
  ASSERT_TRUE(run(utest_fixture, "print !0;"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, strings_concatenate_with_plus) {
  ASSERT_TRUE(run(utest_fixture, "print \"one\" + \"two\";"));

  clox_value_t result = only_printed(utest_fixture);
  ASSERT_TRUE(CLOX_IS_STRING(result));
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(result));
}

UTEST_F(lox, equal_strings_compare_equal) {
  ASSERT_TRUE(run(utest_fixture, "print \"same\" == \"sa\" + \"me\";"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, statements_run_in_the_order_they_are_written) {
  ASSERT_TRUE(run(utest_fixture, "print 1; print 2;"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, an_expression_statement_prints_nothing) {
  ASSERT_TRUE(run(utest_fixture, "1 + 2;"));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(lox, a_variable_holds_the_value_it_was_given) {
  ASSERT_TRUE(run(utest_fixture, "var a = 1 + 2; print a;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_variable_without_an_initializer_is_nil) {
  ASSERT_TRUE(run(utest_fixture, "var a; print a;"));
  EXPECT_VALUE_EQ(CLOX_NIL, only_printed(utest_fixture));
}

UTEST_F(lox, assignment_replaces_the_value_of_a_variable) {
  ASSERT_TRUE(run(utest_fixture, "var a = 1; a = 2; print a;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, assignment_evaluates_to_the_value_it_assigned) {
  ASSERT_TRUE(run(utest_fixture, "var a = 1; print a = 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, one_assignment_can_feed_another) {
  ASSERT_TRUE(run(utest_fixture, "var a; var b; a = b = 3; print a + b;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), only_printed(utest_fixture));
}

UTEST_F(lox, declaring_a_variable_again_replaces_it) {
  ASSERT_TRUE(run(utest_fixture, "var a = 1; var a = 2; print a;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_variable_can_hold_a_string) {
  ASSERT_TRUE(run(utest_fixture, "var a = \"one\"; print a + \"two\";"));

  clox_value_t result = only_printed(utest_fixture);
  ASSERT_TRUE(CLOX_IS_STRING(result));
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(result));
}

UTEST_F(lox, a_variable_survives_into_the_next_run) {
  ASSERT_TRUE(run(utest_fixture, "var a = 1;"));
  ASSERT_EQ((size_t)0, utest_fixture->printed.count);

  clox_chunk_free(&utest_fixture->chunk);
  ASSERT_TRUE(run(utest_fixture, "print a + 1;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_local_holds_the_value_it_was_given) {
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1 + 2; print a; }"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, assignment_replaces_the_value_of_a_local) {
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; a = 2; print a; }"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, locals_declared_together_keep_their_own_values) {
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; var b = 2; print a; print b; }"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, an_inner_block_sees_the_enclosing_local) {
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; { print a; } }"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, an_inner_block_assigns_to_the_enclosing_local) {
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; { a = 2; } print a; }"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_local_shadows_a_global_of_the_same_name) {
  ASSERT_TRUE(run(utest_fixture, "var a = 1; { var a = 2; print a; } print a;"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
  // the global is untouched by the block that shadowed it
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_shadowed_local_comes_back_when_the_block_ends) {
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; { var a = 2; print a; } print a; }"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_local_does_not_outlive_its_block) {
  // nothing named a is left, so the read falls through to the globals
  EXPECT_FALSE(run(utest_fixture, "{ var a = 1; } print a;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "undefined variable") != NULL);
}

UTEST_F(lox, a_block_leaves_no_locals_behind_on_the_stack) {
  // each block's values are gone before the next statement starts, so a
  // sequence of them cannot pile up
  ASSERT_TRUE(run(utest_fixture,
                  "{ var a = 1; var b = 2; } { var c = 3; } { var d = 4; var e = 5; } print 6;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), only_printed(utest_fixture));
}

UTEST_F(lox, statements_around_a_block_run_in_order) {
  ASSERT_TRUE(run(utest_fixture, "print 1; { print 2; } print 3;"));

  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[2]);
}

UTEST_F(lox, an_empty_block_runs_and_prints_nothing) {
  ASSERT_TRUE(run(utest_fixture, "{ }"));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(lox, a_local_used_in_its_own_initializer_is_a_compile_error) {
  // unlike the global form, which compiles and fails at run time
  EXPECT_FALSE(run(utest_fixture, "{ var a = a; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(lox, declaring_a_local_twice_in_one_block_is_a_compile_error) {
  // unlike a global, which the later declaration simply replaces
  EXPECT_FALSE(run(utest_fixture, "{ var a = 1; var a = 2; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(lox, a_local_does_not_survive_into_the_next_run) {
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; }"));
  ASSERT_EQ((size_t)0, utest_fixture->printed.count);

  clox_chunk_free(&utest_fixture->chunk);
  EXPECT_FALSE(run(utest_fixture, "print a;"));
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_value_renders_the_way_the_repl_shows_it) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  ASSERT_TRUE(run(utest_fixture, "print 3 / 4;"));
  EXPECT_STREQ("0.75", clox_test_value_string(&buffer, only_printed(utest_fixture)));
}

UTEST_F(lox, a_syntax_error_stops_before_anything_runs) {
  EXPECT_FALSE(run(utest_fixture, "1 +"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_type_error_is_reported_at_run_time) {
  EXPECT_FALSE(run(utest_fixture, "print \"text\" - 1;"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)1, utest_fixture->errors.positions[0].line);
}

UTEST_F(lox, reading_an_undefined_variable_is_a_runtime_error) {
  EXPECT_FALSE(run(utest_fixture, "print missing;"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "missing") != NULL);
}

UTEST_F(lox, assigning_to_an_undefined_variable_is_a_runtime_error) {
  EXPECT_FALSE(run(utest_fixture, "a = 1;"));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(lox, a_variable_used_in_its_own_initializer_is_a_runtime_error) {
  // the name is defined only once the initializer has been evaluated
  EXPECT_FALSE(run(utest_fixture, "var a = a;"));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(lox, a_runtime_error_stops_the_statements_after_it) {
  EXPECT_FALSE(run(utest_fixture, "print 1; print b; print 2;"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(lox, a_syntax_error_in_one_statement_stops_every_statement) {
  EXPECT_FALSE(run(utest_fixture, "print 1; print ;"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_runtime_error_points_at_the_operator_not_the_operand) {
  EXPECT_FALSE(run(utest_fixture, "print 1 +\n2 *\n\"text\";"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)2, utest_fixture->errors.positions[0].line);
  EXPECT_EQ((size_t)3, utest_fixture->errors.positions[0].col);
}

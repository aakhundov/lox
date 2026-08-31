#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <utest.h>

#include "compiler.h"
#include "error.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#include "support/harness.h"

// The name a run is opened under, the way a file or a REPL prompt gives one
#define SOURCE_NAME "test.lox"
#define SOURCE_SIZE 256

// Source in, printed values and reported errors out: the two halves of the
// interpreter working together, as the REPL and a script file drive them.

struct lox {
  clox_allocator_t alloc;
  clox_compiler_t compiler;
  clox_vm_t vm;
  clox_function_t *script;
  clox_test_printed_t printed;
  clox_test_errors_t errors;
  char source[SOURCE_SIZE];
};

UTEST_F_SETUP(lox) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_compiler_init(&utest_fixture->compiler, &utest_fixture->alloc);
  clox_vm_init(&utest_fixture->vm, &utest_fixture->alloc);
  utest_fixture->script = NULL;
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
  clox_vm_free(&utest_fixture->vm);
  clox_compiler_free(&utest_fixture->compiler);
  clox_allocator_free(&utest_fixture->alloc);
}

static bool run(struct lox *fixture, const char *source) {
  (void)snprintf(fixture->source, SOURCE_SIZE, "%s", source);

  if (!clox_compile(&fixture->compiler, SOURCE_NAME, fixture->source, &fixture->script)) {
    return false;
  }

  return clox_interpret(&fixture->vm, fixture->script);
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
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; var b = 2; print a, b; }"));

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

UTEST_F(lox, a_print_reports_all_of_its_values_in_order) {
  ASSERT_TRUE(run(utest_fixture, "print 1, nil, false, 2;"));

  ASSERT_EQ((size_t)4, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NIL, utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[2]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[3]);
}

UTEST_F(lox, an_if_runs_its_then_branch_when_the_condition_holds) {
  ASSERT_TRUE(run(utest_fixture, "if (true) print 1; else print 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, an_if_runs_its_else_branch_when_the_condition_fails) {
  ASSERT_TRUE(run(utest_fixture, "if (false) print 1; else print 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, an_if_without_an_else_runs_nothing_when_its_condition_fails) {
  ASSERT_TRUE(run(utest_fixture, "if (false) print 1; print 2;"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, an_else_belongs_to_the_nearest_if) {
  ASSERT_TRUE(run(utest_fixture, "if (true) if (false) print 1; else print 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_condition_is_gone_from_the_stack_once_the_branch_is_decided) {
  // a run only returns on an empty stack, so a condition left behind by
  // either outcome would stop this before it printed
  ASSERT_TRUE(run(utest_fixture, "if (true) { } if (false) { } print 1;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, and_gives_back_the_left_operand_that_stopped_it) {
  ASSERT_TRUE(run(utest_fixture, "print nil and 2;"));
  EXPECT_VALUE_EQ(CLOX_NIL, only_printed(utest_fixture));
}

UTEST_F(lox, and_gives_back_its_right_operand_when_the_left_holds) {
  ASSERT_TRUE(run(utest_fixture, "print 1 and 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, or_gives_back_the_left_operand_that_stopped_it) {
  ASSERT_TRUE(run(utest_fixture, "print 1 or 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, or_gives_back_its_right_operand_when_the_left_is_falsy) {
  ASSERT_TRUE(run(utest_fixture, "print false or 2;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, and_never_reaches_its_right_operand_once_the_left_is_falsy) {
  // reading the undefined name would be a runtime error if it happened
  ASSERT_TRUE(run(utest_fixture, "print false and missing;"));

  EXPECT_VALUE_EQ(CLOX_BOOL(false), only_printed(utest_fixture));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(lox, or_never_reaches_its_right_operand_once_the_left_holds) {
  ASSERT_TRUE(run(utest_fixture, "print true or missing;"));

  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(lox, and_binds_tighter_than_or) {
  ASSERT_TRUE(run(utest_fixture, "print false or 1 and 2;"));
  // "false or (1 and 2)", not "(false or 1) and 2"
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_while_loop_repeats_until_its_condition_fails) {
  ASSERT_TRUE(run(utest_fixture, "var n = 0; while (n < 3) { print n; n = n + 1; } print n;"));

  ASSERT_EQ((size_t)4, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[2]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[3]);
}

UTEST_F(lox, a_while_loop_whose_condition_fails_first_never_runs) {
  ASSERT_TRUE(run(utest_fixture, "var n = 0; while (false) { n = 1; } print n;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_loop_body_declaring_locals_does_not_pile_them_up) {
  // more turns than the stack has room for, so a local left behind by any
  // one of them would overflow it before the loop ended
  ASSERT_TRUE(run(utest_fixture, "var n = 0; while (n < 1200) { var t = n; n = n + 1; } print n;"));

  EXPECT_VALUE_EQ(CLOX_NUMBER(1200.0), only_printed(utest_fixture));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(lox, a_for_loop_counts_from_its_initializer_up_to_its_condition) {
  ASSERT_TRUE(run(utest_fixture, "for (var i = 0; i < 3; i = i + 1) print i;"));

  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  // the first turn prints before the increment has run
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[2]);
}

UTEST_F(lox, a_for_loop_may_leave_its_clauses_out) {
  ASSERT_TRUE(run(utest_fixture, "var i = 0; for (; i < 2;) { print i; i = i + 1; }"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_for_loop_variable_does_not_outlive_the_loop) {
  // nothing named i is left, so the read falls through to the globals
  EXPECT_FALSE(run(utest_fixture, "for (var i = 0; i < 1; i = i + 1) print i; print i;"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), only_printed(utest_fixture));
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_for_loop_body_may_declare_locals_of_its_own) {
  ASSERT_TRUE(run(utest_fixture, "var s = 0; for (var i = 1; i < 4; i = i + 1) { var d = i * i;"
                                 " s = s + d; } print s;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(14.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_nested_loop_starts_over_on_every_turn_of_the_outer_one) {
  ASSERT_TRUE(run(utest_fixture, "for (var i = 0; i < 2; i = i + 1)"
                                 " for (var j = 0; j < 2; j = j + 1) print i, j;"));

  ASSERT_EQ((size_t)8, utest_fixture->printed.count);
  // the inner counter comes back to zero for the outer one's second turn
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[4]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[5]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[6]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[7]);
}

UTEST_F(lox, a_break_ends_the_loop_it_is_in) {
  ASSERT_TRUE(run(utest_fixture, "var n = 0; while (true) { n = n + 1;"
                                 " if (n == 3) break; } print n;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_break_leaves_the_rest_of_the_body_unrun) {
  ASSERT_TRUE(run(utest_fixture, "for (var i = 0; i < 3; i = i + 1)"
                                 " { if (i == 1) break; print i; }"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_continue_leaves_the_rest_of_the_body_unrun) {
  ASSERT_TRUE(run(utest_fixture, "for (var i = 0; i < 4; i = i + 1)"
                                 " { if (i == 1) continue; print i; }"));

  // the turn it skips is the only one missing; the loop runs to its end
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[2]);
}

UTEST_F(lox, a_continue_in_a_for_loop_still_runs_the_increment) {
  // going back to the condition instead would leave the counter standing
  // and the loop would never end
  ASSERT_TRUE(run(utest_fixture, "var n = 0; for (var i = 0; i < 3; i = i + 1)"
                                 " { n = n + 1; continue; } print n;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_break_leaves_only_the_innermost_loop) {
  ASSERT_TRUE(run(utest_fixture, "var n = 0; for (var i = 0; i < 3; i = i + 1)"
                                 " { for (var j = 0; j < 3; j = j + 1)"
                                 " { if (j == 1) break; n = n + 1; } } print n;"));

  // one turn of the inner loop survives on each of the outer loop's three
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_continue_repeats_only_the_innermost_loop) {
  ASSERT_TRUE(run(utest_fixture, "var n = 0; for (var i = 0; i < 2; i = i + 1)"
                                 " { for (var j = 0; j < 3; j = j + 1)"
                                 " { if (j == 0) continue; n = n + 1; }"
                                 " n = n + 10; } print n;"));

  // two inner turns count on each outer one, and the outer body goes on
  // past the inner loop both times
  EXPECT_VALUE_EQ(CLOX_NUMBER(24.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_later_break_leaves_the_loop_by_the_way_the_first_one_opened) {
  // the second break is the one that fires, and it reaches the way out
  // through the jump the first break left behind
  ASSERT_TRUE(run(utest_fixture, "var n = 0; while (true) { n = n + 1;"
                                 " if (n == 5) break; if (n == 2) break; } print n;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_continue_leaves_no_locals_behind_on_the_stack) {
  // more turns than the stack has room for, so a local the continue failed
  // to drop would overflow it before the loop ended
  ASSERT_TRUE(run(utest_fixture, "var n = 0; while (n < 1200) { var t = n;"
                                 " n = n + 1; continue; } print n;"));

  EXPECT_VALUE_EQ(CLOX_NUMBER(1200.0), only_printed(utest_fixture));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(lox, a_break_out_of_nested_blocks_leaves_the_stack_as_it_found_it) {
  // the break drops the two locals it leaves behind and no more, so the one
  // declared before the loop is still where the print expects it
  ASSERT_TRUE(run(utest_fixture, "{ var a = 1; while (true) { var b = 2;"
                                 " { var c = 3; break; } } print a; }"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_for_loop_variable_does_not_outlive_a_break) {
  // leaving by a break drops the variable the same way running out does
  EXPECT_FALSE(run(utest_fixture, "for (var i = 0;;) break; print i;"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_runtime_error_inside_a_loop_stops_it) {
  EXPECT_FALSE(run(utest_fixture, "var n = 0; while (n < 3) { print missing; }"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(lox, a_break_outside_a_loop_stops_before_anything_runs) {
  EXPECT_FALSE(run(utest_fixture, "print 1; break;"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_continue_outside_a_loop_stops_before_anything_runs) {
  EXPECT_FALSE(run(utest_fixture, "print 1; continue;"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  EXPECT_TRUE(utest_fixture->errors.count > 0);
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
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.line);
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
  EXPECT_EQ((size_t)2, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(lox, a_function_returns_the_value_it_names) {
  ASSERT_TRUE(run(utest_fixture, "fun answer() { return 42; } print answer();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_reaching_its_end_returns_nil) {
  ASSERT_TRUE(run(utest_fixture, "fun nothing() {} print nothing();"));
  EXPECT_VALUE_EQ(CLOX_NIL, only_printed(utest_fixture));
}

UTEST_F(lox, a_bare_return_yields_nil) {
  ASSERT_TRUE(run(utest_fixture, "fun nothing() { return; } print nothing();"));
  EXPECT_VALUE_EQ(CLOX_NIL, only_printed(utest_fixture));
}

UTEST_F(lox, arguments_reach_the_parameters_in_the_order_they_are_written) {
  ASSERT_TRUE(run(utest_fixture, "fun sub(a, b) { return a - b; } print sub(10, 4);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), only_printed(utest_fixture));
}

UTEST_F(lox, an_argument_is_an_expression_of_its_own) {
  ASSERT_TRUE(run(utest_fixture, "fun twice(n) { return n * 2; } print twice(1 + 2);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_call_is_an_expression_and_nests_in_another) {
  ASSERT_TRUE(run(utest_fixture, "fun twice(n) { return n * 2; } print twice(twice(3));"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(12.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_is_a_value_that_can_be_passed_around) {
  ASSERT_TRUE(run(utest_fixture, "fun answer() { return 42; } var f = answer; print f();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_can_be_handed_to_another_function) {
  ASSERT_TRUE(run(utest_fixture, "fun apply(f) { return f(); }"
                                 "fun answer() { return 42; }"
                                 "print apply(answer);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_prints_as_the_closure_it_reaches_a_value_as) {
  ASSERT_TRUE(run(utest_fixture, "fun named() {} print named;"));

  // a function only ever reaches the stack wrapped in a closure, so the value
  // a name stands for is that closure, and the function is under it
  clox_value_t printed = only_printed(utest_fixture);
  ASSERT_TRUE(CLOX_IS_CLOSURE(printed));
  EXPECT_STREQ("named", CLOX_AS_CLOSURE(printed)->function->name);
}

UTEST_F(lox, recursion_runs_down_to_its_base_case) {
  ASSERT_TRUE(run(utest_fixture,
                  "fun count(n) { if (n <= 0) { return 0; } return count(n - 1) + 1; }"
                  "print count(10);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(10.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_return_leaves_the_function_at_once) {
  ASSERT_TRUE(run(utest_fixture, "fun early() { return 1; print 2; } print early();"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_return_leaves_the_loop_it_stands_in) {
  ASSERT_TRUE(run(utest_fixture, "fun first_over(n) {"
                                 "  for (var i = 0; i < 100; i = i + 1) {"
                                 "    if (i > n) { return i; }"
                                 "  }"
                                 "  return -1;"
                                 "}"
                                 "print first_over(3);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(4.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_return_out_of_a_scope_leaves_nothing_behind) {
  // the locals of every scope the return jumps out of go with the frame
  ASSERT_TRUE(run(utest_fixture, "fun deep() { { var a = 1; { var b = 2; return a + b; } } }"
                                 "print deep(); print deep();"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_function_reads_the_globals_around_it) {
  ASSERT_TRUE(run(utest_fixture, "var g = 7; fun read() { return g; } print read();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_writes_the_globals_around_it) {
  ASSERT_TRUE(run(utest_fixture, "var g = 1; fun bump() { g = g + 1; } bump(); bump(); print g;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_parameter_shadows_a_global_of_the_same_name) {
  ASSERT_TRUE(run(utest_fixture, "var n = 1; fun show(n) { return n; } print show(2); print n;"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_functions_locals_do_not_reach_the_caller) {
  EXPECT_FALSE(run(utest_fixture, "fun hidden() { var secret = 1; } hidden(); print secret;"));
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, one_call_does_not_see_the_locals_of_another) {
  // both frames use the same slots, and neither reads what the other put there
  ASSERT_TRUE(run(utest_fixture, "fun counted(n) { var doubled = n * 2; return doubled; }"
                                 "print counted(1); print counted(5);"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(10.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_function_declared_in_a_block_is_gone_after_it) {
  EXPECT_FALSE(run(utest_fixture, "{ fun inner() { return 1; } print inner(); } print inner();"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(lox, a_nested_function_is_reached_through_the_one_around_it) {
  ASSERT_TRUE(run(utest_fixture, "fun outer() { fun inner() { return 5; } return inner() + 1; }"
                                 "print outer();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_reads_a_local_of_the_function_around_it) {
  ASSERT_TRUE(run(utest_fixture,
                  "fun outer() { var x = 7; fun inner() { return x; } return inner(); }"
                  "print outer();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_writes_a_local_of_the_function_around_it) {
  ASSERT_TRUE(run(utest_fixture, "fun outer() { var x = 1; fun bump() { x = x + 1; }"
                                 "  bump(); bump(); return x; }"
                                 "print outer();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_reads_a_parameter_of_the_function_around_it) {
  ASSERT_TRUE(run(utest_fixture, "fun outer(p) { fun inner() { return p * 2; } return inner(); }"
                                 "print outer(4);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(8.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_capture_reaches_through_a_function_that_does_not_name_it) {
  ASSERT_TRUE(run(utest_fixture, "fun outer() { var x = 1;"
                                 "  fun mid() { fun inner() { return x; } return inner; }"
                                 "  return mid(); }"
                                 "print outer()();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_returned_function_still_reads_what_it_captured) {
  // the local it reads is gone by the time it runs, and the call in between
  // has since stood on those very slots: the value has to have been moved off
  // the stack when the frame holding it returned
  ASSERT_TRUE(run(utest_fixture,
                  "fun outer() { var x = 5; fun inner() { return x; } return inner; }"
                  "var f = outer();"
                  "fun churn() { var p = 7; return p; } churn();"
                  "print f();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(5.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_closure_keeps_the_state_it_captured_between_calls) {
  ASSERT_TRUE(run(utest_fixture, "fun make() { var i = 0; fun count() { i = i + 1; return i; }"
                                 "  return count; }"
                                 "var c = make(); print c(); print c();"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, two_closures_from_two_calls_capture_two_separate_variables) {
  // the two calls stand on the same slots one after the other, so the second
  // would overwrite what the first captured had that not been closed off
  ASSERT_TRUE(run(utest_fixture, "fun make(n) { fun get() { return n; } return get; }"
                                 "var a = make(1); var b = make(2);"
                                 "print a(); print b();"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, two_counters_count_on_their_own) {
  ASSERT_TRUE(run(utest_fixture, "fun make() { var i = 0; fun count() { i = i + 1; return i; }"
                                 "  return count; }"
                                 "var a = make(); var b = make(); a(); print a(); print b();"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, two_closures_over_one_variable_see_each_others_writes) {
  ASSERT_TRUE(run(utest_fixture, "var get; var set;"
                                 "{ var x = 1; fun g() { return x; } fun s() { x = 9; }"
                                 "  get = g; set = s; }"
                                 "set(); print get();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(9.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_closure_outlives_the_block_its_capture_stood_in) {
  ASSERT_TRUE(run(utest_fixture, "var f;"
                                 "{ var x = 3; fun inner() { return x; } f = inner; }"
                                 "print f();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_local_declared_beside_a_captured_one_is_still_dropped) {
  // the scope closes the capture and pops the rest: getting either wrong
  // leaves the stack out of step, and the value read back would be another's
  ASSERT_TRUE(run(utest_fixture, "var f;"
                                 "{ var x = 3; var y = 4; fun inner() { return x; } f = inner; }"
                                 "print f();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_body_local_is_captured_afresh_on_every_turn_of_a_loop) {
  ASSERT_TRUE(run(utest_fixture, "var a; var b;"
                                 "for (var i = 0; i < 2; i = i + 1) { var j = i;"
                                 "  fun f() { return j; } if (i == 0) { a = f; } else { b = f; } }"
                                 "print a(); print b();"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, the_variable_a_for_loop_declares_is_one_slot_every_turn_shares) {
  // the loop variable is declared once, outside the body, so every closure
  // made in the body captures that one slot and reads what it last held
  ASSERT_TRUE(run(utest_fixture, "var a; var b;"
                                 "for (var i = 0; i < 2; i = i + 1) {"
                                 "  fun f() { return i; } if (i == 0) { a = f; } else { b = f; } }"
                                 "print a(); print b();"));

  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_break_out_of_a_loop_leaves_its_captures_readable) {
  // break pops the scope itself rather than running its end, so a capture
  // taken under it has to be closed on that path too
  ASSERT_TRUE(run(utest_fixture, "var f;"
                                 "while (true) { var x = 6; fun g() { return x; } f = g; break; }"
                                 "print f();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_continue_round_a_loop_leaves_its_captures_readable) {
  ASSERT_TRUE(run(utest_fixture, "var f;"
                                 "for (var i = 0; i < 2; i = i + 1) {"
                                 "  var x = i; fun g() { return x; } f = g; continue; }"
                                 "print f();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_local_function_calls_itself_by_its_own_name) {
  // the name is a local of the frame around it, so the recursive call reaches
  // it as a capture rather than as a global
  ASSERT_TRUE(run(utest_fixture,
                  "fun outer() {"
                  "  fun down(n) { if (n <= 0) { return 0; } return down(n - 1) + 1; }"
                  "  return down(4); }"
                  "print outer();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(4.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_closure_handed_to_a_function_keeps_what_it_captured) {
  ASSERT_TRUE(run(utest_fixture, "fun apply(f) { return f(); }"
                                 "fun make() { var x = 8; fun get() { return x; } return get; }"
                                 "print apply(make());"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(8.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_closure_survives_into_the_next_run) {
  // the capture is closed by the time the run ends, so nothing of it points
  // into the stack the next run starts over
  ASSERT_TRUE(run(utest_fixture, "var f;"
                                 "{ var x = 2; fun g() { x = x + 1; return x; } f = g; }"));
  ASSERT_EQ((size_t)0, utest_fixture->printed.count);

  ASSERT_TRUE(run(utest_fixture, "print f(); print f();"));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(4.0), utest_fixture->printed.values[1]);
}

UTEST_F(lox, a_closure_is_truthy) {
  ASSERT_TRUE(run(utest_fixture, "var f; { var x = 1; fun g() { return x; } f = g; }"
                                 "if (f) { print 1; } else { print 2; }"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_global_function_survives_into_the_next_run) {
  ASSERT_TRUE(run(utest_fixture, "fun answer() { return 42; }"));
  ASSERT_EQ((size_t)0, utest_fixture->printed.count);

  ASSERT_TRUE(run(utest_fixture, "print answer();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_global_can_be_redefined_like_any_other) {
  ASSERT_TRUE(run(utest_fixture, "fun which() { return 1; }"
                                 "fun which() { return 2; }"
                                 "print which();"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_function_is_truthy) {
  ASSERT_TRUE(run(utest_fixture, "fun f() {} if (f) { print 1; } else { print 2; }"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, too_few_arguments_is_a_runtime_error) {
  EXPECT_FALSE(run(utest_fixture, "fun two(a, b) { return a; } print two(1);"));

  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "expected 2") != NULL);
}

UTEST_F(lox, too_many_arguments_is_a_runtime_error) {
  EXPECT_FALSE(run(utest_fixture, "fun one(a) { return a; } print one(1, 2);"));

  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "expected 1") != NULL);
}

UTEST_F(lox, calling_something_that_is_not_a_function_is_a_runtime_error) {
  EXPECT_FALSE(run(utest_fixture, "var n = 1; n();"));

  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "call") != NULL);
}

UTEST_F(lox, calling_the_result_of_a_call_that_returned_nil_is_a_runtime_error) {
  EXPECT_FALSE(run(utest_fixture, "fun nothing() {} nothing()();"));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(lox, a_runtime_error_inside_a_function_stops_the_run) {
  EXPECT_FALSE(run(utest_fixture, "fun broken() { return -true; } print 1; print broken();"));

  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(lox, a_runtime_error_inside_a_function_points_into_its_body) {
  EXPECT_FALSE(run(utest_fixture, "fun broken() {\n  return -true;\n}\nprint broken();"));

  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)2, utest_fixture->errors.stacks[0][0].pos.line);
}

UTEST_F(lox, a_runtime_error_inside_nested_calls_is_traced_out_to_the_script) {
  EXPECT_FALSE(run(utest_fixture, "fun inner() {\n"
                                  "  return -true;\n"
                                  "}\n"
                                  "fun outer() {\n"
                                  "  return inner();\n"
                                  "}\n"
                                  "outer();"));

  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  ASSERT_EQ((size_t)3, utest_fixture->errors.stack_sizes[0]);

  // where it broke, then each call that led there, out to the script
  EXPECT_STREQ("inner", utest_fixture->errors.stacks[0][0].fn_name);
  EXPECT_EQ((size_t)2, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_STREQ("outer", utest_fixture->errors.stacks[0][1].fn_name);
  EXPECT_EQ((size_t)5, utest_fixture->errors.stacks[0][1].pos.line);
  EXPECT_STREQ(CLOX_SCRIPT_NAME, utest_fixture->errors.stacks[0][2].fn_name);
  EXPECT_EQ((size_t)7, utest_fixture->errors.stacks[0][2].pos.line);
}

UTEST_F(lox, every_frame_of_a_trace_names_the_source_it_was_compiled_from) {
  EXPECT_FALSE(run(utest_fixture, "fun broken() {\n  return -true;\n}\nbroken();"));

  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  ASSERT_EQ((size_t)2, utest_fixture->errors.stack_sizes[0]);
  for (size_t i = 0; i < utest_fixture->errors.stack_sizes[0]; i++) {
    ASSERT_STREQ(SOURCE_NAME, utest_fixture->errors.stacks[0][i].file_name);
    ASSERT_EQ((const char *)utest_fixture->source, utest_fixture->errors.stacks[0][i].source);
  }
}

UTEST_F(lox, recursion_without_a_base_case_is_a_runtime_error) {
  EXPECT_FALSE(run(utest_fixture, "fun forever() { return forever(); } forever();"));

  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "stack overflow") != NULL);
}

UTEST_F(lox, a_trace_of_runaway_recursion_is_cut_to_what_an_error_can_carry) {
  // the call stack is deeper than the frames an error has room for
  EXPECT_FALSE(run(utest_fixture, "fun forever() { return forever(); } forever();"));

  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)CLOX_MAX_ERROR_STACK_SIZE, utest_fixture->errors.stack_sizes[0]);
}

UTEST_F(lox, a_run_that_overflowed_the_frames_leaves_the_interpreter_usable) {
  ASSERT_FALSE(run(utest_fixture, "fun forever() { return forever(); } forever();"));

  utest_fixture->printed = (clox_test_printed_t){0};
  utest_fixture->errors = (clox_test_errors_t){0};
  ASSERT_TRUE(run(utest_fixture, "print 1;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_native_is_callable_by_its_name) {
  ASSERT_TRUE(run(utest_fixture, "print clock() >= 0;"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, a_native_prints_as_itself) {
  ASSERT_TRUE(run(utest_fixture, "print clock;"));

  clox_value_t printed = only_printed(utest_fixture);
  ASSERT_TRUE(CLOX_IS_NATIVE(printed));
  EXPECT_STREQ("clock", CLOX_AS_NATIVE(printed)->name);
}

UTEST_F(lox, a_native_is_a_global_like_any_other) {
  // nothing protects a built-in name from being redefined
  ASSERT_TRUE(run(utest_fixture, "var clock = 1; print clock;"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_native_called_with_the_wrong_argument_count_is_a_runtime_error) {
  ASSERT_FALSE(run(utest_fixture, "print sqrt();"));
  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "expected 1") != NULL);
}

UTEST_F(lox, a_native_given_the_wrong_type_reports_its_own_message) {
  ASSERT_FALSE(run(utest_fixture, "print sqrt(\"nine\");"));
  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "must be number") != NULL);
}

UTEST_F(lox, the_math_natives_compute_from_source) {
  ASSERT_TRUE(run(utest_fixture, "print sqrt(9) + pow(2, 3) + abs(-1) + floor(2.7) + ceil(0.1);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(15.0), only_printed(utest_fixture));
}

UTEST_F(lox, a_variadic_native_takes_any_number_of_arguments_from_source) {
  ASSERT_TRUE(run(utest_fixture, "print min(3, 1, 2) + max(4) + max(5, 6);"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(11.0), only_printed(utest_fixture));
}

UTEST_F(lox, the_string_natives_read_a_string_literal) {
  ASSERT_TRUE(run(utest_fixture, "print len(\"hello\") + ord(\"A\");"));
  EXPECT_VALUE_EQ(CLOX_NUMBER(70.0), only_printed(utest_fixture));
}

UTEST_F(lox, the_type_predicates_answer_about_a_value) {
  ASSERT_TRUE(run(utest_fixture, "print is_number(1) and is_string(\"s\") and is_nil(nil);"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, a_seeded_random_int_stays_inside_its_bound) {
  ASSERT_TRUE(run(utest_fixture, "seed(1); var n = random_int(4); print n >= 0 and n < 4;"));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), only_printed(utest_fixture));
}

UTEST_F(lox, exit_ends_the_run_as_a_runtime_error) {
  // there is no unwinding path, so halting is failing: the statement after it
  // never runs, and the VM is left to be torn down normally
  ASSERT_FALSE(run(utest_fixture, "print 1; exit(); print 2;"));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_STREQ("exited", utest_fixture->errors.messages[0]);
}

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#include "support/harness.h"

// one more value than the stack can hold
#define OVER_STACK (CLOX_STACK_SIZE + 1)

static const clox_pos_t POS = {.line = 1, .col = 1};

struct vm {
  clox_allocator_t alloc;
  clox_vm_t vm;
  clox_chunk_t chunk;
  clox_test_printed_t printed;
  clox_test_errors_t errors;
};

UTEST_F_SETUP(vm) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_vm_init(&utest_fixture->vm, &utest_fixture->alloc);
  clox_chunk_init(&utest_fixture->chunk);
  utest_fixture->printed = (clox_test_printed_t){0};
  utest_fixture->errors = (clox_test_errors_t){0};
  clox_vm_set_print_fn(&utest_fixture->vm, clox_test_print_fn, &utest_fixture->printed);
  clox_vm_set_error_handler(&utest_fixture->vm, clox_test_error_handler, &utest_fixture->errors);
}

UTEST_F_TEARDOWN(vm) {
  clox_vm_reset_error_handler(&utest_fixture->vm);
  clox_vm_set_default_print_fn(&utest_fixture->vm);
  clox_chunk_free(&utest_fixture->chunk);
  clox_vm_free(&utest_fixture->vm);
  clox_allocator_free(&utest_fixture->alloc);
}

static void emit(struct vm *fixture, clox_byte_t byte) {
  clox_chunk_write(&fixture->chunk, byte, POS);
}

static void emit_constant(struct vm *fixture, clox_value_t val) {
  (void)clox_write_constant(&fixture->chunk, val, POS);
}

static bool interpret(struct vm *fixture) {
  emit(fixture, OP_RETURN);

  return clox_interpret(&fixture->vm, &fixture->chunk);
}

UTEST_F(vm, an_empty_chunk_returns_without_printing) {
  EXPECT_TRUE(interpret(utest_fixture));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(vm, a_constant_reaches_the_print_seam) {
  emit_constant(utest_fixture, CLOX_NUMBER(42.0));

  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, the_literal_opcodes_push_their_values) {
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_FALSE);
  emit(utest_fixture, OP_TRUE);

  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  // the stack is printed from the top down
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_NIL, utest_fixture->printed.values[2]);
}

UTEST_F(vm, addition_leaves_a_single_result) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit(utest_fixture, OP_ADD);

  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(5.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, subtraction_takes_the_operands_in_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(10.0));
  emit_constant(utest_fixture, CLOX_NUMBER(4.0));
  emit(utest_fixture, OP_SUBTRACT);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, multiplication_and_division_chain_into_one_result) {
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_constant(utest_fixture, CLOX_NUMBER(4.0));
  emit(utest_fixture, OP_MULTIPLY);
  emit_constant(utest_fixture, CLOX_NUMBER(6.0));
  emit(utest_fixture, OP_DIVIDE);

  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, negation_flips_the_sign) {
  emit_constant(utest_fixture, CLOX_NUMBER(7.0));
  emit(utest_fixture, OP_NEGATE);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-7.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, not_yields_the_opposite_of_truthiness) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NOT);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
}

UTEST_F(vm, not_of_nil_is_true) {
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_NOT);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, less_compares_the_operands_in_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_LESS);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, greater_compares_the_operands_in_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_GREATER);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
}

UTEST_F(vm, greater_or_equal_admits_the_equal_case) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_GREATER_EQUAL);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_GREATER_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  // the stack is printed from the top down, so the later comparison comes first
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[1]);
}

UTEST_F(vm, less_or_equal_admits_the_equal_case) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_LESS_EQUAL);
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_LESS_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[1]);
}

UTEST_F(vm, equality_holds_between_equal_values) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, values_of_different_types_are_never_equal) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
}

UTEST_F(vm, inequality_is_the_negation_of_equality) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_NOT_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, adding_two_strings_joins_them) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "one", 3));
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "two", 3));
  emit(utest_fixture, OP_ADD);

  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);

  clox_value_t result = utest_fixture->printed.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(result));
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(result));
}

UTEST_F(vm, equal_strings_compare_equal) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "same", 4));
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "same", 4));
  emit(utest_fixture, OP_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, adding_a_number_to_a_string_is_a_runtime_error) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "text", 4));
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit(utest_fixture, OP_ADD);

  EXPECT_FALSE(interpret(utest_fixture));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strlen(utest_fixture->errors.messages[0]) > 0);
}

UTEST_F(vm, negating_a_non_number_is_a_runtime_error) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);

  EXPECT_FALSE(interpret(utest_fixture));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, comparing_a_non_number_is_a_runtime_error) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_LESS);

  EXPECT_FALSE(interpret(utest_fixture));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, nothing_is_printed_once_a_runtime_error_stops_the_run) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);

  EXPECT_FALSE(interpret(utest_fixture));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(vm, a_runtime_error_carries_the_position_of_its_instruction) {
  clox_chunk_t *chunk = &utest_fixture->chunk;
  clox_chunk_write(chunk, OP_TRUE, (clox_pos_t){.line = 2, .col = 1});
  clox_chunk_write(chunk, OP_NEGATE, (clox_pos_t){.line = 7, .col = 3});
  clox_chunk_write(chunk, OP_RETURN, POS);

  EXPECT_FALSE(clox_interpret(&utest_fixture->vm, chunk));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)7, utest_fixture->errors.positions[0].line);
  EXPECT_EQ((size_t)3, utest_fixture->errors.positions[0].col);
}

UTEST_F(vm, overflowing_the_stack_is_a_runtime_error) {
  for (size_t i = 0; i < OVER_STACK; i++) {
    emit(utest_fixture, OP_TRUE);
  }

  EXPECT_FALSE(interpret(utest_fixture));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, a_full_stack_is_not_an_error) {
  for (size_t i = 0; i < CLOX_STACK_SIZE; i++) {
    emit(utest_fixture, OP_TRUE);
  }

  EXPECT_TRUE(interpret(utest_fixture));
  EXPECT_EQ((size_t)CLOX_STACK_SIZE, utest_fixture->printed.count);
}

UTEST_F(vm, a_second_run_starts_from_an_empty_stack) {
  emit(utest_fixture, OP_TRUE);
  ASSERT_TRUE(interpret(utest_fixture));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, &utest_fixture->chunk));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
}

UTEST_F(vm, a_run_that_failed_leaves_the_vm_usable) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);
  ASSERT_FALSE(interpret(utest_fixture));

  clox_chunk_t next;
  clox_chunk_init(&next);
  clox_chunk_write(&next, OP_NIL, POS);
  clox_chunk_write(&next, OP_RETURN, POS);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, &next));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NIL, utest_fixture->printed.values[0]);

  clox_chunk_free(&next);
}

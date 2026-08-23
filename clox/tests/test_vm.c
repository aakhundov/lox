#include <limits.h>
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
// one more than a single-byte constant index can address
#define OVER_BYTE_INDEX 256
// A jump offset needing both of its bytes, measured in the units it spans:
// one constant and one print each, so an offset read wrongly lands among
// instructions that print and the count gives it away. 171 * 3 is 0x0201,
// whose bytes differ, so swapping them lands somewhere else again.
#define WIDE_JUMP_UNIT ((size_t)3)
#define WIDE_JUMP_UNITS ((size_t)171)
#define WIDE_JUMP_OFFSET (WIDE_JUMP_UNITS * WIDE_JUMP_UNIT)

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
  (void)clox_write_constant(&fixture->chunk, OP_CONSTANT, val, POS);
}

// Writes an instruction naming a global. The opcode is the short form of one
// of the global opcodes; the name is interned, so equal names are one key.
static void emit_global(struct vm *fixture, clox_op_code_t opcode, const char *name) {
  (void)clox_write_constant(&fixture->chunk, opcode,
                            CLOX_STRING_COPY(&fixture->alloc, name, strlen(name)), POS);
}

// Writes a jump instruction over the given offset, big-endian. The tests that
// name their offset here are what pins the encoding the three helpers below
// share with the compiler.
static void emit_jump_over(struct vm *fixture, clox_op_code_t opcode, size_t offset) {
  emit(fixture, (clox_byte_t)opcode);
  emit(fixture, (clox_byte_t)(offset >> CHAR_BIT));
  emit(fixture, (clox_byte_t)offset);
}

// Writes a jump whose offset patch_jump fills in, and returns where the two
// operand bytes stand.
static size_t emit_jump_forward(struct vm *fixture, clox_op_code_t opcode) {
  emit_jump_over(fixture, opcode, 0);

  return fixture->chunk.length - 2;
}

// Points a jump written earlier at the end of the code written so far.
static void patch_jump(struct vm *fixture, size_t operand) {
  size_t offset = fixture->chunk.length - operand - 2;
  fixture->chunk.code[operand] = (clox_byte_t)(offset >> CHAR_BIT);
  fixture->chunk.code[operand + 1] = (clox_byte_t)offset;
}

// Writes a loop instruction returning to target.
static void emit_loop_to(struct vm *fixture, size_t target) {
  emit(fixture, OP_LOOP);
  size_t offset = fixture->chunk.length - target + 2;
  emit(fixture, (clox_byte_t)(offset >> CHAR_BIT));
  emit(fixture, (clox_byte_t)offset);
}

// Prints the values the code under test is expected to leave behind, before
// returning on an empty stack. One print drains up to UCHAR_MAX values off the
// top and reports them in the order they were pushed, so a stack within that
// range arrives in push order. Anything deeper takes several prints, and those
// groups arrive top down.
static bool interpret(struct vm *fixture, size_t left_on_stack) {
  for (size_t remaining = left_on_stack; remaining > 0;) {
    size_t n = remaining < UCHAR_MAX ? remaining : UCHAR_MAX;
    if (n == 1) {
      emit(fixture, OP_PRINT);
    } else {
      emit(fixture, OP_PRINT_N);
      // cast is safe: n is capped at UCHAR_MAX above
      emit(fixture, (clox_byte_t)n);
    }
    remaining -= n;
  }
  emit(fixture, OP_RETURN);

  return clox_interpret(&fixture->vm, &fixture->chunk);
}

UTEST_F(vm, an_empty_chunk_returns_without_printing) {
  EXPECT_TRUE(interpret(utest_fixture, 0));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(vm, a_constant_reaches_the_print_seam) {
  emit_constant(utest_fixture, CLOX_NUMBER(42.0));

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, the_literal_opcodes_push_their_values) {
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_FALSE);
  emit(utest_fixture, OP_TRUE);

  ASSERT_TRUE(interpret(utest_fixture, 3));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NIL, utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[2]);
}

UTEST_F(vm, addition_leaves_a_single_result) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit(utest_fixture, OP_ADD);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(5.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, subtraction_takes_the_operands_in_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(10.0));
  emit_constant(utest_fixture, CLOX_NUMBER(4.0));
  emit(utest_fixture, OP_SUBTRACT);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, multiplication_and_division_chain_into_one_result) {
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_constant(utest_fixture, CLOX_NUMBER(4.0));
  emit(utest_fixture, OP_MULTIPLY);
  emit_constant(utest_fixture, CLOX_NUMBER(6.0));
  emit(utest_fixture, OP_DIVIDE);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, negation_flips_the_sign) {
  emit_constant(utest_fixture, CLOX_NUMBER(7.0));
  emit(utest_fixture, OP_NEGATE);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-7.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, not_yields_the_opposite_of_truthiness) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NOT);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
}

UTEST_F(vm, not_of_nil_is_true) {
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_NOT);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, less_compares_the_operands_in_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_LESS);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, greater_compares_the_operands_in_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_GREATER);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
}

UTEST_F(vm, greater_or_equal_admits_the_equal_case) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_GREATER_EQUAL);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_GREATER_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[1]);
}

UTEST_F(vm, less_or_equal_admits_the_equal_case) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_LESS_EQUAL);
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_LESS_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[1]);
}

UTEST_F(vm, equality_holds_between_equal_values) {
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, values_of_different_types_are_never_equal) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
}

UTEST_F(vm, inequality_is_the_negation_of_equality) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_NOT_EQUAL);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, adding_two_strings_joins_them) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "one", 3));
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "two", 3));
  emit(utest_fixture, OP_ADD);

  ASSERT_TRUE(interpret(utest_fixture, 1));
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

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, popping_discards_the_top_of_the_stack) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_POP);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_local_slot_is_read_onto_the_top_of_the_stack) {
  emit_constant(utest_fixture, CLOX_NUMBER(42.0)); // slot 0
  emit(utest_fixture, OP_GET_LOCAL);
  emit(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  // the slot, and the copy read from it onto the top
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, a_local_slot_is_read_by_its_own_index) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // slot 0
  emit_constant(utest_fixture, CLOX_NUMBER(2.0)); // slot 1
  emit(utest_fixture, OP_GET_LOCAL);
  emit(utest_fixture, 1);

  ASSERT_TRUE(interpret(utest_fixture, 3));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  // the copy on top came from slot 1, not slot 0
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[2]);
}

UTEST_F(vm, setting_a_local_slot_overwrites_it) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // slot 0
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_SET_LOCAL);
  emit(utest_fixture, 0);
  emit(utest_fixture, OP_POP); // the value the assignment left behind

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, setting_a_local_slot_leaves_the_value_on_the_stack) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // slot 0
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_SET_LOCAL);
  emit(utest_fixture, 0);

  // both the slot and the assignment's own value are still there
  ASSERT_TRUE(interpret(utest_fixture, 2));
  EXPECT_EQ((size_t)2, utest_fixture->printed.count);
}

UTEST_F(vm, a_counted_pop_discards_that_many_values) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_FALSE);
  emit(utest_fixture, OP_POP_N);
  emit(utest_fixture, 2);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_counted_pop_of_zero_leaves_the_stack_alone) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_POP_N);
  emit(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_counted_pop_clears_a_whole_byte_of_values) {
  for (size_t i = 0; i < UCHAR_MAX; i++) {
    emit(utest_fixture, OP_NIL);
  }
  emit(utest_fixture, OP_POP_N);
  emit(utest_fixture, UCHAR_MAX);

  // the run returns on an empty stack
  EXPECT_TRUE(interpret(utest_fixture, 0));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(vm, a_defined_global_reads_back_its_value) {
  emit_constant(utest_fixture, CLOX_NUMBER(42.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "answer");
  emit_global(utest_fixture, OP_GET_GLOBAL, "answer");

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, defining_a_global_takes_its_value_off_the_stack) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "a");

  // the run returns on an empty stack
  EXPECT_TRUE(interpret(utest_fixture, 0));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(vm, defining_a_global_twice_keeps_the_later_value) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "a");
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "a");
  emit_global(utest_fixture, OP_GET_GLOBAL, "a");

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, distinct_globals_hold_distinct_values) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "one");
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "two");
  emit_global(utest_fixture, OP_GET_GLOBAL, "one");
  emit_global(utest_fixture, OP_GET_GLOBAL, "two");

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, assigning_to_a_global_leaves_the_value_on_the_stack) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "a");
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_global(utest_fixture, OP_SET_GLOBAL, "a");

  // assignment is an expression: its value stays behind
  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, an_assigned_global_reads_back_the_new_value) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "a");
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_global(utest_fixture, OP_SET_GLOBAL, "a");
  emit(utest_fixture, OP_POP);
  emit_global(utest_fixture, OP_GET_GLOBAL, "a");

  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_global_named_by_a_long_index_still_resolves) {
  // push the global's name past the single-byte constant range
  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    emit_constant(utest_fixture, CLOX_NUMBER((double)i));
    emit(utest_fixture, OP_POP);
  }

  emit_constant(utest_fixture, CLOX_NUMBER(42.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "far");
  emit_global(utest_fixture, OP_GET_GLOBAL, "far");

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_global_outlives_the_run_that_defined_it) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "kept");
  ASSERT_TRUE(interpret(utest_fixture, 0));

  clox_chunk_t next;
  clox_chunk_init(&next);
  (void)clox_write_constant(&next, OP_GET_GLOBAL,
                            CLOX_STRING_COPY(&utest_fixture->alloc, "kept", 4), POS);
  clox_chunk_write(&next, OP_PRINT, POS);
  clox_chunk_write(&next, OP_RETURN, POS);

  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, &next));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);

  clox_chunk_free(&next);
}

UTEST_F(vm, a_counted_print_reports_its_values_in_push_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit(utest_fixture, OP_PRINT_N);
  emit(utest_fixture, 3);
  emit(utest_fixture, OP_RETURN);

  ASSERT_TRUE(clox_interpret(&utest_fixture->vm, &utest_fixture->chunk));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  // the deepest of the three comes first, not the one on top
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[2]);
}

UTEST_F(vm, a_counted_print_takes_only_the_values_it_counted) {
  emit_constant(utest_fixture, CLOX_NUMBER(9.0)); // stands below the print
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_PRINT_N);
  emit(utest_fixture, 2);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
  // what was underneath survived the counted print
  EXPECT_VALUE_EQ(CLOX_NUMBER(9.0), utest_fixture->printed.values[2]);
}

UTEST_F(vm, an_unconditional_jump_skips_the_instructions_it_spans) {
  emit_jump_over(utest_fixture, OP_JUMP, 3);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // skipped
  emit(utest_fixture, OP_PRINT);                  // skipped
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_false_condition_takes_the_popping_jump_and_loses_the_condition) {
  emit(utest_fixture, OP_FALSE);
  emit_jump_over(utest_fixture, OP_JUMP_FALSE_POP, 3);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // skipped
  emit(utest_fixture, OP_PRINT);                  // skipped
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));

  // the run ends on an empty stack, so the condition went with the jump
  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_true_condition_falls_through_the_popping_jump_and_still_loses_it) {
  emit(utest_fixture, OP_TRUE);
  emit_jump_over(utest_fixture, OP_JUMP_FALSE_POP, 3);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit(utest_fixture, OP_PRINT);
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, a_false_condition_takes_the_keeping_jump_and_stays_on_the_stack) {
  emit(utest_fixture, OP_FALSE);
  emit_jump_over(utest_fixture, OP_JUMP_FALSE, 3);
  emit(utest_fixture, OP_POP);                    // skipped
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // skipped

  // the condition itself is what the jump leaves behind to be printed
  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(false), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_true_condition_falls_through_the_keeping_jump) {
  emit(utest_fixture, OP_TRUE);
  emit_jump_over(utest_fixture, OP_JUMP_FALSE, 3);
  emit(utest_fixture, OP_POP); // drops the condition it fell past
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_true_condition_takes_the_true_jump_and_stays_on_the_stack) {
  emit(utest_fixture, OP_TRUE);
  emit_jump_over(utest_fixture, OP_JUMP_TRUE, 3);
  emit(utest_fixture, OP_POP);                    // skipped
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // skipped

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_false_condition_falls_through_the_true_jump) {
  emit(utest_fixture, OP_NIL);
  emit_jump_over(utest_fixture, OP_JUMP_TRUE, 3);
  emit(utest_fixture, OP_POP);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_jump_weighs_its_condition_the_way_the_language_does) {
  // zero is falsy here, as it is to OP_NOT, so the branch is taken
  emit_constant(utest_fixture, CLOX_NUMBER(0.0));
  emit_jump_over(utest_fixture, OP_JUMP_FALSE_POP, 3);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // skipped
  emit(utest_fixture, OP_PRINT);                  // skipped
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_jump_offset_wider_than_a_byte_is_read_big_endian) {
  emit_jump_over(utest_fixture, OP_JUMP, WIDE_JUMP_OFFSET);
  for (size_t i = 0; i < WIDE_JUMP_UNITS; i++) {
    emit_constant(utest_fixture, CLOX_NUMBER((double)i));
    emit(utest_fixture, OP_PRINT);
  }
  emit_constant(utest_fixture, CLOX_NUMBER(42.0));

  // an offset short by its high byte, or with its bytes the other way round,
  // lands inside the run above and prints its way to the end
  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_loop_returns_to_an_earlier_instruction) {
  // counts a global down to zero, which is the falsy value the loop stops on
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "n");

  size_t condition = utest_fixture->chunk.length;
  emit_global(utest_fixture, OP_GET_GLOBAL, "n");
  size_t exit_jump = emit_jump_forward(utest_fixture, OP_JUMP_FALSE_POP);
  emit_global(utest_fixture, OP_GET_GLOBAL, "n");
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit(utest_fixture, OP_SUBTRACT);
  emit_global(utest_fixture, OP_SET_GLOBAL, "n");
  emit(utest_fixture, OP_PRINT); // the assignment's own value
  emit_loop_to(utest_fixture, condition);
  patch_jump(utest_fixture, exit_jump);

  ASSERT_TRUE(interpret(utest_fixture, 0));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[1]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[2]);
}

UTEST_F(vm, reading_an_undefined_global_is_a_runtime_error) {
  emit_global(utest_fixture, OP_GET_GLOBAL, "missing");

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "missing") != NULL);
}

UTEST_F(vm, assigning_to_an_undefined_global_is_a_runtime_error) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_SET_GLOBAL, "missing");

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "missing") != NULL);
}

UTEST_F(vm, a_failed_assignment_leaves_the_global_undefined) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_global(utest_fixture, OP_SET_GLOBAL, "missing");
  ASSERT_FALSE(interpret(utest_fixture, 1));

  clox_chunk_t next;
  clox_chunk_init(&next);
  (void)clox_write_constant(&next, OP_GET_GLOBAL,
                            CLOX_STRING_COPY(&utest_fixture->alloc, "missing", 7), POS);
  clox_chunk_write(&next, OP_PRINT, POS);
  clox_chunk_write(&next, OP_RETURN, POS);

  utest_fixture->errors = (clox_test_errors_t){0};
  EXPECT_FALSE(clox_interpret(&utest_fixture->vm, &next));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);

  clox_chunk_free(&next);
}

UTEST_F(vm, adding_a_number_to_a_string_is_a_runtime_error) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  emit_constant(utest_fixture, CLOX_STRING_COPY(alloc, "text", 4));
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit(utest_fixture, OP_ADD);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strlen(utest_fixture->errors.messages[0]) > 0);
}

UTEST_F(vm, negating_a_non_number_is_a_runtime_error) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, comparing_a_non_number_is_a_runtime_error) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_LESS);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, nothing_is_printed_once_a_runtime_error_stops_the_run) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);

  EXPECT_FALSE(interpret(utest_fixture, 1));
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

  EXPECT_FALSE(interpret(utest_fixture, OVER_STACK));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, a_full_stack_is_not_an_error) {
  for (size_t i = 0; i < CLOX_STACK_SIZE; i++) {
    emit(utest_fixture, OP_TRUE);
  }

  EXPECT_TRUE(interpret(utest_fixture, CLOX_STACK_SIZE));
  EXPECT_EQ((size_t)CLOX_STACK_SIZE, utest_fixture->printed.count);
}

UTEST_F(vm, a_second_run_starts_from_an_empty_stack) {
  emit(utest_fixture, OP_TRUE);
  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, &utest_fixture->chunk));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
}

UTEST_F(vm, a_run_that_failed_leaves_the_vm_usable) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);
  ASSERT_FALSE(interpret(utest_fixture, 1));

  clox_chunk_t next;
  clox_chunk_init(&next);
  clox_chunk_write(&next, OP_NIL, POS);
  clox_chunk_write(&next, OP_PRINT, POS);
  clox_chunk_write(&next, OP_RETURN, POS);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, &next));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NIL, utest_fixture->printed.values[0]);

  clox_chunk_free(&next);
}

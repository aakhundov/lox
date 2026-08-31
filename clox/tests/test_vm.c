#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "error.h"
#include "library.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#include "support/harness.h"

// Slot 0 of a call frame holds the closure being run, so the first value a
// chunk of its own pushes stands in slot 1, and that slot is not room the
// chunk can use.
#define FIRST_SLOT 1
#define STACK_ROOM (CLOX_STACK_SIZE - FIRST_SLOT)
// one more value than the stack has room for
#define OVER_STACK (STACK_ROOM + 1)
// one more than a single-byte constant index can address
#define OVER_BYTE_INDEX 256
// A jump offset needing both of its bytes, measured in the units it spans:
// one constant and one print each, so an offset read wrongly lands among
// instructions that print and the count gives it away. 171 * 3 is 0x0201,
// whose bytes differ, so swapping them lands somewhere else again.
#define WIDE_JUMP_UNIT ((size_t)3)
#define WIDE_JUMP_UNITS ((size_t)171)
#define WIDE_JUMP_OFFSET (WIDE_JUMP_UNITS * WIDE_JUMP_UNIT)

// What every function a test builds is compiled under. The VM carries both
// into what it reports without ever reading through them, so the name and the
// text here are only what a reporter would be handed.
#define FILE_NAME "test.lox"
#define SOURCE ""

static const clox_pos_t POS = {.line = 1, .col = 1};

struct vm {
  clox_allocator_t alloc;
  clox_vm_t vm;
  clox_function_t *function;
  clox_test_printed_t printed;
  clox_test_errors_t errors;
};

UTEST_F_SETUP(vm) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_vm_init(&utest_fixture->vm, &utest_fixture->alloc);
  utest_fixture->function = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                              strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  utest_fixture->printed = (clox_test_printed_t){0};
  utest_fixture->errors = (clox_test_errors_t){0};
  clox_vm_set_print_fn(&utest_fixture->vm, clox_test_print_fn, &utest_fixture->printed);
  clox_vm_set_error_handler(&utest_fixture->vm, clox_test_error_handler, &utest_fixture->errors);
}

UTEST_F_TEARDOWN(vm) {
  clox_vm_reset_error_handler(&utest_fixture->vm);
  clox_vm_set_default_print_fn(&utest_fixture->vm);
  clox_vm_free(&utest_fixture->vm);
  clox_allocator_free(&utest_fixture->alloc);
}

static void emit(struct vm *fixture, clox_byte_t byte) {
  clox_chunk_write(&fixture->function->chunk, byte, POS);
}

static void emit_constant(struct vm *fixture, clox_value_t val) {
  (void)clox_write_constant(&fixture->function->chunk, OP_CONSTANT, val, POS);
}

// Writes an instruction naming a global. The opcode is the short form of one
// of the global opcodes; the name is interned, so equal names are one key.
static void emit_global(struct vm *fixture, clox_op_code_t opcode, const char *name) {
  (void)clox_write_constant(&fixture->function->chunk, opcode,
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

  return fixture->function->chunk.length - 2;
}

// Points a jump written earlier at the end of the code written so far.
static void patch_jump(struct vm *fixture, size_t operand) {
  size_t offset = fixture->function->chunk.length - operand - 2;
  fixture->function->chunk.code[operand] = (clox_byte_t)(offset >> CHAR_BIT);
  fixture->function->chunk.code[operand + 1] = (clox_byte_t)offset;
}

// Writes a loop instruction returning to target.
static void emit_loop_to(struct vm *fixture, size_t target) {
  emit(fixture, OP_LOOP);
  size_t offset = fixture->function->chunk.length - target + 2;
  emit(fixture, (clox_byte_t)(offset >> CHAR_BIT));
  emit(fixture, (clox_byte_t)offset);
}

// Prints the values the code under test is expected to leave behind, then
// returns nil off an otherwise empty stack. One print drains up to UCHAR_MAX values off the
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
  emit(fixture, OP_NIL); // the value OP_RETURN takes back
  emit(fixture, OP_RETURN);

  return clox_interpret(&fixture->vm, fixture->function);
}

// A callee with a chunk of its own. Everything the call tests need beyond the
// fixture's own function goes through the three helpers here, so a call is
// always a real second chunk and not the same one re-entered.
static clox_function_t *make_callee(struct vm *fixture, const char *name, size_t arity) {
  return clox_new_function(&fixture->alloc, name, strlen(name), arity, FILE_NAME, SOURCE);
}

static void emit_to(clox_function_t *callee, clox_byte_t byte) {
  clox_chunk_write(&callee->chunk, byte, POS);
}

static void emit_constant_to(clox_function_t *callee, clox_value_t val) {
  (void)clox_write_constant(&callee->chunk, OP_CONSTANT, val, POS);
}

// A callee as a call reaches it. Only a closure is callable, so a function
// standing alone is not what any of these tests can push: it is wrapped here,
// once per use, the way OP_CLOSURE would wrap it at run time.
static clox_value_t closure_of(struct vm *fixture, const clox_function_t *callee) {
  return CLOX_CLOSURE(&fixture->alloc, callee);
}

// Writes the callee onto the stack, ready for the arguments and the OP_CALL
// the caller writes next.
static void emit_callee(struct vm *fixture, const clox_function_t *callee) {
  emit_constant(fixture, closure_of(fixture, callee));
}

static void emit_call(struct vm *fixture, size_t arg_count) {
  emit(fixture, OP_CALL);
  emit(fixture, (clox_byte_t)arg_count);
}

// Writes an OP_CLOSURE naming callee, followed by the two operand bytes each
// upvalue it declares takes. The captures are handed in pairs -- is_local, then
// index -- so a caller lists them the way the compiler emits them.
static void emit_closure(struct vm *fixture, const clox_function_t *callee,
                         const clox_byte_t *captures) {
  (void)clox_write_constant(&fixture->function->chunk, OP_CLOSURE, CLOX_OBJECT(callee), POS);
  for (size_t i = 0; i < callee->upvalue_count; i++) {
    emit(fixture, captures[2 * i]);       // 1 for a local of this frame, 0 for an upvalue
    emit(fixture, captures[(2 * i) + 1]); // the index it is taken at
  }
}

// Native bodies for the tests. Each reports something about the call it got,
// so a test can tell what the VM handed over from what it did not.
static bool counting_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result) {
  (void)args;

  result->value = CLOX_NUMBER((double)arg_count);
  return true;
}

static bool first_arg_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result) {
  result->value = (arg_count == 0) ? CLOX_NIL : args[0];
  return true;
}

// A body that always fails, so a test can follow the message a native writes
// all the way out to the error handler.
static bool failing_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result) {
  (void)arg_count;
  (void)args;

  (void)snprintf(result->error_msg, sizeof(result->error_msg), "native said no");
  return false;
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
  emit_constant(utest_fixture, CLOX_NUMBER(42.0)); // slot 1
  emit(utest_fixture, OP_GET_LOCAL);
  emit(utest_fixture, 1);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  // the slot, and the copy read from it onto the top
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, a_local_slot_is_read_by_its_own_index) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // slot 1
  emit_constant(utest_fixture, CLOX_NUMBER(2.0)); // slot 2
  emit(utest_fixture, OP_GET_LOCAL);
  emit(utest_fixture, 2);

  ASSERT_TRUE(interpret(utest_fixture, 3));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  // the copy on top came from slot 2, not slot 1
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[2]);
}

UTEST_F(vm, setting_a_local_slot_overwrites_it) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // slot 1
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_SET_LOCAL);
  emit(utest_fixture, 1);
  emit(utest_fixture, OP_POP); // the value the assignment left behind

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, setting_a_local_slot_leaves_the_value_on_the_stack) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // slot 1
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit(utest_fixture, OP_SET_LOCAL);
  emit(utest_fixture, 1);

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

  clox_function_t *next = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                            strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  (void)clox_write_constant(&next->chunk, OP_GET_GLOBAL,
                            CLOX_STRING_COPY(&utest_fixture->alloc, "kept", 4), POS);
  clox_chunk_write(&next->chunk, OP_PRINT, POS);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_RETURN, POS);

  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, next));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_counted_print_reports_its_values_in_push_order) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit(utest_fixture, OP_PRINT_N);
  emit(utest_fixture, 3);
  emit(utest_fixture, OP_NIL);
  emit(utest_fixture, OP_RETURN);

  ASSERT_TRUE(clox_interpret(&utest_fixture->vm, utest_fixture->function));
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

  size_t condition = utest_fixture->function->chunk.length;
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

  clox_function_t *next = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                            strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  (void)clox_write_constant(&next->chunk, OP_GET_GLOBAL,
                            CLOX_STRING_COPY(&utest_fixture->alloc, "missing", 7), POS);
  clox_chunk_write(&next->chunk, OP_PRINT, POS);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_RETURN, POS);

  utest_fixture->errors = (clox_test_errors_t){0};
  EXPECT_FALSE(clox_interpret(&utest_fixture->vm, next));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
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
  clox_chunk_t *chunk = &utest_fixture->function->chunk;
  clox_chunk_write(chunk, OP_TRUE, (clox_pos_t){.line = 2, .col = 1});
  clox_chunk_write(chunk, OP_NEGATE, (clox_pos_t){.line = 7, .col = 3});
  clox_chunk_write(chunk, OP_NIL, POS);
  clox_chunk_write(chunk, OP_RETURN, POS);

  EXPECT_FALSE(clox_interpret(&utest_fixture->vm, utest_fixture->function));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)7, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(vm, overflowing_the_stack_is_a_runtime_error) {
  for (size_t i = 0; i < OVER_STACK; i++) {
    emit(utest_fixture, OP_TRUE);
  }

  EXPECT_FALSE(interpret(utest_fixture, OVER_STACK));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, a_full_stack_is_not_an_error) {
  for (size_t i = 0; i < STACK_ROOM; i++) {
    emit(utest_fixture, OP_TRUE);
  }

  EXPECT_TRUE(interpret(utest_fixture, STACK_ROOM));
  EXPECT_EQ((size_t)STACK_ROOM, utest_fixture->printed.count);
}

UTEST_F(vm, a_second_run_starts_from_an_empty_stack) {
  emit(utest_fixture, OP_TRUE);
  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, utest_fixture->function));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
}

UTEST_F(vm, a_run_that_failed_leaves_the_vm_usable) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);
  ASSERT_FALSE(interpret(utest_fixture, 1));

  clox_function_t *next = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                            strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_PRINT, POS);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_RETURN, POS);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, next));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NIL, utest_fixture->printed.values[0]);
}

UTEST_F(vm, calling_a_function_runs_the_chunk_it_carries) {
  clox_function_t *callee = make_callee(utest_fixture, "answer", 0);
  emit_constant_to(callee, CLOX_NUMBER(42.0));
  emit_to(callee, OP_RETURN);

  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_call_leaves_only_its_result_where_the_callee_stood) {
  clox_function_t *callee = make_callee(utest_fixture, "answer", 0);
  emit_constant_to(callee, CLOX_NUMBER(42.0));
  emit_to(callee, OP_RETURN);

  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);

  // one value, not the callee under it: interpret prints what is left and
  // OP_RETURN then asserts the stack drained
  ASSERT_TRUE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
}

UTEST_F(vm, a_function_reads_its_arguments_out_of_its_own_slots) {
  clox_function_t *callee = make_callee(utest_fixture, "sum", 2);
  emit_to(callee, OP_GET_LOCAL);
  emit_to(callee, 1); // first argument
  emit_to(callee, OP_GET_LOCAL);
  emit_to(callee, 2); // second argument
  emit_to(callee, OP_ADD);
  emit_to(callee, OP_RETURN);

  emit_callee(utest_fixture, callee);
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_constant(utest_fixture, CLOX_NUMBER(4.0));
  emit_call(utest_fixture, 2);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_slot_index_counts_from_the_frame_and_not_from_the_stack) {
  // the caller leaves values below the call, so a slot read against the stack
  // base rather than the frame would find one of these instead
  clox_function_t *callee = make_callee(utest_fixture, "first", 1);
  emit_to(callee, OP_GET_LOCAL);
  emit_to(callee, 1);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(111.0));
  emit_constant(utest_fixture, CLOX_NUMBER(222.0));
  emit_callee(utest_fixture, callee);
  emit_constant(utest_fixture, CLOX_NUMBER(9.0));
  emit_call(utest_fixture, 1);

  ASSERT_TRUE(interpret(utest_fixture, 3));
  ASSERT_EQ((size_t)3, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(9.0), utest_fixture->printed.values[2]);
}

UTEST_F(vm, a_function_writes_its_slots_without_reaching_the_caller) {
  clox_function_t *callee = make_callee(utest_fixture, "overwrite", 1);
  emit_constant_to(callee, CLOX_NUMBER(99.0));
  emit_to(callee, OP_SET_LOCAL);
  emit_to(callee, 1);
  emit_to(callee, OP_RETURN); // the assignment's own value

  emit_constant(utest_fixture, CLOX_NUMBER(111.0));
  emit_callee(utest_fixture, callee);
  emit_constant(utest_fixture, CLOX_NUMBER(9.0));
  emit_call(utest_fixture, 1);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  // the caller's value is untouched under the result
  EXPECT_VALUE_EQ(CLOX_NUMBER(111.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(99.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, returning_discards_whatever_the_callee_left_behind) {
  clox_function_t *callee = make_callee(utest_fixture, "messy", 0);
  emit_constant_to(callee, CLOX_NUMBER(1.0));
  emit_constant_to(callee, CLOX_NUMBER(2.0));
  emit_constant_to(callee, CLOX_NUMBER(3.0));
  emit_constant_to(callee, CLOX_NUMBER(42.0));
  emit_to(callee, OP_RETURN); // the three below it never reach the caller

  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, the_caller_resumes_at_the_instruction_after_the_call) {
  clox_function_t *callee = make_callee(utest_fixture, "answer", 0);
  emit_constant_to(callee, CLOX_NUMBER(1.0));
  emit_to(callee, OP_RETURN);

  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);
  emit_constant(utest_fixture, CLOX_NUMBER(2.0)); // after the call

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, one_function_called_twice_starts_from_the_top_each_time) {
  clox_function_t *callee = make_callee(utest_fixture, "answer", 0);
  emit_constant_to(callee, CLOX_NUMBER(7.0));
  emit_to(callee, OP_RETURN);

  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);
  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, a_call_inside_a_call_returns_through_both_frames) {
  clox_function_t *inner = make_callee(utest_fixture, "inner", 0);
  emit_constant_to(inner, CLOX_NUMBER(5.0));
  emit_to(inner, OP_RETURN);

  clox_function_t *outer = make_callee(utest_fixture, "outer", 0);
  emit_constant_to(outer, closure_of(utest_fixture, inner));
  emit_to(outer, OP_CALL);
  emit_to(outer, 0);
  emit_constant_to(outer, CLOX_NUMBER(1.0));
  emit_to(outer, OP_ADD);
  emit_to(outer, OP_RETURN);

  emit_callee(utest_fixture, outer);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(6.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, too_few_arguments_is_a_runtime_error) {
  clox_function_t *callee = make_callee(utest_fixture, "needs_two", 2);
  emit_to(callee, OP_NIL);
  emit_to(callee, OP_RETURN);

  emit_callee(utest_fixture, callee);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_call(utest_fixture, 1);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "expected 2") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "got 1") != NULL);
}

UTEST_F(vm, too_many_arguments_is_a_runtime_error) {
  clox_function_t *callee = make_callee(utest_fixture, "needs_none", 0);
  emit_to(callee, OP_NIL);
  emit_to(callee, OP_RETURN);

  emit_callee(utest_fixture, callee);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_call(utest_fixture, 1);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "expected 0") != NULL);
}

UTEST_F(vm, an_arity_error_points_at_the_call_and_not_at_the_callee) {
  clox_function_t *callee = make_callee(utest_fixture, "needs_one", 1);
  clox_chunk_write(&callee->chunk, OP_NIL, (clox_pos_t){.line = 9, .col = 9});
  clox_chunk_write(&callee->chunk, OP_RETURN, (clox_pos_t){.line = 9, .col = 9});

  emit_callee(utest_fixture, callee);
  clox_chunk_write(&utest_fixture->function->chunk, OP_CALL, (clox_pos_t){.line = 4, .col = 2});
  clox_chunk_write(&utest_fixture->function->chunk, 0, (clox_pos_t){.line = 4, .col = 2});

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)4, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)2, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(vm, a_runtime_error_inside_a_call_carries_the_callee_position) {
  clox_function_t *callee = make_callee(utest_fixture, "broken", 0);
  clox_chunk_write(&callee->chunk, OP_TRUE, (clox_pos_t){.line = 8, .col = 1});
  clox_chunk_write(&callee->chunk, OP_NEGATE, (clox_pos_t){.line = 8, .col = 5});

  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_EQ((size_t)8, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)5, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(vm, a_runtime_error_outside_any_call_reports_the_frame_it_stands_in) {
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  ASSERT_EQ((size_t)1, utest_fixture->errors.stack_sizes[0]);
  EXPECT_STREQ(CLOX_SCRIPT_NAME, utest_fixture->errors.stacks[0][0].fn_name);
}

UTEST_F(vm, a_reported_location_carries_what_the_function_was_compiled_under) {
  // the position says where, and the function it stands in says in which file
  // and in which text
  emit(utest_fixture, OP_TRUE);
  emit(utest_fixture, OP_NEGATE);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  ASSERT_EQ((size_t)1, utest_fixture->errors.stack_sizes[0]);
  EXPECT_STREQ(FILE_NAME, utest_fixture->errors.stacks[0][0].file_name);
  EXPECT_EQ((const char *)SOURCE, utest_fixture->errors.stacks[0][0].source);
}

UTEST_F(vm, a_runtime_error_inside_a_call_is_traced_out_through_its_caller) {
  clox_function_t *callee = make_callee(utest_fixture, "broken", 0);
  clox_chunk_write(&callee->chunk, OP_TRUE, (clox_pos_t){.line = 8, .col = 1});
  clox_chunk_write(&callee->chunk, OP_NEGATE, (clox_pos_t){.line = 8, .col = 5});

  emit_callee(utest_fixture, callee);
  clox_chunk_write(&utest_fixture->function->chunk, OP_CALL, (clox_pos_t){.line = 3, .col = 2});
  clox_chunk_write(&utest_fixture->function->chunk, 0, (clox_pos_t){.line = 3, .col = 2});

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  ASSERT_EQ((size_t)2, utest_fixture->errors.stack_sizes[0]);

  // innermost first: where it broke, then the call that led there
  EXPECT_STREQ("broken", utest_fixture->errors.stacks[0][0].fn_name);
  EXPECT_EQ((size_t)8, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)5, utest_fixture->errors.stacks[0][0].pos.col);
  EXPECT_STREQ(CLOX_SCRIPT_NAME, utest_fixture->errors.stacks[0][1].fn_name);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[0][1].pos.line);
  EXPECT_EQ((size_t)2, utest_fixture->errors.stacks[0][1].pos.col);
}

UTEST_F(vm, a_caller_frame_is_traced_at_its_call_and_not_at_what_it_ran_next) {
  clox_function_t *callee = make_callee(utest_fixture, "broken", 0);
  emit_to(callee, OP_TRUE);
  emit_to(callee, OP_NEGATE);

  // the caller runs on past the call once it returns, so the position traced
  // for it has to be the call it is suspended at and not where it resumes
  emit_callee(utest_fixture, callee);
  clox_chunk_write(&utest_fixture->function->chunk, OP_CALL, (clox_pos_t){.line = 3, .col = 2});
  clox_chunk_write(&utest_fixture->function->chunk, 0, (clox_pos_t){.line = 3, .col = 2});
  clox_chunk_write(&utest_fixture->function->chunk, OP_POP, (clox_pos_t){.line = 4, .col = 1});

  EXPECT_FALSE(interpret(utest_fixture, 0));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  ASSERT_EQ((size_t)2, utest_fixture->errors.stack_sizes[0]);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[0][1].pos.line);
  EXPECT_EQ((size_t)2, utest_fixture->errors.stacks[0][1].pos.col);
}

UTEST_F(vm, a_stack_deeper_than_an_error_can_carry_is_cut_to_its_innermost_frames) {
  // a function that calls itself: it overflows the call stack, which is
  // deeper than the frames an error has room for
  clox_function_t *loops = make_callee(utest_fixture, "loops", 0);
  emit_constant_to(loops, closure_of(utest_fixture, loops));
  emit_to(loops, OP_CALL);
  emit_to(loops, 0);
  emit_to(loops, OP_RETURN);

  emit_callee(utest_fixture, loops);
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  ASSERT_EQ((size_t)CLOX_MAX_ERROR_STACK_SIZE, utest_fixture->errors.stack_sizes[0]);

  // what is kept is the innermost slice, so the script that started
  // the run is among the frames left out
  for (size_t i = 0; i < CLOX_MAX_ERROR_STACK_SIZE; i++) {
    ASSERT_STREQ("loops", utest_fixture->errors.stacks[0][i].fn_name);
  }
}

UTEST_F(vm, calling_a_number_is_a_runtime_error) {
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "call") != NULL);
}

UTEST_F(vm, calling_a_string_is_a_runtime_error) {
  emit_constant(utest_fixture, CLOX_STRING_COPY(&utest_fixture->alloc, "text", 4));
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, calling_nil_is_a_runtime_error) {
  emit(utest_fixture, OP_NIL);
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, arguments_are_evaluated_before_the_callee_is_checked) {
  // the arguments stand above the callee, so a call that fails has already run
  // everything the caller wrote for it
  emit_constant(utest_fixture, CLOX_NUMBER(1.0)); // not callable
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_call(utest_fixture, 2);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(vm, nesting_calls_past_the_frame_limit_is_a_runtime_error) {
  // a function that calls itself through a global, which is the only way to
  // reach itself by name today
  clox_function_t *callee = make_callee(utest_fixture, "again", 0);
  (void)clox_write_constant(&callee->chunk, OP_GET_GLOBAL,
                            CLOX_STRING_COPY(&utest_fixture->alloc, "again", 5), POS);
  emit_to(callee, OP_CALL);
  emit_to(callee, 0);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, closure_of(utest_fixture, callee));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "again");
  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "stack overflow") != NULL);
}

UTEST_F(vm, a_run_that_overflowed_the_frames_leaves_the_vm_usable) {
  clox_function_t *callee = make_callee(utest_fixture, "again", 0);
  (void)clox_write_constant(&callee->chunk, OP_GET_GLOBAL,
                            CLOX_STRING_COPY(&utest_fixture->alloc, "again", 5), POS);
  emit_to(callee, OP_CALL);
  emit_to(callee, 0);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, closure_of(utest_fixture, callee));
  emit_global(utest_fixture, OP_DEF_GLOBAL, "again");
  emit_callee(utest_fixture, callee);
  emit_call(utest_fixture, 0);
  ASSERT_FALSE(interpret(utest_fixture, 1));

  // the frames the failed run left behind are not the next run's
  clox_function_t *next = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                            strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_PRINT, POS);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_RETURN, POS);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, next));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
}

UTEST_F(vm, a_closure_instruction_wraps_the_function_it_names) {
  clox_function_t *callee = make_callee(utest_fixture, "named", 0);
  emit_closure(utest_fixture, callee, NULL);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);

  // the constant is the function; what reaches the stack is a closure over it
  clox_value_t made = utest_fixture->printed.values[0];
  ASSERT_TRUE(CLOX_IS_CLOSURE(made));
  EXPECT_TRUE(CLOX_AS_CLOSURE(made)->function == callee);
}

UTEST_F(vm, one_function_closed_over_twice_gives_two_closures) {
  clox_function_t *callee = make_callee(utest_fixture, "named", 0);
  emit_closure(utest_fixture, callee, NULL);
  emit_closure(utest_fixture, callee, NULL);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);

  // each pass over the instruction makes its own closure, since each may
  // capture a different set of slots
  EXPECT_FALSE(
      clox_value_equals(utest_fixture->printed.values[0], utest_fixture->printed.values[1]));
}

UTEST_F(vm, a_closure_reads_a_captured_local_of_the_frame_it_was_made_in) {
  clox_function_t *callee = make_callee(utest_fixture, "reads", 0);
  callee->upvalue_count = 1;
  emit_to(callee, OP_GET_UPVALUE);
  emit_to(callee, 0);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(42.0)); // the script's slot 1
  const clox_byte_t captures[] = {1, FIRST_SLOT};  // a local, at that slot
  emit_closure(utest_fixture, callee, captures);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, an_open_upvalue_reads_the_slot_as_it_stands_when_the_closure_runs) {
  // the capture is taken when the closure is made, but it is a pointer into
  // the slot: what the closure reads is whatever the slot holds by then
  clox_function_t *callee = make_callee(utest_fixture, "reads", 0);
  callee->upvalue_count = 1;
  emit_to(callee, OP_GET_UPVALUE);
  emit_to(callee, 0);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  const clox_byte_t captures[] = {1, FIRST_SLOT};
  emit_closure(utest_fixture, callee, captures);

  emit_constant(utest_fixture, CLOX_NUMBER(2.0)); // written after the capture
  emit(utest_fixture, OP_SET_LOCAL);
  emit(utest_fixture, FIRST_SLOT);
  emit(utest_fixture, OP_POP); // the assignment's own value

  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, writing_through_an_upvalue_reaches_the_slot_it_closes_over) {
  clox_function_t *callee = make_callee(utest_fixture, "writes", 0);
  callee->upvalue_count = 1;
  emit_constant_to(callee, CLOX_NUMBER(99.0));
  emit_to(callee, OP_SET_UPVALUE);
  emit_to(callee, 0);
  emit_to(callee, OP_RETURN); // the assignment's own value

  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  const clox_byte_t captures[] = {1, FIRST_SLOT};
  emit_closure(utest_fixture, callee, captures);
  emit_call(utest_fixture, 0);
  emit(utest_fixture, OP_POP); // what the call returned

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  // the slot the script left behind carries what the callee wrote into it
  EXPECT_VALUE_EQ(CLOX_NUMBER(99.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, two_closures_over_one_slot_share_a_single_upvalue) {
  // A write through one has to be visible through the other. While the slot
  // still stands, two separate upvalues over it would alias it and hide the
  // difference; once it is closed, only a shared upvalue carries the write
  // across, since closing gives each upvalue a copy of its own.
  clox_function_t *writer = make_callee(utest_fixture, "writer", 0);
  writer->upvalue_count = 1;
  emit_constant_to(writer, CLOX_NUMBER(99.0));
  emit_to(writer, OP_SET_UPVALUE);
  emit_to(writer, 0);
  emit_to(writer, OP_RETURN);

  clox_function_t *reader = make_callee(utest_fixture, "reader", 0);
  reader->upvalue_count = 1;
  emit_to(reader, OP_GET_UPVALUE);
  emit_to(reader, 0);
  emit_to(reader, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  const clox_byte_t captures[] = {1, FIRST_SLOT};
  emit_closure(utest_fixture, writer, captures);
  emit_global(utest_fixture, OP_DEF_GLOBAL, "writer");
  emit_closure(utest_fixture, reader, captures);
  emit_global(utest_fixture, OP_DEF_GLOBAL, "reader");

  emit(utest_fixture, OP_CLOSE_UPVALUE); // the slot both captured is gone

  emit_global(utest_fixture, OP_GET_GLOBAL, "writer");
  emit_call(utest_fixture, 0);
  emit(utest_fixture, OP_POP);
  emit_global(utest_fixture, OP_GET_GLOBAL, "reader");
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(99.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, closing_an_upvalue_takes_the_value_off_the_slot_and_pops_it) {
  clox_function_t *callee = make_callee(utest_fixture, "reads", 0);
  callee->upvalue_count = 1;
  emit_to(callee, OP_GET_UPVALUE);
  emit_to(callee, 0);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(42.0));
  const clox_byte_t captures[] = {1, FIRST_SLOT};
  emit_closure(utest_fixture, callee, captures);

  // park the closure out of the way, close the slot, then call it back: what
  // it reads can no longer be the slot, which the close popped
  emit_global(utest_fixture, OP_DEF_GLOBAL, "kept");
  emit(utest_fixture, OP_CLOSE_UPVALUE);
  emit_global(utest_fixture, OP_GET_GLOBAL, "kept");
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_closure_outliving_the_frame_it_captured_keeps_reading_its_value) {
  // the maker returns a closure over its own slot, and that slot is gone the
  // moment it returns: the return has to close what it captured
  clox_function_t *inner = make_callee(utest_fixture, "inner", 0);
  inner->upvalue_count = 1;
  emit_to(inner, OP_GET_UPVALUE);
  emit_to(inner, 0);
  emit_to(inner, OP_RETURN);

  clox_function_t *maker = make_callee(utest_fixture, "maker", 0);
  emit_constant_to(maker, CLOX_NUMBER(7.0)); // the maker's own slot 1
  (void)clox_write_constant(&maker->chunk, OP_CLOSURE, CLOX_OBJECT(inner), POS);
  emit_to(maker, 1);          // a local
  emit_to(maker, FIRST_SLOT); // at that slot
  emit_to(maker, OP_RETURN);

  // a second call runs over the slots the first one left behind, so calling
  // what the maker handed back only reads 7 if the value was taken off the
  // stack when the frame that held it returned
  clox_function_t *churn = make_callee(utest_fixture, "churn", 0);
  emit_constant_to(churn, CLOX_NUMBER(77.0));
  emit_to(churn, OP_RETURN);

  emit_callee(utest_fixture, maker);
  emit_call(utest_fixture, 0);
  emit_global(utest_fixture, OP_DEF_GLOBAL, "kept");

  emit_callee(utest_fixture, churn);
  emit_call(utest_fixture, 0);
  emit(utest_fixture, OP_POP);

  emit_global(utest_fixture, OP_GET_GLOBAL, "kept");
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, two_calls_to_one_maker_capture_two_separate_slots) {
  // each call has slots of its own, so the closures they hand back must not
  // end up sharing the upvalue the first call opened
  clox_function_t *inner = make_callee(utest_fixture, "inner", 0);
  inner->upvalue_count = 1;
  emit_to(inner, OP_GET_UPVALUE);
  emit_to(inner, 0);
  emit_to(inner, OP_RETURN);

  clox_function_t *maker = make_callee(utest_fixture, "maker", 1);
  (void)clox_write_constant(&maker->chunk, OP_CLOSURE, CLOX_OBJECT(inner), POS);
  emit_to(maker, 1);
  emit_to(maker, FIRST_SLOT); // the parameter's slot
  emit_to(maker, OP_RETURN);

  // both are made before either is called: the two calls stand on the same
  // slots, so a capture left pointing at one of them would read the other's
  emit_callee(utest_fixture, maker);
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_call(utest_fixture, 1);
  emit_global(utest_fixture, OP_DEF_GLOBAL, "first");

  emit_callee(utest_fixture, maker);
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_call(utest_fixture, 1);
  emit_global(utest_fixture, OP_DEF_GLOBAL, "second");

  emit_global(utest_fixture, OP_GET_GLOBAL, "first");
  emit_call(utest_fixture, 0);
  emit_global(utest_fixture, OP_GET_GLOBAL, "second");
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->printed.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, a_capture_that_is_not_a_local_is_taken_from_the_enclosing_closure) {
  // three frames deep: the innermost names a slot it cannot see, so the middle
  // one has to carry the capture through as an upvalue of its own
  clox_function_t *innermost = make_callee(utest_fixture, "innermost", 0);
  innermost->upvalue_count = 1;
  emit_to(innermost, OP_GET_UPVALUE);
  emit_to(innermost, 0);
  emit_to(innermost, OP_RETURN);

  clox_function_t *middle = make_callee(utest_fixture, "middle", 0);
  middle->upvalue_count = 1;
  (void)clox_write_constant(&middle->chunk, OP_CLOSURE, CLOX_OBJECT(innermost), POS);
  emit_to(middle, 0); // not a local: an upvalue of this closure
  emit_to(middle, 0); // at index 0
  emit_to(middle, OP_CALL);
  emit_to(middle, 0);
  emit_to(middle, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(5.0));
  const clox_byte_t captures[] = {1, FIRST_SLOT};
  emit_closure(utest_fixture, middle, captures);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(5.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, an_upvalue_index_counts_within_the_closure_and_not_the_frame) {
  clox_function_t *callee = make_callee(utest_fixture, "reads", 0);
  callee->upvalue_count = 3;
  emit_to(callee, OP_GET_UPVALUE);
  emit_to(callee, 1); // the second capture, not the second slot
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(10.0));
  emit_constant(utest_fixture, CLOX_NUMBER(20.0));
  emit_constant(utest_fixture, CLOX_NUMBER(30.0));
  const clox_byte_t captures[] = {1, FIRST_SLOT, 1, FIRST_SLOT + 1, 1, FIRST_SLOT + 2};
  emit_closure(utest_fixture, callee, captures);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 4));
  ASSERT_EQ((size_t)4, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(20.0), utest_fixture->printed.values[3]);
}

// Pushes count distinct constants, so the one written next is addressed by a
// wider index, and drains them again. Returns nothing: what it leaves behind
// is a constant pool the caller's own constant no longer fits a byte of.
static void fill_constants(struct vm *fixture, size_t count) {
  for (size_t i = 0; i < count; i++) {
    emit_constant(fixture, CLOX_NUMBER((double)i));
  }
  for (size_t remaining = count; remaining > 0;) {
    size_t n = remaining < UCHAR_MAX ? remaining : UCHAR_MAX;
    emit(fixture, OP_POP_N);
    emit(fixture, (clox_byte_t)n);
    remaining -= n;
  }
}

UTEST_F(vm, a_closure_addressed_by_a_wider_index_reads_the_function_it_names) {
  // the long form carries a three-byte index: read as the short one it would
  // take some other constant for the function and step into its own operands
  clox_function_t *callee = make_callee(utest_fixture, "named", 0);
  emit_constant_to(callee, CLOX_NUMBER(42.0));
  emit_to(callee, OP_RETURN);

  fill_constants(utest_fixture, OVER_BYTE_INDEX);
  emit_closure(utest_fixture, callee, NULL);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_closure_addressed_by_a_wider_index_still_reads_its_captures) {
  // the capture operands follow the wider index, so a misread of it lands the
  // ip among them and the pair is taken from the wrong two bytes
  clox_function_t *callee = make_callee(utest_fixture, "reads", 0);
  callee->upvalue_count = 1;
  emit_to(callee, OP_GET_UPVALUE);
  emit_to(callee, 0);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, CLOX_NUMBER(42.0)); // the script's slot 1
  fill_constants(utest_fixture, OVER_BYTE_INDEX);
  const clox_byte_t captures[] = {1, FIRST_SLOT};
  emit_closure(utest_fixture, callee, captures);
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 2));
  ASSERT_EQ((size_t)2, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->printed.values[1]);
}

UTEST_F(vm, a_runtime_error_inside_a_closure_names_the_function_it_closed_over) {
  // the frame carries a closure now, and the name an error reports has to come
  // through it to the function underneath
  clox_function_t *callee = make_callee(utest_fixture, "broken", 0);
  clox_chunk_write(&callee->chunk, OP_TRUE, (clox_pos_t){.line = 8, .col = 1});
  clox_chunk_write(&callee->chunk, OP_NEGATE, (clox_pos_t){.line = 8, .col = 5});

  emit_closure(utest_fixture, callee, NULL);
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_STREQ("broken", utest_fixture->errors.stacks[0][0].fn_name);
}

UTEST_F(vm, a_function_that_is_not_closed_over_is_not_callable) {
  // only a closure is a callable object: a function reaching a call is a value
  // the compiler never emits, and the VM refuses it like any other
  clox_function_t *callee = make_callee(utest_fixture, "bare", 0);
  emit_to(callee, OP_NIL);
  emit_to(callee, OP_RETURN);

  emit_constant(utest_fixture, CLOX_OBJECT(callee));
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "call") != NULL);
}

UTEST_F(vm, a_run_that_failed_with_upvalues_open_leaves_the_vm_usable) {
  // the failed run leaves its open upvalues pointing into a stack the next run
  // reuses, so what it left behind must not carry over
  clox_function_t *callee = make_callee(utest_fixture, "broken", 0);
  callee->upvalue_count = 1;
  emit_to(callee, OP_TRUE);
  emit_to(callee, OP_NEGATE); // fails here, with the capture still open

  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  const clox_byte_t captures[] = {1, FIRST_SLOT};
  emit_closure(utest_fixture, callee, captures);
  emit_call(utest_fixture, 0);
  ASSERT_FALSE(interpret(utest_fixture, 1));

  clox_function_t *next = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                            strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_PRINT, POS);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_RETURN, POS);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, next));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
}

UTEST_F(vm, a_native_call_leaves_its_result_in_place_of_the_callee) {
  clox_vm_define_native(&utest_fixture->vm, "counting", 2, counting_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "counting");
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_call(utest_fixture, 2);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  // the count the native saw, standing where the callee and its args were
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_native_receives_its_arguments_in_push_order) {
  clox_vm_define_native(&utest_fixture->vm, "first_arg", 2, first_arg_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "first_arg");
  emit_constant(utest_fixture, CLOX_NUMBER(10.0));
  emit_constant(utest_fixture, CLOX_NUMBER(20.0));
  emit_call(utest_fixture, 2);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(10.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_native_called_with_too_few_arguments_is_a_runtime_error) {
  clox_vm_define_native(&utest_fixture->vm, "counting", 2, counting_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "counting");
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_call(utest_fixture, 1);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "expected 2") != NULL);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "got 1") != NULL);
}

UTEST_F(vm, a_native_called_with_too_many_arguments_is_a_runtime_error) {
  clox_vm_define_native(&utest_fixture->vm, "counting", 0, counting_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "counting");
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_call(utest_fixture, 1);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "expected 0") != NULL);
}

UTEST_F(vm, an_arity_mismatch_does_not_reach_the_native_body) {
  // counting_native would report a count of its own, so a printed value here
  // would mean the VM handed the call over before checking it
  clox_vm_define_native(&utest_fixture->vm, "counting", 2, counting_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "counting");
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(vm, a_variadic_native_takes_any_number_of_arguments) {
  clox_vm_define_native(&utest_fixture->vm, "counting", SIZE_MAX, counting_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "counting");
  emit_constant(utest_fixture, CLOX_NUMBER(1.0));
  emit_constant(utest_fixture, CLOX_NUMBER(2.0));
  emit_constant(utest_fixture, CLOX_NUMBER(3.0));
  emit_call(utest_fixture, 3);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  // the count the body saw, so the arguments reached it rather than being
  // counted by the VM alone
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_variadic_native_takes_no_arguments_at_all) {
  clox_vm_define_native(&utest_fixture->vm, "counting", SIZE_MAX, counting_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "counting");
  emit_call(utest_fixture, 0);

  ASSERT_TRUE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->printed.count);
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), utest_fixture->printed.values[0]);
}

UTEST_F(vm, a_native_that_fails_reports_the_message_it_wrote) {
  clox_vm_define_native(&utest_fixture->vm, "refusing", 0, failing_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "refusing");
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  ASSERT_EQ((size_t)1, utest_fixture->errors.count);
  EXPECT_STREQ("native said no", utest_fixture->errors.messages[0]);
}

UTEST_F(vm, a_native_that_fails_prints_nothing) {
  clox_vm_define_native(&utest_fixture->vm, "refusing", 0, failing_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "refusing");
  emit_call(utest_fixture, 0);

  EXPECT_FALSE(interpret(utest_fixture, 1));
  EXPECT_EQ((size_t)0, utest_fixture->printed.count);
}

UTEST_F(vm, a_run_that_a_native_failed_leaves_the_vm_usable) {
  clox_vm_define_native(&utest_fixture->vm, "refusing", 0, failing_native);

  emit_global(utest_fixture, OP_GET_GLOBAL, "refusing");
  emit_call(utest_fixture, 0);
  ASSERT_FALSE(interpret(utest_fixture, 1));

  // the callee and its arguments were popped before the error was reported,
  // so the next run starts on a stack the failed one did not leave behind
  clox_function_t *next = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                            strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_PRINT, POS);
  clox_chunk_write(&next->chunk, OP_NIL, POS);
  clox_chunk_write(&next->chunk, OP_RETURN, POS);

  utest_fixture->printed = (clox_test_printed_t){0};
  EXPECT_TRUE(clox_interpret(&utest_fixture->vm, next));
  EXPECT_EQ((size_t)1, utest_fixture->printed.count);
}

UTEST_F(vm, a_defined_native_is_a_global_holding_a_native_object) {
  clox_vm_define_native(&utest_fixture->vm, "counting", 2, counting_native);

  clox_value_t value;
  const clox_string_t *name = clox_test_intern(&utest_fixture->alloc, "counting");
  ASSERT_TRUE(clox_table_get(&utest_fixture->vm.globals, name, &value));

  ASSERT_TRUE(CLOX_IS_NATIVE(value));
  EXPECT_STREQ("counting", CLOX_AS_NATIVE(value)->name);
}

UTEST_F(vm, a_fresh_vm_defines_every_library_native_as_a_global) {
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    clox_value_t value;
    const clox_string_t *name = clox_test_intern(&utest_fixture->alloc, clox_library_fns[i].name);
    ASSERT_TRUE(clox_table_get(&utest_fixture->vm.globals, name, &value));
    ASSERT_TRUE(CLOX_IS_NATIVE(value));
    EXPECT_TRUE(CLOX_AS_NATIVE(value)->function == clox_library_fns[i].fn);
    EXPECT_EQ(clox_library_fns[i].arity, CLOX_AS_NATIVE(value)->arity);
  }
}

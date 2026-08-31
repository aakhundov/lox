#include <stdint.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "memory.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

// What a function is compiled under. A function carries both for a reporter to
// name and quote the code it holds; nothing here reports an error, so the text
// is empty and is never read through.
#define FILE_NAME "test.lox"
#define SOURCE ""

struct object {
  clox_allocator_t alloc;
};

// A body for the native tests. It returns its argument count, so a test can
// tell an invocation that reached this function from one that did not.
static bool counting_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result) {
  (void)args;

  result->value = CLOX_NUMBER((double)arg_count);
  return true;
}

// A second body, distinguishable from the first by its result.
static bool nil_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result) {
  (void)arg_count;
  (void)args;

  result->value = CLOX_NIL;
  return true;
}

UTEST_F_SETUP(object) {
  clox_allocator_init(&utest_fixture->alloc);
}

UTEST_F_TEARDOWN(object) {
  clox_allocator_free(&utest_fixture->alloc);
}

UTEST_F(object, copy_makes_a_string_of_the_given_content) {
  clox_value_t value = clox_test_string_kept(&utest_fixture->alloc, "text", 4);

  ASSERT_TRUE(CLOX_IS_STRING(value));
  EXPECT_EQ((size_t)4, CLOX_AS_STRING(value)->length);
  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, copy_takes_only_the_length_it_is_given) {
  clox_value_t value = clox_test_string_kept(&utest_fixture->alloc, "textual", 4);

  EXPECT_EQ((size_t)4, CLOX_AS_STRING(value)->length);
  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, copy_does_not_keep_the_source_buffer) {
  char source[] = "text";

  clox_value_t value = clox_test_string_kept(&utest_fixture->alloc, source, 4);
  source[0] = 'n';

  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, equal_content_is_one_interned_object) {
  clox_value_t first = clox_test_string_kept(&utest_fixture->alloc, "same", 4);
  clox_value_t second = clox_test_string_kept(&utest_fixture->alloc, "same", 4);

  EXPECT_EQ(CLOX_AS_OBJECT(first), CLOX_AS_OBJECT(second));
}

UTEST_F(object, different_content_is_a_different_object) {
  clox_value_t first = clox_test_string_kept(&utest_fixture->alloc, "one", 3);
  clox_value_t second = clox_test_string_kept(&utest_fixture->alloc, "two", 3);

  EXPECT_NE(CLOX_AS_OBJECT(first), CLOX_AS_OBJECT(second));
}

UTEST_F(object, interned_strings_share_their_hash) {
  clox_value_t first = clox_test_string_kept(&utest_fixture->alloc, "same", 4);
  clox_value_t second = clox_test_string_kept(&utest_fixture->alloc, "same", 4);

  EXPECT_EQ(CLOX_AS_STRING(first)->hash, CLOX_AS_STRING(second)->hash);
}

UTEST_F(object, move_adopts_the_buffer_it_is_given) {
  // the buffer is the allocator's to release from here on, so it is the
  // allocator that has to hand it out: a block it never counted cannot be
  // subtracted from its running total when the string is freed
  char *buffer = CLOX_ARRAY_ALLOCATE(&utest_fixture->alloc, char, 5);
  ASSERT_TRUE(buffer != NULL);
  memcpy(buffer, "text", 5);

  clox_value_t value = CLOX_STRING_MOVE(&utest_fixture->alloc, buffer, 4);

  ASSERT_TRUE(CLOX_IS_STRING(value));
  EXPECT_EQ((size_t)4, CLOX_AS_STRING(value)->length);
  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, move_of_content_already_interned_yields_the_existing_object) {
  clox_value_t copied = clox_test_string_kept(&utest_fixture->alloc, "same", 4);

  char *buffer = CLOX_ARRAY_ALLOCATE(&utest_fixture->alloc, char, 5);
  ASSERT_TRUE(buffer != NULL);
  memcpy(buffer, "same", 5);

  // the buffer is the allocator's to release: LSan reports it if it is not
  clox_value_t moved = CLOX_STRING_MOVE(&utest_fixture->alloc, buffer, 4);

  EXPECT_EQ(CLOX_AS_OBJECT(copied), CLOX_AS_OBJECT(moved));
}

UTEST_F(object, concat_joins_its_operands_in_order) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t joined = clox_string_concat(alloc, clox_test_string_kept(alloc, "one", 3),
                                           clox_test_string_kept(alloc, "two", 3));

  ASSERT_TRUE(CLOX_IS_STRING(joined));
  EXPECT_EQ((size_t)6, CLOX_AS_STRING(joined)->length);
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(joined));
}

UTEST_F(object, concat_with_an_empty_string_yields_the_other_content) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t joined = clox_string_concat(alloc, clox_test_string_kept(alloc, "", 0),
                                           clox_test_string_kept(alloc, "text", 4));

  EXPECT_STREQ("text", CLOX_AS_CSTRING(joined));
}

UTEST_F(object, a_concat_result_is_interned_like_any_other_string) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t joined = clox_string_concat(alloc, clox_test_string_kept(alloc, "one", 3),
                                           clox_test_string_kept(alloc, "two", 3));

  EXPECT_EQ(CLOX_AS_OBJECT(joined), CLOX_AS_OBJECT(clox_test_string_kept(alloc, "onetwo", 6)));
}

UTEST_F(object, objects_are_equal_when_their_content_is) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t first = clox_test_string_kept(alloc, "same", 4);
  clox_value_t second = clox_test_string_kept(alloc, "same", 4);
  clox_value_t other = clox_test_string_kept(alloc, "other", 5);

  EXPECT_TRUE(clox_object_equals(first, second));
  EXPECT_FALSE(clox_object_equals(first, other));
}

UTEST_F(object, a_string_renders_as_its_characters) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = clox_test_string_kept(&utest_fixture->alloc, "text", 4);

  EXPECT_STREQ("text", clox_test_value_string(&buffer, value));
}

UTEST_F(object, a_string_reprs_in_quotes) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = clox_test_string_kept(&utest_fixture->alloc, "text", 4);

  EXPECT_STREQ("\"text\"", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, an_empty_string_reprs_as_a_bare_pair_of_quotes) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = clox_test_string_kept(&utest_fixture->alloc, "", 0);

  EXPECT_STREQ("\"\"", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, a_function_carries_the_name_it_is_given) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  ASSERT_TRUE(function->name != NULL);
  EXPECT_STREQ("named", function->name);
}

UTEST_F(object, a_function_takes_only_the_name_length_it_is_given) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "namedly", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  EXPECT_STREQ("named", function->name);
}

UTEST_F(object, a_function_does_not_keep_the_name_buffer) {
  char source[] = "named";

  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, source, 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);
  source[0] = 'f';

  EXPECT_STREQ("named", function->name);
}

UTEST_F(object, the_script_is_a_function_named_like_one) {
  // the script has no name of its own in the source, so it is given the one
  // standing name every part of the interpreter recognises it by
  clox_function_t *script = clox_new_function(&utest_fixture->alloc, CLOX_SCRIPT_NAME,
                                              strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, script);

  EXPECT_STREQ(CLOX_SCRIPT_NAME, script->name);
}

UTEST_F(object, a_function_of_no_name_at_all_still_owns_an_empty_one) {
  // a name is duplicated whatever its length, so no function carries a null
  // one and whoever reports a name need not check for it
  clox_function_t *function = clox_new_function(&utest_fixture->alloc, "", 0, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  ASSERT_TRUE(function->name != NULL);
  EXPECT_STREQ("", function->name);
}

UTEST_F(object, a_function_starts_out_capturing_nothing) {
  // the count is raised by the compiler as it resolves upvalues, and it is
  // what sizes the array a closure over this function allocates
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  EXPECT_EQ((size_t)0, function->upvalue_count);
}

UTEST_F(object, a_function_carries_the_file_name_and_source_it_is_given) {
  // a position holds a line and a column only, so it is the function that says
  // which file and which text those count in
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  EXPECT_STREQ(FILE_NAME, function->file_name);
  EXPECT_EQ((const char *)SOURCE, function->source);
}

UTEST_F(object, a_function_does_not_copy_the_file_name_or_the_source) {
  // unlike its name, neither is owned: both are borrowed from whoever compiled
  // the function and outlive it, so the buffers are kept as they were handed in
  char file_name[] = "other.lox";
  char source[] = "1 + 2";

  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, file_name, source);
  clox_test_keep(&utest_fixture->alloc, function);

  EXPECT_EQ((const char *)file_name, function->file_name);
  EXPECT_EQ((const char *)source, function->source);
}

UTEST_F(object, a_function_keeps_the_arity_it_is_given) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "three", 5, 3, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  EXPECT_EQ((size_t)3, function->arity);
}

UTEST_F(object, a_new_function_starts_with_an_empty_chunk) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "empty", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  EXPECT_EQ((size_t)0, function->chunk.length);
  EXPECT_EQ((size_t)0, function->chunk.constants.length);
}

UTEST_F(object, a_function_owns_what_is_written_into_its_chunk) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "body", 4, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  // the chunk goes with the object: LSan reports it if it does not
  clox_chunk_write(&function->chunk, OP_NIL, (clox_pos_t){.line = 1, .col = 1});
  clox_chunk_write(&function->chunk, OP_RETURN, (clox_pos_t){.line = 1, .col = 1});

  EXPECT_EQ((size_t)2, function->chunk.length);
}

UTEST_F(object, functions_are_not_interned_the_way_strings_are) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  // two declarations of one name are two functions, however alike
  clox_function_t *first = clox_new_function(alloc, "same", 4, 0, FILE_NAME, SOURCE);
  clox_test_keep(alloc, first);
  clox_function_t *second = clox_new_function(alloc, "same", 4, 0, FILE_NAME, SOURCE);
  clox_test_keep(alloc, second);

  EXPECT_NE(first, second);
}

UTEST_F(object, a_function_is_equal_only_to_itself) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t first = CLOX_OBJECT(clox_new_function(alloc, "same", 4, 0, FILE_NAME, SOURCE));
  clox_test_keep(alloc, CLOX_AS_OBJECT(first));
  clox_value_t second = CLOX_OBJECT(clox_new_function(alloc, "same", 4, 0, FILE_NAME, SOURCE));
  clox_test_keep(alloc, CLOX_AS_OBJECT(second));

  EXPECT_TRUE(clox_object_equals(first, first));
  EXPECT_FALSE(clox_object_equals(first, second));
}

UTEST_F(object, a_function_renders_as_its_name) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value =
      CLOX_OBJECT(clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  EXPECT_STREQ("<fn named>", clox_test_value_string(&buffer, value));
}

UTEST_F(object, the_script_renders_as_itself_and_not_as_a_named_function) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_OBJECT(clox_new_function(
      &utest_fixture->alloc, CLOX_SCRIPT_NAME, strlen(CLOX_SCRIPT_NAME), 0, FILE_NAME, SOURCE));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  // the script's name is a rendering already: it takes no "<fn ...>" around it
  EXPECT_STREQ(CLOX_SCRIPT_NAME, clox_test_value_string(&buffer, value));
}

UTEST_F(object, a_function_reprs_the_way_it_renders) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value =
      CLOX_OBJECT(clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  // unlike a string, a function has no quoted form to fall back on
  EXPECT_STREQ("<fn named>", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, a_function_is_truthy) {
  clox_value_t value =
      CLOX_OBJECT(clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  EXPECT_TRUE(clox_value_is_truthy(value));
}

UTEST_F(object, a_native_carries_the_name_the_arity_and_the_body_it_is_given) {
  clox_native_t *native = clox_new_native(&utest_fixture->alloc, "counting", 2, counting_native);
  clox_test_keep(&utest_fixture->alloc, native);

  EXPECT_STREQ("counting", native->name);
  EXPECT_EQ((size_t)2, native->arity);
  ASSERT_TRUE(native->function == counting_native);

  clox_native_result_t result;
  ASSERT_TRUE(native->function(2, NULL, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), result.value);
}

UTEST_F(object, a_native_carries_a_variadic_arity_like_any_other) {
  // SIZE_MAX is the arity the VM reads as "any number of arguments", so the
  // object has to hold it unchanged rather than treat it as a count
  clox_native_t *native = clox_new_native(&utest_fixture->alloc, "any", SIZE_MAX, counting_native);
  clox_test_keep(&utest_fixture->alloc, native);

  EXPECT_EQ(SIZE_MAX, native->arity);
}

UTEST_F(object, a_native_does_not_keep_the_name_buffer) {
  char source[] = "named";

  clox_native_t *native = clox_new_native(&utest_fixture->alloc, source, 0, nil_native);
  clox_test_keep(&utest_fixture->alloc, native);
  source[0] = 'f';

  EXPECT_STREQ("named", native->name);
}

UTEST_F(object, a_native_renders_as_its_name) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_NATIVE(&utest_fixture->alloc, "counting", 2, counting_native);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  EXPECT_STREQ("<nt counting>", clox_test_value_string(&buffer, value));
  EXPECT_STREQ("<nt counting>", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, a_native_is_equal_only_to_itself) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t first = CLOX_NATIVE(alloc, "same", 2, counting_native);
  clox_test_keep(alloc, CLOX_AS_OBJECT(first));
  clox_value_t second = CLOX_NATIVE(alloc, "same", 2, counting_native);
  clox_test_keep(alloc, CLOX_AS_OBJECT(second));

  EXPECT_TRUE(clox_object_equals(first, first));
  EXPECT_FALSE(clox_object_equals(first, second));
}

UTEST_F(object, a_native_is_truthy) {
  clox_value_t value = CLOX_NATIVE(&utest_fixture->alloc, "counting", 2, counting_native);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  EXPECT_TRUE(clox_value_is_truthy(value));
}

UTEST_F(object, an_upvalue_points_at_the_location_it_is_given) {
  clox_value_t slot = CLOX_NUMBER(1.0);
  clox_upvalue_t *upvalue = clox_new_upvalue(&utest_fixture->alloc, &slot);
  clox_test_keep(&utest_fixture->alloc, upvalue);

  // while open, an upvalue reads straight through to the slot it closes over,
  // so a write to that slot is what the upvalue reads back
  ASSERT_TRUE(upvalue->location == &slot);
  slot = CLOX_NUMBER(2.0);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), *upvalue->location);
}

UTEST_F(object, an_upvalue_starts_open_and_unlinked) {
  clox_value_t slot = CLOX_NUMBER(1.0);
  clox_upvalue_t *upvalue = clox_new_upvalue(&utest_fixture->alloc, &slot);
  clox_test_keep(&utest_fixture->alloc, upvalue);

  // closed is where the value goes once the slot is gone; until then it is
  // untouched, and next is set only when the VM links the upvalue into its list
  EXPECT_VALUE_EQ(CLOX_NIL, upvalue->closed);
  EXPECT_TRUE(upvalue->next == NULL);
}

UTEST_F(object, an_upvalue_renders_with_the_value_it_stands_for) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t slot = CLOX_NUMBER(1.0);
  clox_value_t value = CLOX_UPVALUE(&utest_fixture->alloc, &slot);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  // the location is an address, so only what surrounds it can be pinned: an
  // upvalue over a number renders that number and not an object of some kind
  const char *rendered = clox_test_value_string(&buffer, value);
  EXPECT_TRUE(strncmp("<up ", rendered, 4) == 0);
  EXPECT_TRUE(strstr(rendered, "(1)") != NULL);
}

UTEST_F(object, an_upvalue_is_equal_only_to_itself) {
  clox_value_t slot = CLOX_NUMBER(1.0);

  // two upvalues over one slot are still two objects: the VM keeps them
  // unique by searching its open list, not by comparing them
  clox_value_t first = CLOX_UPVALUE(&utest_fixture->alloc, &slot);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(first));
  clox_value_t second = CLOX_UPVALUE(&utest_fixture->alloc, &slot);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(second));

  EXPECT_TRUE(clox_object_equals(first, first));
  EXPECT_FALSE(clox_object_equals(first, second));
}

UTEST_F(object, an_upvalue_is_truthy) {
  clox_value_t slot = CLOX_NIL;
  clox_value_t value = CLOX_UPVALUE(&utest_fixture->alloc, &slot);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  // truthiness is the upvalue's own, not that of the falsy value it holds
  EXPECT_TRUE(clox_value_is_truthy(value));
}

UTEST_F(object, a_closure_carries_the_function_it_wraps) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 2, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);
  clox_closure_t *closure = clox_new_closure(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, closure);

  // the function is shared, not copied: one function may be closed over many
  // times, each closure carrying different captures of it
  EXPECT_TRUE(closure->function == function);
}

UTEST_F(object, a_closure_takes_a_slot_per_upvalue_the_function_declares) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);
  function->upvalue_count = 3;

  clox_closure_t *closure = clox_new_closure(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, closure);

  ASSERT_EQ((size_t)3, closure->upvalue_count);
  ASSERT_TRUE(closure->upvalues != NULL);
  // the slots stand empty until the VM fills them from the OP_CLOSURE operands
  for (size_t i = 0; i < closure->upvalue_count; i++) {
    EXPECT_TRUE(closure->upvalues[i] == NULL);
  }
}

UTEST_F(object, a_closure_over_a_function_capturing_nothing_takes_no_slots) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);
  clox_closure_t *closure = clox_new_closure(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, closure);

  EXPECT_EQ((size_t)0, closure->upvalue_count);
}

UTEST_F(object, a_closure_renders_as_the_name_of_its_function) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);
  clox_value_t value = CLOX_CLOSURE(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  EXPECT_STREQ("<cl named>", clox_test_value_string(&buffer, value));
  EXPECT_STREQ("<cl named>", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, a_closure_is_equal_only_to_itself) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "same", 4, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  // two closures over one function are two objects, since they may yet capture
  // different variables: identity is all that can be compared
  clox_value_t first = CLOX_CLOSURE(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(first));
  clox_value_t second = CLOX_CLOSURE(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(second));

  EXPECT_TRUE(clox_object_equals(first, first));
  EXPECT_FALSE(clox_object_equals(first, second));
}

UTEST_F(object, a_closure_is_not_equal_to_the_function_it_wraps) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "same", 4, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);

  clox_value_t closure = CLOX_CLOSURE(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(closure));

  EXPECT_FALSE(clox_value_equals(closure, CLOX_OBJECT(function)));
}

UTEST_F(object, a_closure_is_truthy) {
  clox_function_t *function =
      clox_new_function(&utest_fixture->alloc, "named", 5, 0, FILE_NAME, SOURCE);
  clox_test_keep(&utest_fixture->alloc, function);
  clox_value_t value = CLOX_CLOSURE(&utest_fixture->alloc, function);
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(value));

  EXPECT_TRUE(clox_value_is_truthy(value));
}

UTEST_F(object, no_two_object_types_are_ever_equal_to_each_other) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_value_t slot = CLOX_NIL;

  clox_value_t string = clox_test_string_kept(alloc, "same", 4);
  clox_function_t *fn = clox_new_function(alloc, "same", 4, 0, FILE_NAME, SOURCE);
  clox_test_keep(alloc, fn);
  clox_value_t function = CLOX_OBJECT(fn);
  clox_value_t native = CLOX_NATIVE(alloc, "same", 2, counting_native);
  clox_test_keep(alloc, CLOX_AS_OBJECT(native));
  clox_value_t upvalue = CLOX_UPVALUE(alloc, &slot);
  clox_test_keep(alloc, CLOX_AS_OBJECT(upvalue));
  clox_value_t closure = CLOX_CLOSURE(alloc, fn);
  clox_test_keep(alloc, CLOX_AS_OBJECT(closure));

  // every pair of them shares a name, so only the type tells them apart
  const clox_value_t values[] = {string, function, native, upvalue, closure};
  const size_t count = sizeof(values) / sizeof(*values);
  for (size_t i = 0; i < count; i++) {
    for (size_t j = 0; j < count; j++) {
      EXPECT_TRUE(clox_value_equals(values[i], values[j]) == (i == j));
    }
  }
}

UTEST(object_lifetime, freeing_the_allocator_releases_every_string) {
  // LSan is the check: nothing here is freed by hand
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  for (size_t i = 0; i < 32; i++) {
    (void)clox_test_intern_indexed(&alloc, i);
  }
  clox_value_t joined = clox_string_concat(&alloc, clox_test_string_kept(&alloc, "one", 3),
                                           clox_test_string_kept(&alloc, "two", 3));
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(joined));

  clox_allocator_free(&alloc);
}

UTEST(object_lifetime, freeing_the_allocator_releases_functions_and_natives) {
  // LSan is the check again: a function owns a name and a chunk, and both go
  // with it. The chunk is grown past its first allocation on purpose.
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  for (size_t i = 0; i < 32; i++) {
    clox_function_t *function = clox_new_function(&alloc, "named", 5, 0, FILE_NAME, SOURCE);
    clox_test_keep(&alloc, function);
    for (size_t j = 0; j < 64; j++) {
      clox_chunk_write(&function->chunk, OP_NIL, (clox_pos_t){.line = 1, .col = 1});
    }
    (void)clox_write_constant(&function->chunk, OP_CONSTANT, clox_test_string_kept(&alloc, "k", 1),
                              (clox_pos_t){.line = 1, .col = 1});
    (void)clox_new_native(&alloc, "counting", 2, counting_native);
  }
  // a name of no length is still a buffer of its own to release
  (void)clox_new_function(&alloc, "", 0, 0, FILE_NAME, SOURCE);

  clox_allocator_free(&alloc);
}

UTEST(object_lifetime, freeing_the_allocator_releases_closures_and_upvalues) {
  // LSan again: a closure owns the array of pointers it holds its captures in,
  // and that array is a second allocation the object has to take with it. The
  // upvalues those pointers reach are not the closure's to free -- several
  // closures may share one -- so they are released as objects in their own right.
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  clox_value_t slot = CLOX_NUMBER(1.0);
  for (size_t i = 0; i < 32; i++) {
    clox_function_t *function = clox_new_function(&alloc, "named", 5, 0, FILE_NAME, SOURCE);
    clox_test_keep(&alloc, function);
    function->upvalue_count = 4;

    clox_closure_t *closure = clox_new_closure(&alloc, function);
    clox_test_keep(&alloc, closure);
    for (size_t j = 0; j < closure->upvalue_count; j++) {
      closure->upvalues[j] = clox_new_upvalue(&alloc, &slot);
    }
  }
  // a closure capturing nothing allocates no array, and is freed all the same
  (void)clox_new_closure(&alloc, clox_new_function(&alloc, "bare", 4, 0, FILE_NAME, SOURCE));

  clox_allocator_free(&alloc);
}

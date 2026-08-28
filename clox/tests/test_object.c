#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "common.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

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
  clox_value_t value = CLOX_STRING_COPY(&utest_fixture->alloc, "text", 4);

  ASSERT_TRUE(CLOX_IS_STRING(value));
  EXPECT_EQ((size_t)4, CLOX_AS_STRING(value)->length);
  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, copy_takes_only_the_length_it_is_given) {
  clox_value_t value = CLOX_STRING_COPY(&utest_fixture->alloc, "textual", 4);

  EXPECT_EQ((size_t)4, CLOX_AS_STRING(value)->length);
  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, copy_does_not_keep_the_source_buffer) {
  char source[] = "text";

  clox_value_t value = CLOX_STRING_COPY(&utest_fixture->alloc, source, 4);
  source[0] = 'n';

  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, equal_content_is_one_interned_object) {
  clox_value_t first = CLOX_STRING_COPY(&utest_fixture->alloc, "same", 4);
  clox_value_t second = CLOX_STRING_COPY(&utest_fixture->alloc, "same", 4);

  EXPECT_EQ(CLOX_AS_OBJECT(first), CLOX_AS_OBJECT(second));
}

UTEST_F(object, different_content_is_a_different_object) {
  clox_value_t first = CLOX_STRING_COPY(&utest_fixture->alloc, "one", 3);
  clox_value_t second = CLOX_STRING_COPY(&utest_fixture->alloc, "two", 3);

  EXPECT_NE(CLOX_AS_OBJECT(first), CLOX_AS_OBJECT(second));
}

UTEST_F(object, interned_strings_share_their_hash) {
  clox_value_t first = CLOX_STRING_COPY(&utest_fixture->alloc, "same", 4);
  clox_value_t second = CLOX_STRING_COPY(&utest_fixture->alloc, "same", 4);

  EXPECT_EQ(CLOX_AS_STRING(first)->hash, CLOX_AS_STRING(second)->hash);
}

UTEST_F(object, move_adopts_the_buffer_it_is_given) {
  char *buffer = malloc(5); // owned by the allocator from here on
  ASSERT_TRUE(buffer != NULL);
  memcpy(buffer, "text", 5);

  clox_value_t value = CLOX_STRING_MOVE(&utest_fixture->alloc, buffer, 4);

  ASSERT_TRUE(CLOX_IS_STRING(value));
  EXPECT_EQ((size_t)4, CLOX_AS_STRING(value)->length);
  EXPECT_STREQ("text", CLOX_AS_CSTRING(value));
}

UTEST_F(object, move_of_content_already_interned_yields_the_existing_object) {
  clox_value_t copied = CLOX_STRING_COPY(&utest_fixture->alloc, "same", 4);

  char *buffer = malloc(5);
  ASSERT_TRUE(buffer != NULL);
  memcpy(buffer, "same", 5);

  // the buffer is the allocator's to release: LSan reports it if it is not
  clox_value_t moved = CLOX_STRING_MOVE(&utest_fixture->alloc, buffer, 4);

  EXPECT_EQ(CLOX_AS_OBJECT(copied), CLOX_AS_OBJECT(moved));
}

UTEST_F(object, concat_joins_its_operands_in_order) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t joined = clox_string_concat(alloc, CLOX_STRING_COPY(alloc, "one", 3),
                                           CLOX_STRING_COPY(alloc, "two", 3));

  ASSERT_TRUE(CLOX_IS_STRING(joined));
  EXPECT_EQ((size_t)6, CLOX_AS_STRING(joined)->length);
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(joined));
}

UTEST_F(object, concat_with_an_empty_string_yields_the_other_content) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t joined =
      clox_string_concat(alloc, CLOX_STRING_COPY(alloc, "", 0), CLOX_STRING_COPY(alloc, "text", 4));

  EXPECT_STREQ("text", CLOX_AS_CSTRING(joined));
}

UTEST_F(object, a_concat_result_is_interned_like_any_other_string) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t joined = clox_string_concat(alloc, CLOX_STRING_COPY(alloc, "one", 3),
                                           CLOX_STRING_COPY(alloc, "two", 3));

  EXPECT_EQ(CLOX_AS_OBJECT(joined), CLOX_AS_OBJECT(CLOX_STRING_COPY(alloc, "onetwo", 6)));
}

UTEST_F(object, objects_are_equal_when_their_content_is) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t first = CLOX_STRING_COPY(alloc, "same", 4);
  clox_value_t second = CLOX_STRING_COPY(alloc, "same", 4);
  clox_value_t other = CLOX_STRING_COPY(alloc, "other", 5);

  EXPECT_TRUE(clox_object_equals(first, second));
  EXPECT_FALSE(clox_object_equals(first, other));
}

UTEST_F(object, a_string_renders_as_its_characters) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_STRING_COPY(&utest_fixture->alloc, "text", 4);

  EXPECT_STREQ("text", clox_test_value_string(&buffer, value));
}

UTEST_F(object, a_string_reprs_in_quotes) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_STRING_COPY(&utest_fixture->alloc, "text", 4);

  EXPECT_STREQ("\"text\"", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, an_empty_string_reprs_as_a_bare_pair_of_quotes) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_STRING_COPY(&utest_fixture->alloc, "", 0);

  EXPECT_STREQ("\"\"", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, a_function_carries_the_name_it_is_given) {
  clox_function_t *function = clox_new_function(&utest_fixture->alloc, "named", 5, 0);

  ASSERT_TRUE(function->name != NULL);
  EXPECT_STREQ("named", function->name);
}

UTEST_F(object, a_function_takes_only_the_name_length_it_is_given) {
  clox_function_t *function = clox_new_function(&utest_fixture->alloc, "namedly", 5, 0);

  EXPECT_STREQ("named", function->name);
}

UTEST_F(object, a_function_does_not_keep_the_name_buffer) {
  char source[] = "named";

  clox_function_t *function = clox_new_function(&utest_fixture->alloc, source, 5, 0);
  source[0] = 'f';

  EXPECT_STREQ("named", function->name);
}

UTEST_F(object, a_function_without_a_name_is_the_script) {
  clox_function_t *script = clox_new_function(&utest_fixture->alloc, NULL, 0, 0);

  EXPECT_TRUE(script->name == NULL);
}

UTEST_F(object, a_function_keeps_the_arity_it_is_given) {
  clox_function_t *function = clox_new_function(&utest_fixture->alloc, "three", 5, 3);

  EXPECT_EQ((size_t)3, function->arity);
}

UTEST_F(object, a_new_function_starts_with_an_empty_chunk) {
  clox_function_t *function = clox_new_function(&utest_fixture->alloc, "empty", 5, 0);

  EXPECT_EQ((size_t)0, function->chunk.length);
  EXPECT_EQ((size_t)0, function->chunk.constants.length);
}

UTEST_F(object, a_function_owns_what_is_written_into_its_chunk) {
  clox_function_t *function = clox_new_function(&utest_fixture->alloc, "body", 4, 0);

  // the chunk goes with the object: LSan reports it if it does not
  clox_chunk_write(&function->chunk, OP_NIL, (clox_pos_t){.line = 1, .col = 1});
  clox_chunk_write(&function->chunk, OP_RETURN, (clox_pos_t){.line = 1, .col = 1});

  EXPECT_EQ((size_t)2, function->chunk.length);
}

UTEST_F(object, functions_are_not_interned_the_way_strings_are) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  // two declarations of one name are two functions, however alike
  clox_function_t *first = clox_new_function(alloc, "same", 4, 0);
  clox_function_t *second = clox_new_function(alloc, "same", 4, 0);

  EXPECT_NE(first, second);
}

UTEST_F(object, a_function_is_equal_only_to_itself) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t first = CLOX_OBJECT(clox_new_function(alloc, "same", 4, 0));
  clox_value_t second = CLOX_OBJECT(clox_new_function(alloc, "same", 4, 0));

  EXPECT_TRUE(clox_object_equals(first, first));
  EXPECT_FALSE(clox_object_equals(first, second));
}

UTEST_F(object, a_function_renders_as_its_name) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_OBJECT(clox_new_function(&utest_fixture->alloc, "named", 5, 0));

  EXPECT_STREQ("<fn named>", clox_test_value_string(&buffer, value));
}

UTEST_F(object, a_function_without_a_name_renders_as_the_script) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_OBJECT(clox_new_function(&utest_fixture->alloc, NULL, 0, 0));

  EXPECT_STREQ("<script>", clox_test_value_string(&buffer, value));
}

UTEST_F(object, a_function_reprs_the_way_it_renders) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_OBJECT(clox_new_function(&utest_fixture->alloc, "named", 5, 0));

  // unlike a string, a function has no quoted form to fall back on
  EXPECT_STREQ("<fn named>", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, a_function_is_truthy) {
  clox_value_t value = CLOX_OBJECT(clox_new_function(&utest_fixture->alloc, "named", 5, 0));

  EXPECT_TRUE(clox_value_is_truthy(value));
}

UTEST_F(object, a_native_carries_the_name_the_arity_and_the_body_it_is_given) {
  clox_native_t *native = clox_new_native(&utest_fixture->alloc, "counting", 2, counting_native);

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

  EXPECT_EQ(SIZE_MAX, native->arity);
}

UTEST_F(object, a_native_does_not_keep_the_name_buffer) {
  char source[] = "named";

  clox_native_t *native = clox_new_native(&utest_fixture->alloc, source, 0, nil_native);
  source[0] = 'f';

  EXPECT_STREQ("named", native->name);
}

UTEST_F(object, a_native_renders_as_its_name) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t value = CLOX_NATIVE(&utest_fixture->alloc, "counting", 2, counting_native);

  EXPECT_STREQ("<native counting>", clox_test_value_string(&buffer, value));
  EXPECT_STREQ("<native counting>", clox_test_value_repr_string(&buffer, value));
}

UTEST_F(object, a_native_is_equal_only_to_itself) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t first = CLOX_NATIVE(alloc, "same", 2, counting_native);
  clox_value_t second = CLOX_NATIVE(alloc, "same", 2, counting_native);

  EXPECT_TRUE(clox_object_equals(first, first));
  EXPECT_FALSE(clox_object_equals(first, second));
}

UTEST_F(object, a_native_is_truthy) {
  clox_value_t value = CLOX_NATIVE(&utest_fixture->alloc, "counting", 2, counting_native);

  EXPECT_TRUE(clox_value_is_truthy(value));
}

UTEST_F(object, the_three_object_types_are_never_equal_to_each_other) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  clox_value_t string = CLOX_STRING_COPY(alloc, "same", 4);
  clox_value_t function = CLOX_OBJECT(clox_new_function(alloc, "same", 4, 0));
  clox_value_t native = CLOX_NATIVE(alloc, "same", 2, counting_native);

  EXPECT_FALSE(clox_value_equals(string, function));
  EXPECT_FALSE(clox_value_equals(function, native));
  EXPECT_FALSE(clox_value_equals(native, string));
}

UTEST(object_lifetime, freeing_the_allocator_releases_every_string) {
  // LSan is the check: nothing here is freed by hand
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  for (size_t i = 0; i < 32; i++) {
    (void)clox_test_intern_indexed(&alloc, i);
  }
  clox_value_t joined = clox_string_concat(&alloc, CLOX_STRING_COPY(&alloc, "one", 3),
                                           CLOX_STRING_COPY(&alloc, "two", 3));
  EXPECT_STREQ("onetwo", CLOX_AS_CSTRING(joined));

  clox_allocator_free(&alloc);
}

UTEST(object_lifetime, freeing_the_allocator_releases_functions_and_natives) {
  // LSan is the check again: a function owns a name and a chunk, and both go
  // with it. The chunk is grown past its first allocation on purpose.
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  for (size_t i = 0; i < 32; i++) {
    clox_function_t *function = clox_new_function(&alloc, "named", 5, 0);
    for (size_t j = 0; j < 64; j++) {
      clox_chunk_write(&function->chunk, OP_NIL, (clox_pos_t){.line = 1, .col = 1});
    }
    (void)clox_write_constant(&function->chunk, OP_CONSTANT, CLOX_STRING_COPY(&alloc, "k", 1),
                              (clox_pos_t){.line = 1, .col = 1});
    (void)clox_new_native(&alloc, "counting", 2, counting_native);
  }
  // an unnamed function has nothing to release for its name
  (void)clox_new_function(&alloc, NULL, 0, 0);

  clox_allocator_free(&alloc);
}

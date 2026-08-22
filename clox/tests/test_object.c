#include <stdlib.h>
#include <string.h>

#include <utest.h>

#include "object.h"
#include "value.h"

#include "support/harness.h"

struct object {
  clox_allocator_t alloc;
};

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

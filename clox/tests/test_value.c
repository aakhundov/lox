#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <utest.h>

#include "memory.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

#define MANY_VALUES 64

struct value_array {
  clox_value_array_t array;
};

UTEST_F_SETUP(value_array) {
  clox_value_array_init(&utest_fixture->array);
}

UTEST_F_TEARDOWN(value_array) {
  clox_value_array_free(&utest_fixture->array);
}

UTEST_F(value_array, starts_empty) {
  EXPECT_EQ((size_t)0, utest_fixture->array.length);
}

UTEST_F(value_array, write_appends_in_order) {
  clox_value_array_t *array = &utest_fixture->array;

  clox_value_array_write(array, CLOX_NUMBER(1.0));
  clox_value_array_write(array, CLOX_BOOL(true));
  clox_value_array_write(array, CLOX_NIL);

  ASSERT_EQ((size_t)3, array->length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), array->values[0]);
  EXPECT_VALUE_EQ(CLOX_BOOL(true), array->values[1]);
  EXPECT_VALUE_EQ(CLOX_NIL, array->values[2]);
}

UTEST_F(value_array, pop_returns_the_last_written_and_removes_it) {
  clox_value_array_t *array = &utest_fixture->array;

  clox_value_array_write(array, CLOX_NUMBER(1.0));
  clox_value_array_write(array, CLOX_NUMBER(2.0));

  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), clox_value_array_pop(array));
  ASSERT_EQ((size_t)1, array->length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), clox_value_array_pop(array));
  EXPECT_EQ((size_t)0, array->length);
}

UTEST_F(value_array, many_writes_all_survive_the_growth) {
  clox_value_array_t *array = &utest_fixture->array;

  for (size_t i = 0; i < MANY_VALUES; i++) {
    clox_value_array_write(array, CLOX_NUMBER((double)i));
  }

  ASSERT_EQ((size_t)MANY_VALUES, array->length);
  for (size_t i = 0; i < MANY_VALUES; i++) {
    ASSERT_VALUE_EQ(CLOX_NUMBER((double)i), array->values[i]);
  }
}

UTEST_F(value_array, free_empties_it_and_leaves_it_reusable) {
  clox_value_array_t *array = &utest_fixture->array;

  clox_value_array_write(array, CLOX_NUMBER(1.0));
  clox_value_array_free(array);
  ASSERT_EQ((size_t)0, array->length);

  clox_value_array_write(array, CLOX_NUMBER(2.0));
  ASSERT_EQ((size_t)1, array->length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), array->values[0]);
}

UTEST(value, nil_is_not_truthy) {
  EXPECT_FALSE(clox_value_is_truthy(CLOX_NIL));
}

UTEST(value, booleans_are_truthy_only_when_true) {
  EXPECT_TRUE(clox_value_is_truthy(CLOX_BOOL(true)));
  EXPECT_FALSE(clox_value_is_truthy(CLOX_BOOL(false)));
}

UTEST(value, zero_is_not_truthy_and_other_numbers_are) {
  EXPECT_FALSE(clox_value_is_truthy(CLOX_NUMBER(0.0)));
  EXPECT_TRUE(clox_value_is_truthy(CLOX_NUMBER(1.0)));
  EXPECT_TRUE(clox_value_is_truthy(CLOX_NUMBER(-1.0)));
  EXPECT_TRUE(clox_value_is_truthy(CLOX_NUMBER(0.5)));
}

UTEST(value, a_string_is_truthy) {
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  EXPECT_TRUE(clox_value_is_truthy(CLOX_STRING_COPY(&alloc, "text", 4)));

  clox_allocator_free(&alloc);
}

UTEST(value, an_empty_string_is_not_truthy) {
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  EXPECT_FALSE(clox_value_is_truthy(CLOX_STRING_COPY(&alloc, "", 0)));

  clox_allocator_free(&alloc);
}

UTEST(value, values_of_different_types_are_never_equal) {
  EXPECT_FALSE(clox_value_equals(CLOX_NIL, CLOX_BOOL(false)));
  EXPECT_FALSE(clox_value_equals(CLOX_NUMBER(0.0), CLOX_BOOL(false)));
  EXPECT_FALSE(clox_value_equals(CLOX_NUMBER(1.0), CLOX_BOOL(true)));
  EXPECT_FALSE(clox_value_equals(CLOX_NIL, CLOX_NUMBER(0.0)));
}

UTEST(value, nil_equals_nil) {
  EXPECT_TRUE(clox_value_equals(CLOX_NIL, CLOX_NIL));
}

UTEST(value, booleans_and_numbers_are_equal_by_value) {
  EXPECT_TRUE(clox_value_equals(CLOX_BOOL(true), CLOX_BOOL(true)));
  EXPECT_FALSE(clox_value_equals(CLOX_BOOL(true), CLOX_BOOL(false)));
  EXPECT_TRUE(clox_value_equals(CLOX_NUMBER(1.5), CLOX_NUMBER(1.5)));
  EXPECT_FALSE(clox_value_equals(CLOX_NUMBER(1.5), CLOX_NUMBER(2.5)));
}

UTEST(value, strings_are_equal_when_their_content_is) {
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  clox_value_t first = CLOX_STRING_COPY(&alloc, "same", 4);
  clox_value_t second = CLOX_STRING_COPY(&alloc, "same", 4);
  clox_value_t other = CLOX_STRING_COPY(&alloc, "different", 9);

  EXPECT_TRUE(clox_value_equals(first, second));
  EXPECT_FALSE(clox_value_equals(first, other));

  clox_allocator_free(&alloc);
}

UTEST(value, nil_and_booleans_render_as_words) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("nil", clox_test_value_string(&buffer, CLOX_NIL));
  EXPECT_STREQ("true", clox_test_value_string(&buffer, CLOX_BOOL(true)));
  EXPECT_STREQ("false", clox_test_value_string(&buffer, CLOX_BOOL(false)));
}

UTEST(value, whole_numbers_render_without_a_fractional_part) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("0", clox_test_value_string(&buffer, CLOX_NUMBER(0.0)));
  EXPECT_STREQ("1", clox_test_value_string(&buffer, CLOX_NUMBER(1.0)));
  EXPECT_STREQ("42", clox_test_value_string(&buffer, CLOX_NUMBER(42.0)));
  EXPECT_STREQ("-7", clox_test_value_string(&buffer, CLOX_NUMBER(-7.0)));
}

UTEST(value, fractions_render_as_decimals_cut_to_six_digits) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("0.5", clox_test_value_string(&buffer, CLOX_NUMBER(0.5)));
  EXPECT_STREQ("0.333333", clox_test_value_string(&buffer, CLOX_NUMBER(1.0 / 3.0)));
}

UTEST(value, a_string_renders_as_its_characters) {
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("text", clox_test_value_string(&buffer, CLOX_STRING_COPY(&alloc, "text", 4)));

  clox_allocator_free(&alloc);
}

UTEST(value, a_size_is_truthy_only_when_it_is_not_zero) {
  EXPECT_FALSE(clox_value_is_truthy(CLOX_SIZE(0)));
  EXPECT_TRUE(clox_value_is_truthy(CLOX_SIZE(1)));
  EXPECT_TRUE(clox_value_is_truthy(CLOX_SIZE(SIZE_MAX)));
}

UTEST(value, sizes_are_equal_by_value) {
  EXPECT_TRUE(clox_value_equals(CLOX_SIZE(7), CLOX_SIZE(7)));
  EXPECT_FALSE(clox_value_equals(CLOX_SIZE(7), CLOX_SIZE(8)));
}

UTEST(value, a_size_is_never_equal_to_the_number_of_the_same_magnitude) {
  EXPECT_FALSE(clox_value_equals(CLOX_SIZE(7), CLOX_NUMBER(7.0)));
}

UTEST(value, a_size_renders_as_a_whole_number) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("0", clox_test_value_string(&buffer, CLOX_SIZE(0)));
  EXPECT_STREQ("42", clox_test_value_string(&buffer, CLOX_SIZE(42)));
}

UTEST(value, nil_and_booleans_repr_as_they_render) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("nil", clox_test_value_repr_string(&buffer, CLOX_NIL));
  EXPECT_STREQ("true", clox_test_value_repr_string(&buffer, CLOX_BOOL(true)));
  EXPECT_STREQ("false", clox_test_value_repr_string(&buffer, CLOX_BOOL(false)));
  EXPECT_STREQ("42", clox_test_value_repr_string(&buffer, CLOX_SIZE(42)));
}

UTEST(value, whole_numbers_repr_without_a_fractional_part) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("0", clox_test_value_repr_string(&buffer, CLOX_NUMBER(0.0)));
  EXPECT_STREQ("1", clox_test_value_repr_string(&buffer, CLOX_NUMBER(1.0)));
  EXPECT_STREQ("42", clox_test_value_repr_string(&buffer, CLOX_NUMBER(42.0)));
  EXPECT_STREQ("-7", clox_test_value_repr_string(&buffer, CLOX_NUMBER(-7.0)));
}

UTEST(value, a_fraction_reprs_with_every_digit_that_identifies_it) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("0.5", clox_test_value_repr_string(&buffer, CLOX_NUMBER(0.5)));
  EXPECT_STREQ("0.3333333333333333", clox_test_value_repr_string(&buffer, CLOX_NUMBER(1.0 / 3.0)));
}

UTEST(value, a_repr_keeps_the_digits_the_rendered_form_drops) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  clox_value_t sum = CLOX_NUMBER(0.1 + 0.2);

  // the rendered form rounds to six significant digits
  EXPECT_STREQ("0.3", clox_test_value_string(&buffer, sum));
  EXPECT_STREQ("0.30000000000000004", clox_test_value_repr_string(&buffer, sum));
  // and the two are different numbers
  EXPECT_FALSE(clox_value_equals(sum, CLOX_NUMBER(0.3)));
}

UTEST(value, numbers_repr_in_positional_notation) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  // Lox source has no exponent syntax, so neither may the repr
  EXPECT_STREQ("1000000000000000019884624838656",
               clox_test_value_repr_string(&buffer, CLOX_NUMBER(1e30)));
  EXPECT_STREQ("0.0000001", clox_test_value_repr_string(&buffer, CLOX_NUMBER(1e-7)));
}

UTEST(value, a_repr_parses_back_to_the_number_it_came_from) {
  char buffer[CLOX_TEST_MESSAGE_SIZE];
  const double numbers[] = {0.0,  1.0,  -7.0, 0.5, 0.1 + 0.2, 1.0 / 3.0,
                            1e-7, 1e30, 1e17, 0.1, -0.1 - 0.2};

  for (size_t i = 0; i < CLOX_ARRAY_SIZE(numbers); i++) {
    const char *text = clox_test_value_repr_string(&buffer, CLOX_NUMBER(numbers[i]));
    ASSERT_EQ(numbers[i], strtod(text, NULL));
  }
}

UTEST(value, a_string_reprs_in_quotes) {
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);
  char buffer[CLOX_TEST_MESSAGE_SIZE];

  EXPECT_STREQ("\"text\"",
               clox_test_value_repr_string(&buffer, CLOX_STRING_COPY(&alloc, "text", 4)));

  clox_allocator_free(&alloc);
}

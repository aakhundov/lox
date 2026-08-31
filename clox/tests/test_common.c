#include <stddef.h>

#include <utest.h>

#include "common.h"

UTEST(common, array_size_counts_elements_not_bytes) {
  int numbers[5] = {0};
  double doubles[3] = {0};

  EXPECT_EQ((size_t)5, CLOX_ARRAY_SIZE(numbers));
  EXPECT_EQ((size_t)3, CLOX_ARRAY_SIZE(doubles));
}

UTEST(common, array_size_is_a_constant_expression) {
  int numbers[5] = {0};
  (void)numbers;

  _Static_assert(CLOX_ARRAY_SIZE((int[5]){0}) == 5, "unexpected");
  _Static_assert(CLOX_ARRAY_SIZE((double[3]){0}) == 3, "unexpected");
}

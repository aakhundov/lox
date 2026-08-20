#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "memory.h"

#define ELEMENTS 8
#define GROWN_ELEMENTS ((size_t)ELEMENTS * 4)

// Most of what this module promises cannot be stated as a return value, so
// the sanitizers are the check: every test writes each byte it asked for, and
// ASan reports a block that is short of it while LSan reports one never freed.

UTEST(memory, an_allocated_array_holds_every_element_asked_for) {
  int *values = CLOX_ALLOCATE_ARRAY(int, ELEMENTS);
  ASSERT_TRUE(values != NULL);

  for (size_t i = 0; i < ELEMENTS; i++) {
    values[i] = (int)i;
  }
  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(ELEMENTS - 1, values[ELEMENTS - 1]);

  CLOX_FREE_ARRAY(int, values, ELEMENTS);
}

UTEST(memory, growing_keeps_the_old_contents) {
  int *values = CLOX_ALLOCATE_ARRAY(int, ELEMENTS);
  ASSERT_TRUE(values != NULL);
  for (size_t i = 0; i < ELEMENTS; i++) {
    values[i] = (int)i;
  }

  values = CLOX_GROW_ARRAY(int, values, ELEMENTS, GROWN_ELEMENTS);
  ASSERT_TRUE(values != NULL);

  for (size_t i = 0; i < ELEMENTS; i++) {
    ASSERT_EQ((int)i, values[i]);
  }
  for (size_t i = ELEMENTS; i < GROWN_ELEMENTS; i++) {
    values[i] = (int)i; // the room asked for has to be there
  }

  CLOX_FREE_ARRAY(int, values, GROWN_ELEMENTS);
}

UTEST(memory, shrinking_keeps_what_still_fits) {
  int *values = CLOX_ALLOCATE_ARRAY(int, ELEMENTS);
  ASSERT_TRUE(values != NULL);
  for (size_t i = 0; i < ELEMENTS; i++) {
    values[i] = (int)i;
  }

  values = CLOX_GROW_ARRAY(int, values, ELEMENTS, 2);
  ASSERT_TRUE(values != NULL);

  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(1, values[1]);

  CLOX_FREE_ARRAY(int, values, 2);
}

UTEST(memory, allocating_from_nothing_and_freeing_back_to_nothing) {
  void *block = clox_reallocate(NULL, 0, 16);
  ASSERT_TRUE(block != NULL);
  memset(block, 0, 16);

  EXPECT_EQ(NULL, clox_reallocate(block, 16, 0));
}

UTEST(memory, freeing_an_array_yields_null) {
  int *values = CLOX_ALLOCATE_ARRAY(int, ELEMENTS);
  ASSERT_TRUE(values != NULL);

  EXPECT_EQ(NULL, CLOX_FREE_ARRAY(int, values, ELEMENTS));
}

UTEST(memory, grow_size_starts_at_the_initial_size_and_then_doubles) {
  EXPECT_EQ((size_t)CLOX_INITIAL_SIZE, (size_t)CLOX_GROW_SIZE(0));
  EXPECT_EQ((size_t)CLOX_INITIAL_SIZE, (size_t)CLOX_GROW_SIZE(1));
  EXPECT_EQ((size_t)CLOX_INITIAL_SIZE, (size_t)CLOX_GROW_SIZE(CLOX_INITIAL_SIZE - 1));
  EXPECT_EQ((size_t)(CLOX_INITIAL_SIZE * 2), (size_t)CLOX_GROW_SIZE(CLOX_INITIAL_SIZE));
  EXPECT_EQ((size_t)(CLOX_INITIAL_SIZE * 4), (size_t)CLOX_GROW_SIZE(CLOX_INITIAL_SIZE * 2));
}

UTEST(memory, array_size_counts_elements_not_bytes) {
  int numbers[5] = {0};
  double doubles[3] = {0};

  EXPECT_EQ((size_t)5, CLOX_ARRAY_SIZE(numbers));
  EXPECT_EQ((size_t)3, CLOX_ARRAY_SIZE(doubles));
}

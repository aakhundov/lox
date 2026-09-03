#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "memory.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

#define ELEMENTS 8
#define GROWN_ELEMENTS ((size_t)ELEMENTS * 4)
#define BLOCK_BYTES 16

// Much of what this module promises cannot be stated as a return value, so the
// sanitizers are the check: every test writes each byte it asked for, and ASan
// reports a block that is short of it while LSan reports one never freed. What
// the allocator does say for itself is the running total it keeps, so the tests
// below read `allocated_size` back through the header.

struct memory {
  clox_allocator_t alloc;
};

// A body for the native the object tests allocate. Nothing calls it; it is here
// because a native without a function is rejected before it is ever recorded.
static bool a_native(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                     clox_vm_t *vm) {
  (void)arg_count;
  (void)args;
  (void)vm;

  result->value = CLOX_NIL;
  return true;
}

UTEST_F_SETUP(memory) {
  clox_allocator_init(&utest_fixture->alloc);
}

UTEST_F_TEARDOWN(memory) {
  clox_allocator_free(&utest_fixture->alloc);
}

UTEST_F(memory, an_allocated_array_holds_every_element_asked_for) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *values = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(values != NULL);

  for (size_t i = 0; i < ELEMENTS; i++) {
    values[i] = (int)i;
  }
  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(ELEMENTS - 1, values[ELEMENTS - 1]);

  CLOX_ARRAY_FREE(alloc, int, values, ELEMENTS);
}

UTEST_F(memory, growing_keeps_the_old_contents) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *values = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(values != NULL);
  for (size_t i = 0; i < ELEMENTS; i++) {
    values[i] = (int)i;
  }

  values = CLOX_ARRAY_GROW(alloc, int, values, ELEMENTS, GROWN_ELEMENTS);
  ASSERT_TRUE(values != NULL);

  for (size_t i = 0; i < ELEMENTS; i++) {
    ASSERT_EQ((int)i, values[i]);
  }
  for (size_t i = ELEMENTS; i < GROWN_ELEMENTS; i++) {
    values[i] = (int)i; // the room asked for has to be there
  }

  CLOX_ARRAY_FREE(alloc, int, values, GROWN_ELEMENTS);
}

UTEST_F(memory, shrinking_keeps_what_still_fits) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *values = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(values != NULL);
  for (size_t i = 0; i < ELEMENTS; i++) {
    values[i] = (int)i;
  }

  values = CLOX_ARRAY_GROW(alloc, int, values, ELEMENTS, 2);
  ASSERT_TRUE(values != NULL);

  EXPECT_EQ(0, values[0]);
  EXPECT_EQ(1, values[1]);

  CLOX_ARRAY_FREE(alloc, int, values, 2);
}

UTEST_F(memory, allocating_from_nothing_and_freeing_back_to_nothing) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  void *block = clox_reallocate(alloc, NULL, 0, BLOCK_BYTES);
  ASSERT_TRUE(block != NULL);
  memset(block, 0, BLOCK_BYTES);

  EXPECT_EQ(NULL, clox_reallocate(alloc, block, BLOCK_BYTES, 0));
}

UTEST_F(memory, freeing_an_array_yields_null) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *values = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(values != NULL);

  EXPECT_EQ(NULL, CLOX_ARRAY_FREE(alloc, int, values, ELEMENTS));
}

UTEST(memory, grow_size_starts_at_the_initial_size_and_then_doubles) {
  EXPECT_EQ((size_t)CLOX_ARRAY_INITIAL_SIZE, (size_t)CLOX_ARRAY_GROW_SIZE(0));
  EXPECT_EQ((size_t)CLOX_ARRAY_INITIAL_SIZE, (size_t)CLOX_ARRAY_GROW_SIZE(1));
  EXPECT_EQ((size_t)CLOX_ARRAY_INITIAL_SIZE,
            (size_t)CLOX_ARRAY_GROW_SIZE(CLOX_ARRAY_INITIAL_SIZE - 1));
  EXPECT_EQ((size_t)(CLOX_ARRAY_INITIAL_SIZE * 2),
            (size_t)CLOX_ARRAY_GROW_SIZE(CLOX_ARRAY_INITIAL_SIZE));
  EXPECT_EQ((size_t)(CLOX_ARRAY_INITIAL_SIZE * 4),
            (size_t)CLOX_ARRAY_GROW_SIZE(CLOX_ARRAY_INITIAL_SIZE * 2));
}

// the running total

UTEST_F(memory, a_new_allocator_holds_nothing) {
  EXPECT_EQ((size_t)0, utest_fixture->alloc.allocated_size);
  EXPECT_EQ(NULL, utest_fixture->alloc.objects);
}

UTEST_F(memory, the_total_counts_the_bytes_asked_for) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  void *block = clox_reallocate(alloc, NULL, 0, BLOCK_BYTES);
  ASSERT_TRUE(block != NULL);

  EXPECT_EQ((size_t)BLOCK_BYTES, alloc->allocated_size);

  clox_reallocate(alloc, block, BLOCK_BYTES, 0);
  EXPECT_EQ((size_t)0, alloc->allocated_size);
}

UTEST_F(memory, the_total_counts_an_array_by_its_element_size) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *values = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(values != NULL);

  EXPECT_EQ(sizeof(int) * ELEMENTS, alloc->allocated_size);

  CLOX_ARRAY_FREE(alloc, int, values, ELEMENTS);
  EXPECT_EQ((size_t)0, alloc->allocated_size);
}

UTEST_F(memory, growing_adds_only_the_difference) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *values = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(values != NULL);

  values = CLOX_ARRAY_GROW(alloc, int, values, ELEMENTS, GROWN_ELEMENTS);
  ASSERT_TRUE(values != NULL);
  EXPECT_EQ(sizeof(int) * GROWN_ELEMENTS, alloc->allocated_size);

  CLOX_ARRAY_FREE(alloc, int, values, GROWN_ELEMENTS);
  EXPECT_EQ((size_t)0, alloc->allocated_size);
}

UTEST_F(memory, shrinking_gives_the_difference_back) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *values = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(values != NULL);

  values = CLOX_ARRAY_GROW(alloc, int, values, ELEMENTS, 2);
  ASSERT_TRUE(values != NULL);
  EXPECT_EQ(sizeof(int) * 2, alloc->allocated_size);

  CLOX_ARRAY_FREE(alloc, int, values, 2);
  EXPECT_EQ((size_t)0, alloc->allocated_size);
}

UTEST_F(memory, separate_blocks_add_up) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  int *first = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  int *second = CLOX_ARRAY_ALLOCATE(alloc, int, ELEMENTS);
  ASSERT_TRUE(first != NULL);
  ASSERT_TRUE(second != NULL);

  EXPECT_EQ(sizeof(int) * ELEMENTS * 2, alloc->allocated_size);

  CLOX_ARRAY_FREE(alloc, int, first, ELEMENTS);
  EXPECT_EQ(sizeof(int) * ELEMENTS, alloc->allocated_size);

  CLOX_ARRAY_FREE(alloc, int, second, ELEMENTS);
  EXPECT_EQ((size_t)0, alloc->allocated_size);
}

// the objects the allocator owns

// Whether the list runs in allocation order or against it is the allocator's
// own business, so the tests below only ask whether an object is on it.
static bool is_recorded(const clox_allocator_t *alloc, const void *object) {
  for (const clox_object_t *obj = alloc->objects; obj != NULL; obj = obj->next) {
    if (obj == object) {
      return true;
    }
  }
  return false;
}

UTEST_F(memory, an_allocated_object_is_recorded) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  const clox_string_t *string = clox_string_copy(alloc, "text", 4);
  clox_test_keep(alloc, string);
  ASSERT_TRUE(string != NULL);

  EXPECT_TRUE(is_recorded(alloc, string));
}

UTEST_F(memory, an_object_of_every_kind_is_recorded) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_value_t slot = CLOX_NIL;

  // each is held by the test alone, so each has to be kept before the next
  // allocation is free to take it
  const clox_string_t *string = clox_string_copy(alloc, "text", 4);
  clox_test_keep(alloc, string);
  const clox_function_t *function = clox_new_function(alloc, "fn", 2, 0, "test.lox", "");
  clox_test_keep(alloc, function);
  const clox_native_t *native = clox_new_native(alloc, "nt", 0, a_native);
  clox_test_keep(alloc, native);
  const clox_upvalue_t *upvalue = clox_new_upvalue(alloc, &slot);
  clox_test_keep(alloc, upvalue);
  const clox_closure_t *closure = clox_new_closure(alloc, function);
  clox_test_keep(alloc, closure);

  EXPECT_TRUE(is_recorded(alloc, string));
  EXPECT_TRUE(is_recorded(alloc, function));
  EXPECT_TRUE(is_recorded(alloc, native));
  EXPECT_TRUE(is_recorded(alloc, upvalue));
  EXPECT_TRUE(is_recorded(alloc, closure));
}

UTEST_F(memory, an_object_adds_its_own_size_to_the_total) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  size_t before = alloc->allocated_size;
  const clox_string_t *string = clox_string_copy(alloc, "text", 4);
  clox_test_keep(alloc, string);
  ASSERT_TRUE(string != NULL);

  // the object, its characters and the NUL; the intern table it goes into
  // accounts for whatever else the total grew by
  EXPECT_TRUE(alloc->allocated_size >= before + sizeof(clox_string_t) + string->length + 1);
}

UTEST_F(memory, an_interned_string_is_not_allocated_a_second_time) {
  clox_allocator_t *alloc = &utest_fixture->alloc;

  const clox_string_t *first = clox_string_copy(alloc, "text", 4);
  clox_test_keep(alloc, first);
  ASSERT_TRUE(first != NULL);

  size_t after_first = alloc->allocated_size;
  const clox_string_t *second = clox_string_copy(alloc, "text", 4);

  EXPECT_EQ(first, second);
  EXPECT_EQ(after_first, alloc->allocated_size);
}

// LSan is the oracle here: the strings, the function and the chunk it carries
// are never freed by name, only by the allocator going away underneath them.
UTEST(memory, freeing_the_allocator_frees_everything_it_handed_out) {
  clox_allocator_t alloc;
  clox_allocator_init(&alloc);

  ASSERT_TRUE(clox_string_copy(&alloc, "text", 4) != NULL);
  ASSERT_TRUE(clox_new_function(&alloc, "fn", 2, 0, "test.lox", "") != NULL);
  ASSERT_TRUE(clox_new_native(&alloc, "nt", 0, a_native) != NULL);

  clox_allocator_free(&alloc);

  EXPECT_EQ(NULL, alloc.objects);
}

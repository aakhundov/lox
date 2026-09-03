#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utest.h>

#include "library.h"
#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

#include "support/harness.h"

// Wall-clock seconds at the start of 2023: any later reading is a plausible
// clock, and a reading below it is not a time at all.
#define A_PAST_EPOCH_SECOND 1672531200.0

// The entry registered under name, or NULL when the library has no such one.
static const clox_library_fn_t *library_entry(const char *name) {
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    if (strcmp(clox_library_fns[i].name, name) == 0) {
      return &clox_library_fns[i];
    }
  }

  return NULL;
}

// Calls the entry registered under name with the arguments given, as the VM
// making the call would. The VM has already enforced the declared arity by the
// time a body runs, so these calls pass what the entry asked for and the result
// says what the body did with it.
static bool call_library_fn(clox_vm_t *vm, const char *name, size_t arg_count, clox_value_t *args,
                            clox_native_result_t *result) {
  const clox_library_fn_t *entry = library_entry(name);
  if (entry == NULL) {
    return false;
  }

  return entry->fn(arg_count, args, result, vm);
}

// The single-argument call the type and range tests are made of.
static bool call_library_fn_1(clox_vm_t *vm, const char *name, clox_value_t arg,
                              clox_native_result_t *result) {
  clox_value_t args[] = {arg};

  return call_library_fn(vm, name, 1, args, result);
}

// Every entry is called with a VM, since an entry is free to reach for one: the
// allocating natives build their result in its heap, and the random ones draw
// from its state. The allocator under it is the fixture's own, so a test can
// hand a native a string and read the objects it makes back.
struct library {
  clox_allocator_t alloc;
  clox_vm_t vm;
};

UTEST_F_SETUP(library) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_vm_init(&utest_fixture->vm, &utest_fixture->alloc);
}

UTEST_F_TEARDOWN(library) {
  clox_vm_free(&utest_fixture->vm);
  clox_allocator_free(&utest_fixture->alloc);
}

// A string the fixture holds, for handing to a native as an argument. A native
// called by the VM reads its arguments off the stack, which roots them; a test
// calling one directly stands in for that with the durable stack.
static clox_value_t a_string(struct library *fixture, const char *chars) {
  return clox_test_string_kept(&fixture->alloc, chars, strlen(chars));
}

// The result of a call that allocated, kept for as long as the test needs it.
// The VM would have pushed it onto the stack the moment the native returned;
// nothing roots it here until the test says so.
static clox_value_t kept_result(struct library *fixture, clox_native_result_t *result) {
  if (CLOX_IS_OBJECT(result->value)) {
    clox_test_keep(&fixture->alloc, CLOX_AS_OBJECT(result->value));
  }

  return result->value;
}

UTEST_F(library, the_library_is_not_empty) {
  // the tests below are vacuous if the library ever empties
  EXPECT_TRUE(CLOX_LIBRARY_SIZE > 0);
}

UTEST_F(library, every_entry_has_a_name_and_a_body) {
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    ASSERT_TRUE(clox_library_fns[i].name != NULL);
    ASSERT_TRUE(clox_library_fns[i].name[0] != '\0');
    ASSERT_TRUE(clox_library_fns[i].fn != NULL);
  }
}

UTEST_F(library, every_entry_finds_its_own_index_back_by_name) {
  // name, arity and body come from one line of library.def, so a lookup by
  // name must land on the entry that line built
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    EXPECT_TRUE(library_entry(clox_library_fns[i].name) == &clox_library_fns[i]);
  }
}

UTEST_F(library, no_two_entries_share_a_name) {
  // a duplicate would shadow itself once the VM defines the globals
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    for (size_t j = i + 1; j < CLOX_LIBRARY_SIZE; j++) {
      EXPECT_STRNE(clox_library_fns[i].name, clox_library_fns[j].name);
    }
  }
}

UTEST_F(library, every_entry_declares_the_arity_its_callers_expect) {
  struct {
    const char *name;
    size_t arity;
  } expected[] = {
      {"clock", 0},     {"time", 0},      {"sleep", 1},      {"exit", 0},       {"seed", 1},
      {"random", 0},    {"randint", 1},   {"sqrt", 1},       {"pow", 2},        {"abs", 1},
      {"floor", 1},     {"ceil", 1},      {"min", SIZE_MAX}, {"max", SIZE_MAX}, {"len", 1},
      {"ord", 1},       {"chr", 1},       {"substr", 3},     {"upper", 1},      {"lower", 1},
      {"trim", 1},      {"repeat", 2},    {"replace", 3},    {"type", 1},       {"str", 1},
      {"read_line", 0}, {"read_file", 1}, {"gc", 0},
  };

  // every entry the table declares is named here, and every name here is in
  // the table: adding a native without deciding its arity fails one or other
  EXPECT_EQ(sizeof(expected) / sizeof(expected[0]), (size_t)CLOX_LIBRARY_SIZE);

  for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
    const clox_library_fn_t *entry = library_entry(expected[i].name);
    ASSERT_TRUE_MSG(entry != NULL, expected[i].name);
    EXPECT_EQ_MSG(expected[i].arity, entry->arity, expected[i].name);
  }
}

UTEST_F(library, clock_returns_a_number_of_seconds) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "clock", 0, NULL, &result));
  ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
  EXPECT_TRUE(CLOX_AS_NUMBER(result.value) >= 0.0);
}

UTEST_F(library, clock_does_not_run_backwards) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "clock", 0, NULL, &result));
  double first = CLOX_AS_NUMBER(result.value);

  // work the processor rather than the wall clock: this is CPU time
  volatile double sink = 0.0;
  for (size_t i = 0; i < 100000; i++) {
    sink += (double)i;
  }

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "clock", 0, NULL, &result));
  EXPECT_TRUE(CLOX_AS_NUMBER(result.value) >= first);
}

UTEST_F(library, time_returns_wall_clock_seconds_since_the_epoch) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "time", 0, NULL, &result));
  ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
  // clock() counts CPU time from zero, so this also separates the two
  EXPECT_TRUE(CLOX_AS_NUMBER(result.value) > A_PAST_EPOCH_SECOND);
}

UTEST_F(library, sleep_returns_nil) {
  clox_native_result_t result;

  // zero seconds: the test says what the call yields, not how long it takes
  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "sleep", CLOX_NUMBER(0.0), &result));
  EXPECT_VALUE_EQ(CLOX_NIL, result.value);
}

UTEST_F(library, sleep_rejects_a_non_integer_count_of_seconds) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "sleep", CLOX_NUMBER(0.5), &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST_F(library, sleep_rejects_a_negative_count_of_seconds) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "sleep", CLOX_NUMBER(-1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);
}

UTEST_F(library, sleep_rejects_a_count_of_seconds_wider_than_the_call_takes) {
  clox_native_result_t result;

  // sleep() takes an unsigned int, so anything past its range would wrap
  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "sleep", CLOX_NUMBER(4294967296.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);
}

UTEST_F(library, exit_always_fails) {
  clox_native_result_t result;

  // the VM has no unwinding path, so halting means failing: the run ends
  // through the ordinary runtime-error return and the VM is torn down
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "exit", 0, NULL, &result));
  EXPECT_STREQ("exited", result.error_msg);
}

// The objects the allocator is holding, however its list is ordered.
static size_t count_objects(const clox_allocator_t *alloc) {
  size_t count = 0;
  for (const clox_object_t *object = alloc->objects; object != NULL; object = object->next) {
    count++;
  }

  return count;
}

UTEST_F(library, gc_reports_the_bytes_it_reclaimed) {
  CLOX_TEST_SKIP_UNDER_STRESS();

  // deliberately not kept: nothing roots it, so the collection takes it and
  // what it took is what the call reports
  size_t before = utest_fixture->alloc.allocated_size;
  (void)clox_string_copy(&utest_fixture->alloc, "unreferenced", 12);
  size_t grown = utest_fixture->alloc.allocated_size - before;
  ASSERT_TRUE(grown > 0);

  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "gc", 0, NULL, &result));

  // the string's object and its bytes, and nothing else: the intern table it
  // was entered in does not shrink with it
  EXPECT_VALUE_EQ(CLOX_NUMBER((double)grown), result.value);
  EXPECT_EQ(before, utest_fixture->alloc.allocated_size);
}

UTEST_F(library, gc_reports_nothing_reclaimed_when_it_takes_nothing) {
  CLOX_TEST_SKIP_UNDER_STRESS();

  clox_native_result_t result;
  // everything allocated so far is a global of the VM's, which it reaches
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "gc", 0, NULL, &result));

  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), result.value);
}

UTEST_F(library, gc_reclaims_what_nothing_refers_to) {
  // a build that collects at every allocation would have taken the string
  // below before the call under test ran
  CLOX_TEST_SKIP_UNDER_STRESS();

  size_t before = count_objects(&utest_fixture->alloc);
  // deliberately not kept: nothing roots it, so the collection is free to take
  // it and the count says whether it did
  (void)clox_string_copy(&utest_fixture->alloc, "unreferenced", 12);
  ASSERT_EQ(before + 1, count_objects(&utest_fixture->alloc));

  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "gc", 0, NULL, &result));

  EXPECT_EQ(before, count_objects(&utest_fixture->alloc));
}

UTEST_F(library, gc_keeps_what_the_vm_still_reaches) {
  CLOX_TEST_SKIP_UNDER_STRESS();

  // the globals the VM defined at startup are every library native and its
  // name, and a collection asked for from inside a run must not take them
  size_t before = count_objects(&utest_fixture->alloc);
  ASSERT_TRUE(before > 0);

  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "gc", 0, NULL, &result));

  EXPECT_EQ(before, count_objects(&utest_fixture->alloc));
}

UTEST_F(library, seed_returns_nil) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(1.0), &result));
  EXPECT_VALUE_EQ(CLOX_NIL, result.value);
}

UTEST_F(library, seed_rejects_a_non_integer_seed) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(1.5), &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST_F(library, seed_rejects_a_negative_seed) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(-1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);
}

UTEST_F(library, random_stays_within_the_unit_interval) {
  clox_native_result_t result;

  for (size_t i = 0; i < 1000; i++) {
    ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
    ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
    double drawn = CLOX_AS_NUMBER(result.value);
    // the upper end is open: randint multiplies by its bound and relies
    // on the product staying below it
    EXPECT_TRUE(drawn >= 0.0);
    EXPECT_TRUE(drawn < 1.0);
  }
}

UTEST_F(library, the_same_seed_replays_the_same_draws) {
  clox_native_result_t result;
  double first[8];

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(42.0), &result));
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
    first[i] = CLOX_AS_NUMBER(result.value);
  }

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(42.0), &result));
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
    EXPECT_EQ(first[i], CLOX_AS_NUMBER(result.value));
  }
}

UTEST_F(library, a_different_seed_does_not_replay_the_same_draws) {
  clox_native_result_t result;
  double first[8];

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(1.0), &result));
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
    first[i] = CLOX_AS_NUMBER(result.value);
  }

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(2.0), &result));
  bool any_differs = false;
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
    any_differs = any_differs || CLOX_AS_NUMBER(result.value) != first[i];
  }

  EXPECT_TRUE(any_differs);
}

UTEST_F(library, randint_stays_below_its_bound) {
  clox_native_result_t result;

  for (size_t i = 0; i < 1000; i++) {
    ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "randint", CLOX_NUMBER(10.0), &result));
    ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
    double drawn = CLOX_AS_NUMBER(result.value);
    EXPECT_TRUE(drawn >= 0.0);
    EXPECT_TRUE(drawn < 10.0);
    EXPECT_TRUE(CLOX_IS_INTEGER(result.value));
  }
}

UTEST_F(library, randint_with_a_bound_of_one_is_always_zero) {
  clox_native_result_t result;

  for (size_t i = 0; i < 100; i++) {
    ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "randint", CLOX_NUMBER(1.0), &result));
    EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), result.value);
  }
}

UTEST_F(library, randint_rejects_a_bound_of_zero) {
  clox_native_result_t result;

  // an empty range has no value to return
  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "randint", CLOX_NUMBER(0.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "positive") != NULL);
}

UTEST_F(library, randint_rejects_a_non_integer_bound) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "randint", CLOX_NUMBER(2.5), &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST_F(library, sqrt_takes_the_square_root) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "sqrt", CLOX_NUMBER(9.0), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), result.value);
}

UTEST_F(library, sqrt_of_a_negative_number_is_not_a_number) {
  clox_native_result_t result;

  // OP_DIVIDE lets 1/0 through as an infinity, so the arithmetic is IEEE all
  // the way down and this is not the one operation that raises instead
  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "sqrt", CLOX_NUMBER(-1.0), &result));
  ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
  EXPECT_TRUE(isnan(CLOX_AS_NUMBER(result.value)));
}

UTEST_F(library, pow_raises_its_first_argument_to_its_second) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(2.0), CLOX_NUMBER(10.0)};

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "pow", 2, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1024.0), result.value);
}

UTEST_F(library, pow_rejects_a_non_number_in_either_position) {
  clox_native_result_t result;
  clox_value_t first_bad[] = {CLOX_NIL, CLOX_NUMBER(2.0)};
  clox_value_t second_bad[] = {CLOX_NUMBER(2.0), CLOX_NIL};

  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "pow", 2, first_bad, &result));
  EXPECT_TRUE(strstr(result.error_msg, "first") != NULL);

  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "pow", 2, second_bad, &result));
  EXPECT_TRUE(strstr(result.error_msg, "second") != NULL);
}

UTEST_F(library, abs_drops_the_sign) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "abs", CLOX_NUMBER(-3.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.5), result.value);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "abs", CLOX_NUMBER(3.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.5), result.value);
}

UTEST_F(library, floor_and_ceil_round_in_opposite_directions) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "floor", CLOX_NUMBER(2.7), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), result.value);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "ceil", CLOX_NUMBER(2.1), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), result.value);
}

UTEST_F(library, floor_and_ceil_round_a_negative_number_away_from_each_other) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "floor", CLOX_NUMBER(-2.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-3.0), result.value);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "ceil", CLOX_NUMBER(-2.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-2.0), result.value);
}

UTEST_F(library, the_one_argument_math_natives_reject_a_non_number) {
  const char *names[] = {"sqrt", "abs", "floor", "ceil"};

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    clox_native_result_t result;
    EXPECT_FALSE_MSG(call_library_fn_1(&utest_fixture->vm, names[i], CLOX_BOOL(true), &result),
                     names[i]);
    EXPECT_TRUE_MSG(strstr(result.error_msg, "number") != NULL, names[i]);
  }
}

UTEST_F(library, min_and_max_pick_from_their_arguments) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(3.0), CLOX_NUMBER(1.0), CLOX_NUMBER(2.0)};

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "min", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), result.value);

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "max", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), result.value);
}

UTEST_F(library, min_and_max_of_one_argument_are_that_argument) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(7.0)};

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "min", 1, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), result.value);

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "max", 1, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), result.value);
}

UTEST_F(library, min_and_max_reject_no_arguments_at_all) {
  clox_native_result_t result;

  // the arity is variadic, so this is the one bound the VM cannot enforce
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "min", 0, NULL, &result));
  EXPECT_TRUE(strstr(result.error_msg, "at least one") != NULL);

  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "max", 0, NULL, &result));
  EXPECT_TRUE(strstr(result.error_msg, "at least one") != NULL);
}

UTEST_F(library, min_and_max_reject_a_non_number_anywhere_in_the_arguments) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(1.0), CLOX_NUMBER(2.0), CLOX_NIL};

  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "min", 3, args, &result));
  // the position is what tells the caller which argument to fix
  EXPECT_TRUE(strstr(result.error_msg, "3") != NULL);

  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "max", 3, args, &result));
  EXPECT_TRUE(strstr(result.error_msg, "3") != NULL);
}

UTEST_F(library, min_and_max_ignore_a_nan_among_real_numbers) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(1.0), CLOX_NUMBER(NAN), CLOX_NUMBER(2.0)};

  // one nan in the middle must not swallow the whole result
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "min", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), result.value);

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "max", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), result.value);
}

UTEST_F(library, len_reports_the_length_of_a_string) {
  clox_native_result_t result;
  clox_value_t text = clox_test_string_kept(&utest_fixture->alloc, "hello", 5);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "len", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(5.0), result.value);
}

UTEST_F(library, len_of_the_empty_string_is_zero) {
  clox_native_result_t result;
  clox_value_t text = clox_test_string_kept(&utest_fixture->alloc, "", 0);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "len", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), result.value);
}

UTEST_F(library, len_counts_bytes_and_not_characters) {
  clox_native_result_t result;
  // two characters, five bytes: the scanner does not decode UTF-8, so neither
  // does this
  clox_value_t text = clox_test_string_kept(&utest_fixture->alloc, "e\xcc\x81z", 4);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "len", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(4.0), result.value);
}

UTEST_F(library, len_rejects_a_value_that_is_not_a_string) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "len", CLOX_NUMBER(1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
}

UTEST_F(library, ord_reports_the_byte_value_of_a_one_byte_string) {
  clox_native_result_t result;
  clox_value_t text = clox_test_string_kept(&utest_fixture->alloc, "A", 1);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "ord", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(65.0), result.value);
}

UTEST_F(library, ord_reports_a_high_byte_as_an_unsigned_value) {
  clox_native_result_t result;
  // char may be signed, so this byte would come back negative if it were read
  // as one
  clox_value_t text = clox_test_string_kept(&utest_fixture->alloc, "\xff", 1);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "ord", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(255.0), result.value);
}

UTEST_F(library, ord_rejects_a_string_that_is_not_one_byte_long) {
  clox_native_result_t result;
  clox_value_t two = clox_test_string_kept(&utest_fixture->alloc, "ab", 2);
  clox_value_t none = clox_test_string_kept(&utest_fixture->alloc, "", 0);

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "ord", two, &result));
  EXPECT_TRUE(strstr(result.error_msg, "one byte") != NULL);

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "ord", none, &result));
  EXPECT_TRUE(strstr(result.error_msg, "one byte") != NULL);
}

UTEST_F(library, ord_rejects_a_value_that_is_not_a_string) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "ord", CLOX_NUMBER(65.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
}

// What a function is compiled under. Nothing here reports an error, so the
// text is empty and is never read through.
#define FILE_NAME "test.lox"
#define SOURCE ""

// A function, a closure over it, and a native, for the tests that ask what
// type() and str() make of the callable values. The native body is a library
// entry's own: a native object needs one, and none of these tests calls it.
static clox_value_t a_function(struct library *fixture, const char *name) {
  clox_function_t *function =
      clox_new_function(&fixture->alloc, name, strlen(name), 0, FILE_NAME, SOURCE);
  clox_test_keep(&fixture->alloc, function);

  return CLOX_OBJECT(function);
}

static clox_value_t a_closure(struct library *fixture, clox_value_t function) {
  clox_closure_t *closure = clox_new_closure(&fixture->alloc, CLOX_AS_FUNCTION(function));
  clox_test_keep(&fixture->alloc, closure);

  return CLOX_OBJECT(closure);
}

static clox_value_t a_native(struct library *fixture, const char *name) {
  clox_native_t *native = clox_new_native(&fixture->alloc, name, 0, library_entry("clock")->fn);
  clox_test_keep(&fixture->alloc, native);

  return CLOX_OBJECT(native);
}

UTEST_F(library, type_names_the_kind_of_a_value) {
  struct {
    clox_value_t value;
    const char *name;
  } cases[] = {
      {CLOX_BOOL(true), "bool"},
      {CLOX_BOOL(false), "bool"},
      {CLOX_NIL, "nil"},
      {CLOX_NUMBER(1.5), "number"},
      {a_string(utest_fixture, "text"), "string"},
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    clox_native_result_t result;
    ASSERT_TRUE_MSG(call_library_fn_1(&utest_fixture->vm, "type", cases[i].value, &result),
                    cases[i].name);
    clox_value_t named = kept_result(utest_fixture, &result);
    ASSERT_TRUE(CLOX_IS_STRING(named));
    EXPECT_STREQ(cases[i].name, CLOX_AS_CSTRING(named));
  }
}

UTEST_F(library, type_calls_a_function_a_closure_and_a_script_all_functions) {
  // a closure and the function it wraps are one thing to a program: which of
  // the two a call ends up on is the compiler's business, not the program's
  clox_value_t function = a_function(utest_fixture, "f");
  clox_value_t closure = a_closure(utest_fixture, function);
  clox_value_t script = a_function(utest_fixture, CLOX_SCRIPT_NAME);

  clox_value_t values[] = {function, closure, script};
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    clox_native_result_t result;
    ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "type", values[i], &result));
    EXPECT_STREQ("function", CLOX_AS_CSTRING(kept_result(utest_fixture, &result)));
  }
}

UTEST_F(library, type_keeps_a_native_apart_from_a_function) {
  // the printer keeps them apart too, and a program that can see the
  // difference in what print writes can ask about it here
  clox_native_result_t result;

  ASSERT_TRUE(
      call_library_fn_1(&utest_fixture->vm, "type", a_native(utest_fixture, "nt"), &result));
  EXPECT_STREQ("native", CLOX_AS_CSTRING(kept_result(utest_fixture, &result)));
}

UTEST_F(library, type_answers_with_a_string_every_time) {
  // the answer is meant to be compared against a string literal in Lox
  clox_value_t values[] = {CLOX_NIL, CLOX_BOOL(true), CLOX_NUMBER(0.0)};

  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    clox_native_result_t result;
    ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "type", values[i], &result));
    EXPECT_TRUE(CLOX_IS_STRING(kept_result(utest_fixture, &result)));
  }
}

UTEST_F(library, type_of_equal_values_is_the_same_interned_string) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "type", CLOX_NUMBER(1.0), &result));
  clox_value_t first = kept_result(utest_fixture, &result);

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "type", CLOX_NUMBER(2.0), &result));
  clox_value_t second = kept_result(utest_fixture, &result);

  // strings are interned, so == on the two answers is what a program will use
  EXPECT_TRUE(CLOX_AS_OBJECT(first) == CLOX_AS_OBJECT(second));
}

UTEST_F(library, str_writes_what_print_writes) {
  // str() and the printer must not drift apart: everything below goes through
  // both, and the two texts have to agree
  clox_value_t function = a_function(utest_fixture, "f");
  clox_value_t values[] = {
      CLOX_NIL,
      CLOX_BOOL(true),
      CLOX_BOOL(false),
      CLOX_NUMBER(0.0),
      CLOX_NUMBER(1.5),
      CLOX_NUMBER(-2.0),
      a_string(utest_fixture, "text"),
      function,
      a_closure(utest_fixture, function),
      a_function(utest_fixture, CLOX_SCRIPT_NAME),
      a_native(utest_fixture, "nt"),
  };

  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    char printed[CLOX_TEST_MESSAGE_SIZE];
    clox_test_value_string(&printed, values[i]);

    clox_native_result_t result;
    ASSERT_TRUE_MSG(call_library_fn_1(&utest_fixture->vm, "str", values[i], &result), printed);
    clox_value_t text = kept_result(utest_fixture, &result);
    ASSERT_TRUE(CLOX_IS_STRING(text));
    EXPECT_STREQ(printed, CLOX_AS_CSTRING(text));
  }
}

UTEST_F(library, str_of_a_number_drops_a_trailing_zero_as_print_does) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "str", CLOX_NUMBER(1.0), &result));
  EXPECT_STREQ("1", CLOX_AS_CSTRING(kept_result(utest_fixture, &result)));
}

UTEST_F(library, str_of_a_string_is_that_very_string) {
  // nothing to build: the text is already what str() would write
  clox_value_t text = a_string(utest_fixture, "already text");
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "str", text, &result));
  EXPECT_TRUE(CLOX_AS_OBJECT(result.value) == CLOX_AS_OBJECT(text));
}

UTEST_F(library, str_of_a_long_name_is_not_cut_short) {
  // the text is sized before it is written, so no fixed buffer bounds it
  char name[200];
  memset(name, 'n', sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';

  clox_native_result_t result;
  ASSERT_TRUE(
      call_library_fn_1(&utest_fixture->vm, "str", a_function(utest_fixture, name), &result));
  clox_value_t text = kept_result(utest_fixture, &result);

  // "<fn " + the name + ">"
  EXPECT_EQ(strlen(name) + 5, CLOX_AS_STRING(text)->length);
}

UTEST_F(library, chr_builds_a_one_byte_string) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "chr", CLOX_NUMBER(65.0), &result));
  clox_value_t text = kept_result(utest_fixture, &result);
  ASSERT_TRUE(CLOX_IS_STRING(text));
  EXPECT_STREQ("A", CLOX_AS_CSTRING(text));
}

UTEST_F(library, chr_and_ord_undo_each_other) {
  for (size_t code = 1; code <= UCHAR_MAX; code++) {
    clox_native_result_t result;
    ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "chr", CLOX_NUMBER((double)code), &result));
    clox_value_t text = kept_result(utest_fixture, &result);

    ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "ord", text, &result));
    EXPECT_VALUE_EQ(CLOX_NUMBER((double)code), result.value);
  }
}

UTEST_F(library, chr_rejects_a_byte_no_string_can_hold) {
  clox_native_result_t result;

  // a string is NUL-terminated, so there is no one-byte string of NUL
  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "chr", CLOX_NUMBER(0.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "chr", CLOX_NUMBER(256.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "chr", CLOX_NUMBER(-1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);
}

UTEST_F(library, chr_rejects_a_non_integer_code) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "chr", CLOX_NUMBER(65.5), &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

// substr, upper, lower, trim, repeat and replace all answer with a string, and
// every one of these cases is "this text in, that text out".
static void expect_string_result(int *utest_result, struct library *fixture, const char *name,
                                 size_t arg_count, clox_value_t *args, const char *expected) {
  clox_native_result_t result;
  ASSERT_TRUE_MSG(call_library_fn(&fixture->vm, name, arg_count, args, &result), expected);
  clox_value_t text = kept_result(fixture, &result);
  ASSERT_TRUE_MSG(CLOX_IS_STRING(text), expected);
  EXPECT_STREQ(expected, CLOX_AS_CSTRING(text));
}

UTEST_F(library, substr_takes_the_bytes_it_is_asked_for) {
  clox_value_t args[] = {a_string(utest_fixture, "abcdef"), CLOX_NUMBER(1.0), CLOX_NUMBER(3.0)};

  expect_string_result(utest_result, utest_fixture, "substr", 3, args, "bcd");
}

UTEST_F(library, substr_from_the_start_and_to_the_end) {
  clox_value_t text = a_string(utest_fixture, "abcdef");

  clox_value_t front[] = {text, CLOX_NUMBER(0.0), CLOX_NUMBER(2.0)};
  expect_string_result(utest_result, utest_fixture, "substr", 3, front, "ab");

  clox_value_t back[] = {text, CLOX_NUMBER(4.0), CLOX_NUMBER(2.0)};
  expect_string_result(utest_result, utest_fixture, "substr", 3, back, "ef");
}

UTEST_F(library, substr_stops_at_the_end_of_the_string) {
  // a length running past the end is clamped rather than refused
  clox_value_t args[] = {a_string(utest_fixture, "abc"), CLOX_NUMBER(1.0), CLOX_NUMBER(100.0)};

  expect_string_result(utest_result, utest_fixture, "substr", 3, args, "bc");
}

UTEST_F(library, substr_past_the_end_is_the_empty_string) {
  clox_value_t text = a_string(utest_fixture, "abc");

  clox_value_t past[] = {text, CLOX_NUMBER(10.0), CLOX_NUMBER(2.0)};
  expect_string_result(utest_result, utest_fixture, "substr", 3, past, "");

  clox_value_t none[] = {text, CLOX_NUMBER(1.0), CLOX_NUMBER(0.0)};
  expect_string_result(utest_result, utest_fixture, "substr", 3, none, "");
}

UTEST_F(library, substr_rejects_a_negative_start_or_length) {
  clox_native_result_t result;
  clox_value_t text = a_string(utest_fixture, "abc");

  clox_value_t start[] = {text, CLOX_NUMBER(-1.0), CLOX_NUMBER(1.0)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "substr", 3, start, &result));
  EXPECT_TRUE(strstr(result.error_msg, "negative") != NULL);

  clox_value_t length[] = {text, CLOX_NUMBER(0.0), CLOX_NUMBER(-1.0)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "substr", 3, length, &result));
  EXPECT_TRUE(strstr(result.error_msg, "negative") != NULL);
}

UTEST_F(library, substr_rejects_arguments_of_the_wrong_type) {
  clox_native_result_t result;

  clox_value_t not_text[] = {CLOX_NUMBER(1.0), CLOX_NUMBER(0.0), CLOX_NUMBER(1.0)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "substr", 3, not_text, &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);

  clox_value_t fractional[] = {a_string(utest_fixture, "abc"), CLOX_NUMBER(0.5), CLOX_NUMBER(1.0)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "substr", 3, fractional, &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST_F(library, upper_and_lower_change_the_ascii_letters) {
  clox_value_t mixed[] = {a_string(utest_fixture, "Hello, World!")};

  expect_string_result(utest_result, utest_fixture, "upper", 1, mixed, "HELLO, WORLD!");
  expect_string_result(utest_result, utest_fixture, "lower", 1, mixed, "hello, world!");
}

UTEST_F(library, upper_and_lower_leave_a_byte_that_is_not_a_letter) {
  // digits, punctuation and high bytes come back as they went in
  clox_value_t args[] = {a_string(utest_fixture, "12-\xff")};

  expect_string_result(utest_result, utest_fixture, "upper", 1, args, "12-\xff");
  expect_string_result(utest_result, utest_fixture, "lower", 1, args, "12-\xff");
}

UTEST_F(library, upper_and_lower_of_the_empty_string_are_empty) {
  clox_value_t args[] = {a_string(utest_fixture, "")};

  expect_string_result(utest_result, utest_fixture, "upper", 1, args, "");
  expect_string_result(utest_result, utest_fixture, "lower", 1, args, "");
}

UTEST_F(library, upper_and_lower_reject_a_value_that_is_not_a_string) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "upper", CLOX_NUMBER(1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "lower", CLOX_NUMBER(1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
}

UTEST_F(library, trim_drops_the_whitespace_at_either_end) {
  clox_value_t args[] = {a_string(utest_fixture, " \t\r\n text \n\r\t ")};

  expect_string_result(utest_result, utest_fixture, "trim", 1, args, "text");
}

UTEST_F(library, trim_keeps_the_whitespace_inside) {
  clox_value_t args[] = {a_string(utest_fixture, "  two words  ")};

  expect_string_result(utest_result, utest_fixture, "trim", 1, args, "two words");
}

UTEST_F(library, trim_of_nothing_but_whitespace_is_the_empty_string) {
  clox_value_t args[] = {a_string(utest_fixture, " \t\n ")};

  expect_string_result(utest_result, utest_fixture, "trim", 1, args, "");
}

UTEST_F(library, trim_of_a_string_with_no_whitespace_keeps_it_whole) {
  clox_value_t args[] = {a_string(utest_fixture, "text")};

  expect_string_result(utest_result, utest_fixture, "trim", 1, args, "text");
}

UTEST_F(library, trim_rejects_a_value_that_is_not_a_string) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "trim", CLOX_NIL, &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
}

UTEST_F(library, repeat_writes_the_text_as_many_times_as_asked) {
  clox_value_t args[] = {a_string(utest_fixture, "ab"), CLOX_NUMBER(3.0)};

  expect_string_result(utest_result, utest_fixture, "repeat", 2, args, "ababab");
}

UTEST_F(library, repeat_once_is_the_text_itself_and_none_is_empty) {
  clox_value_t text = a_string(utest_fixture, "ab");

  clox_value_t once[] = {text, CLOX_NUMBER(1.0)};
  expect_string_result(utest_result, utest_fixture, "repeat", 2, once, "ab");

  clox_value_t never[] = {text, CLOX_NUMBER(0.0)};
  expect_string_result(utest_result, utest_fixture, "repeat", 2, never, "");
}

UTEST_F(library, repeat_of_the_empty_string_is_empty_however_often) {
  clox_value_t args[] = {a_string(utest_fixture, ""), CLOX_NUMBER(1000.0)};

  expect_string_result(utest_result, utest_fixture, "repeat", 2, args, "");
}

UTEST_F(library, repeat_rejects_a_count_that_is_negative_or_not_an_integer) {
  clox_native_result_t result;
  clox_value_t text = a_string(utest_fixture, "ab");

  clox_value_t negative[] = {text, CLOX_NUMBER(-1.0)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "repeat", 2, negative, &result));
  EXPECT_TRUE(strstr(result.error_msg, "negative") != NULL);

  clox_value_t fractional[] = {text, CLOX_NUMBER(1.5)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "repeat", 2, fractional, &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST_F(library, repeat_rejects_a_count_no_result_could_hold) {
  clox_native_result_t result;
  clox_value_t text = a_string(utest_fixture, "ab");

  // the length is worked out before anything is allocated, so both of these
  // report rather than asking the allocator for the impossible
  clox_value_t wider_than_a_size[] = {text, CLOX_NUMBER(1e30)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "repeat", 2, wider_than_a_size, &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);

  // a count that fits, whose product with the length does not
  clox_value_t wider_than_the_product[] = {text, CLOX_NUMBER(1e19)};
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "repeat", 2, wider_than_the_product, &result));
  EXPECT_TRUE(strstr(result.error_msg, "too long") != NULL);
}

UTEST_F(library, replace_puts_the_new_text_at_every_occurrence) {
  clox_value_t args[] = {a_string(utest_fixture, "a-b-c"), a_string(utest_fixture, "-"),
                         a_string(utest_fixture, "+")};

  expect_string_result(utest_result, utest_fixture, "replace", 3, args, "a+b+c");
}

UTEST_F(library, replace_takes_the_occurrences_in_order_without_overlapping) {
  clox_value_t args[] = {a_string(utest_fixture, "aaa"), a_string(utest_fixture, "aa"),
                         a_string(utest_fixture, "b")};

  // the second "aa" would start inside the first, so only one match stands
  expect_string_result(utest_result, utest_fixture, "replace", 3, args, "ba");
}

UTEST_F(library, replace_does_not_look_at_what_it_wrote) {
  clox_value_t args[] = {a_string(utest_fixture, "aa"), a_string(utest_fixture, "a"),
                         a_string(utest_fixture, "aa")};

  // a replacement that contains the text being replaced would never end if
  // the result were scanned again
  expect_string_result(utest_result, utest_fixture, "replace", 3, args, "aaaa");
}

UTEST_F(library, replace_with_the_empty_string_removes_the_text) {
  clox_value_t args[] = {a_string(utest_fixture, "a-b-c"), a_string(utest_fixture, "-"),
                         a_string(utest_fixture, "")};

  expect_string_result(utest_result, utest_fixture, "replace", 3, args, "abc");
}

UTEST_F(library, replace_leaves_a_string_it_finds_nothing_in) {
  clox_value_t args[] = {a_string(utest_fixture, "abc"), a_string(utest_fixture, "z"),
                         a_string(utest_fixture, "y")};

  expect_string_result(utest_result, utest_fixture, "replace", 3, args, "abc");
}

UTEST_F(library, replace_rejects_an_empty_text_to_look_for) {
  clox_native_result_t result;
  clox_value_t args[] = {a_string(utest_fixture, "abc"), a_string(utest_fixture, ""),
                         a_string(utest_fixture, "x")};

  // an empty needle stands between every pair of bytes, which no replacement
  // can answer
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "replace", 3, args, &result));
  EXPECT_TRUE(strstr(result.error_msg, "empty") != NULL);
}

UTEST_F(library, replace_rejects_a_value_that_is_not_a_string_in_any_position) {
  clox_native_result_t result;
  clox_value_t text = a_string(utest_fixture, "abc");

  clox_value_t positions[3][3] = {
      {CLOX_NUMBER(1.0), text, text},
      {text, CLOX_NUMBER(1.0), text},
      {text, text, CLOX_NUMBER(1.0)},
  };

  for (size_t i = 0; i < 3; i++) {
    EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "replace", 3, positions[i], &result));
    EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
  }
}

UTEST_F(library, a_vm_draws_from_its_own_generator) {
  // the state sits on the VM, so seeding one leaves another where it stood: a
  // second interpreter in the same process is not moved by the first
  clox_allocator_t other_alloc;
  clox_allocator_init(&other_alloc);
  // on the heap rather than beside the fixture's: a VM carries its whole stack
  clox_vm_t *other = malloc(sizeof(clox_vm_t));
  ASSERT_TRUE(other != NULL);
  clox_vm_init(other, &other_alloc);

  clox_native_result_t result;
  double drawn[4];

  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "seed", CLOX_NUMBER(7.0), &result));
  for (size_t i = 0; i < 4; i++) {
    ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
    drawn[i] = CLOX_AS_NUMBER(result.value);
  }

  // the same seed on the other VM replays the very same draws
  ASSERT_TRUE(call_library_fn_1(other, "seed", CLOX_NUMBER(7.0), &result));
  for (size_t i = 0; i < 4; i++) {
    ASSERT_TRUE(call_library_fn(other, "random", 0, NULL, &result));
    EXPECT_EQ(drawn[i], CLOX_AS_NUMBER(result.value));
  }

  clox_vm_free(other);
  free(other);
  clox_allocator_free(&other_alloc);
}

UTEST_F(library, a_fresh_vm_starts_where_every_other_fresh_vm_starts) {
  // an unseeded run repeats, as an unseeded rand() does
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
  double first = CLOX_AS_NUMBER(result.value);

  utest_fixture->vm.rng_state = CLOX_RNG_INITIAL_STATE;

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "random", 0, NULL, &result));
  EXPECT_EQ(first, CLOX_AS_NUMBER(result.value));
}

// read_line reads standard input and read_file reads a path, so these tests
// write a file and hand it over as one or the other. utest runs one test at a
// time in one process, so the redirected stdin only ever stands for the test
// that asked for it.
#define INPUT_PATH "clox_test_input.tmp"

struct library_input {
  clox_allocator_t alloc;
  clox_vm_t vm;
};

UTEST_F_SETUP(library_input) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_vm_init(&utest_fixture->vm, &utest_fixture->alloc);
}

UTEST_F_TEARDOWN(library_input) {
  clox_vm_free(&utest_fixture->vm);
  clox_allocator_free(&utest_fixture->alloc);
  (void)remove(INPUT_PATH);
}

// Writes (length) bytes to the file the tests below read.
static bool write_input_file(const char *chars, size_t length) {
  FILE *file = fopen(INPUT_PATH, "wb");
  if (file == NULL) {
    return false;
  }

  size_t written = fwrite(chars, 1, length, file);
  return fclose(file) == 0 && written == length;
}

// Puts the file in standard input's place, so read_line has it to read.
static bool redirect_stdin(void) {
  return freopen(INPUT_PATH, "rb", stdin) != NULL;
}

UTEST_F(library_input, read_line_reads_up_to_the_newline) {
  ASSERT_TRUE(write_input_file("first\nsecond\n", 13));
  ASSERT_TRUE(redirect_stdin());

  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "read_line", 0, NULL, &result));
  clox_value_t first = result.value;
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(first));
  ASSERT_TRUE(CLOX_IS_STRING(first));
  EXPECT_STREQ("first", CLOX_AS_CSTRING(first));

  // the newline is taken off, and the next call starts after it
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "read_line", 0, NULL, &result));
  clox_value_t second = result.value;
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(second));
  EXPECT_STREQ("second", CLOX_AS_CSTRING(second));
}

UTEST_F(library_input, read_line_reads_a_last_line_that_has_no_newline) {
  ASSERT_TRUE(write_input_file("tail", 4));
  ASSERT_TRUE(redirect_stdin());

  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "read_line", 0, NULL, &result));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(result.value));

  EXPECT_STREQ("tail", CLOX_AS_CSTRING(result.value));
}

UTEST_F(library_input, read_line_reads_an_empty_line_as_the_empty_string) {
  ASSERT_TRUE(write_input_file("\nnext\n", 6));
  ASSERT_TRUE(redirect_stdin());

  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "read_line", 0, NULL, &result));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(result.value));

  EXPECT_STREQ("", CLOX_AS_CSTRING(result.value));
}

UTEST_F(library_input, read_line_at_the_end_of_the_input_is_the_empty_string) {
  ASSERT_TRUE(write_input_file("", 0));
  ASSERT_TRUE(redirect_stdin());

  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "read_line", 0, NULL, &result));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(result.value));

  ASSERT_TRUE(CLOX_IS_STRING(result.value));
  EXPECT_STREQ("", CLOX_AS_CSTRING(result.value));
}

UTEST_F(library_input, read_line_reads_a_line_longer_than_the_buffer_it_starts_with) {
  char line[1000];
  memset(line, 'x', sizeof(line) - 1);
  line[sizeof(line) - 1] = '\n';

  ASSERT_TRUE(write_input_file(line, sizeof(line)));
  ASSERT_TRUE(redirect_stdin());

  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn(&utest_fixture->vm, "read_line", 0, NULL, &result));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(result.value));

  EXPECT_EQ(sizeof(line) - 1, CLOX_AS_STRING(result.value)->length);
}

UTEST_F(library_input, read_line_rejects_a_nul_byte_in_the_input) {
  ASSERT_TRUE(write_input_file("ab\0c\n", 5));
  ASSERT_TRUE(redirect_stdin());

  clox_native_result_t result;
  // no string can hold a NUL: the text would end at it
  EXPECT_FALSE(call_library_fn(&utest_fixture->vm, "read_line", 0, NULL, &result));
  EXPECT_TRUE(strstr(result.error_msg, "NUL") != NULL);
}

UTEST_F(library_input, read_file_reads_the_whole_file) {
  ASSERT_TRUE(write_input_file("one\ntwo\n", 8));

  clox_value_t path = clox_test_string_kept(&utest_fixture->alloc, INPUT_PATH, strlen(INPUT_PATH));
  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "read_file", path, &result));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(result.value));

  ASSERT_TRUE(CLOX_IS_STRING(result.value));
  // the newlines are part of the text, the last one included
  EXPECT_STREQ("one\ntwo\n", CLOX_AS_CSTRING(result.value));
  EXPECT_EQ((size_t)8, CLOX_AS_STRING(result.value)->length);
}

UTEST_F(library_input, read_file_reads_an_empty_file_as_the_empty_string) {
  ASSERT_TRUE(write_input_file("", 0));

  clox_value_t path = clox_test_string_kept(&utest_fixture->alloc, INPUT_PATH, strlen(INPUT_PATH));
  clox_native_result_t result;
  ASSERT_TRUE(call_library_fn_1(&utest_fixture->vm, "read_file", path, &result));
  clox_test_keep(&utest_fixture->alloc, CLOX_AS_OBJECT(result.value));

  ASSERT_TRUE(CLOX_IS_STRING(result.value));
  EXPECT_STREQ("", CLOX_AS_CSTRING(result.value));
}

UTEST_F(library_input, read_file_reports_a_path_it_cannot_open) {
  const char *missing = "no_such_file_here.tmp";
  clox_value_t path = clox_test_string_kept(&utest_fixture->alloc, missing, strlen(missing));

  clox_native_result_t result;
  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "read_file", path, &result));
  EXPECT_TRUE(strstr(result.error_msg, "open") != NULL);
  // the message names the path, so a program can tell which read failed
  EXPECT_TRUE(strstr(result.error_msg, missing) != NULL);
}

UTEST_F(library_input, read_file_rejects_a_file_holding_a_nul_byte) {
  ASSERT_TRUE(write_input_file("ab\0c", 4));

  clox_value_t path = clox_test_string_kept(&utest_fixture->alloc, INPUT_PATH, strlen(INPUT_PATH));
  clox_native_result_t result;
  // a string that held one would end at it, and the rest would be lost
  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "read_file", path, &result));
  EXPECT_TRUE(strstr(result.error_msg, "NUL") != NULL);
}

UTEST_F(library_input, read_file_rejects_a_path_that_is_not_a_string) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1(&utest_fixture->vm, "read_file", CLOX_NUMBER(1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
}

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <utest.h>

#include "library.h"
#include "memory.h"
#include "object.h"
#include "value.h"

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

// Calls the entry registered under name with the arguments given. The VM has
// already enforced the declared arity by the time a body runs, so these calls
// pass what the entry asked for and the result says what the body did with it.
static bool call_library_fn(const char *name, size_t arg_count, clox_value_t *args,
                            clox_native_result_t *result) {
  const clox_library_fn_t *entry = library_entry(name);
  if (entry == NULL) {
    return false;
  }

  return entry->fn(arg_count, args, result);
}

// The single-argument call the type and range tests are made of.
static bool call_library_fn_1(const char *name, clox_value_t arg, clox_native_result_t *result) {
  clox_value_t args[] = {arg};

  return call_library_fn(name, 1, args, result);
}

UTEST(library, the_library_is_not_empty) {
  // the tests below are vacuous if the library ever empties
  EXPECT_TRUE(CLOX_LIBRARY_SIZE > 0);
}

UTEST(library, every_entry_has_a_name_and_a_body) {
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    ASSERT_TRUE(clox_library_fns[i].name != NULL);
    ASSERT_TRUE(clox_library_fns[i].name[0] != '\0');
    ASSERT_TRUE(clox_library_fns[i].fn != NULL);
  }
}

UTEST(library, every_entry_finds_its_own_index_back_by_name) {
  // name, arity and body come from one line of library.def, so a lookup by
  // name must land on the entry that line built
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    EXPECT_TRUE(library_entry(clox_library_fns[i].name) == &clox_library_fns[i]);
  }
}

UTEST(library, no_two_entries_share_a_name) {
  // a duplicate would shadow itself once the VM defines the globals
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    for (size_t j = i + 1; j < CLOX_LIBRARY_SIZE; j++) {
      EXPECT_STRNE(clox_library_fns[i].name, clox_library_fns[j].name);
    }
  }
}

UTEST(library, every_entry_declares_the_arity_its_callers_expect) {
  struct {
    const char *name;
    size_t arity;
  } expected[] = {
      {"clock", 0},  {"time", 0},       {"sleep", 1},      {"exit", 0},       {"seed", 1},
      {"random", 0}, {"random_int", 1}, {"sqrt", 1},       {"pow", 2},        {"abs", 1},
      {"floor", 1},  {"ceil", 1},       {"min", SIZE_MAX}, {"max", SIZE_MAX}, {"len", 1},
      {"ord", 1},    {"is_bool", 1},    {"is_nil", 1},     {"is_number", 1},  {"is_string", 1},
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

UTEST(library, clock_returns_a_number_of_seconds) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn("clock", 0, NULL, &result));
  ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
  EXPECT_TRUE(CLOX_AS_NUMBER(result.value) >= 0.0);
}

UTEST(library, clock_does_not_run_backwards) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn("clock", 0, NULL, &result));
  double first = CLOX_AS_NUMBER(result.value);

  // work the processor rather than the wall clock: this is CPU time
  volatile double sink = 0.0;
  for (size_t i = 0; i < 100000; i++) {
    sink += (double)i;
  }

  ASSERT_TRUE(call_library_fn("clock", 0, NULL, &result));
  EXPECT_TRUE(CLOX_AS_NUMBER(result.value) >= first);
}

UTEST(library, time_returns_wall_clock_seconds_since_the_epoch) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn("time", 0, NULL, &result));
  ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
  // clock() counts CPU time from zero, so this also separates the two
  EXPECT_TRUE(CLOX_AS_NUMBER(result.value) > A_PAST_EPOCH_SECOND);
}

UTEST(library, sleep_returns_nil) {
  clox_native_result_t result;

  // zero seconds: the test says what the call yields, not how long it takes
  ASSERT_TRUE(call_library_fn_1("sleep", CLOX_NUMBER(0.0), &result));
  EXPECT_VALUE_EQ(CLOX_NIL, result.value);
}

UTEST(library, sleep_rejects_a_non_integer_count_of_seconds) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1("sleep", CLOX_NUMBER(0.5), &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST(library, sleep_rejects_a_negative_count_of_seconds) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1("sleep", CLOX_NUMBER(-1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);
}

UTEST(library, sleep_rejects_a_count_of_seconds_wider_than_the_call_takes) {
  clox_native_result_t result;

  // sleep() takes an unsigned int, so anything past its range would wrap
  EXPECT_FALSE(call_library_fn_1("sleep", CLOX_NUMBER(4294967296.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);
}

UTEST(library, exit_always_fails) {
  clox_native_result_t result;

  // the VM has no unwinding path, so halting means failing: the run ends
  // through the ordinary runtime-error return and the VM is torn down
  EXPECT_FALSE(call_library_fn("exit", 0, NULL, &result));
  EXPECT_STREQ("exited", result.error_msg);
}

UTEST(library, seed_returns_nil) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1("seed", CLOX_NUMBER(1.0), &result));
  EXPECT_VALUE_EQ(CLOX_NIL, result.value);
}

UTEST(library, seed_rejects_a_non_integer_seed) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1("seed", CLOX_NUMBER(1.5), &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST(library, seed_rejects_a_negative_seed) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1("seed", CLOX_NUMBER(-1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "range") != NULL);
}

UTEST(library, random_stays_within_the_unit_interval) {
  clox_native_result_t result;

  for (size_t i = 0; i < 1000; i++) {
    ASSERT_TRUE(call_library_fn("random", 0, NULL, &result));
    ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
    double drawn = CLOX_AS_NUMBER(result.value);
    // the upper end is open: random_int multiplies by its bound and relies
    // on the product staying below it
    EXPECT_TRUE(drawn >= 0.0);
    EXPECT_TRUE(drawn < 1.0);
  }
}

UTEST(library, the_same_seed_replays_the_same_draws) {
  clox_native_result_t result;
  double first[8];

  ASSERT_TRUE(call_library_fn_1("seed", CLOX_NUMBER(42.0), &result));
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn("random", 0, NULL, &result));
    first[i] = CLOX_AS_NUMBER(result.value);
  }

  ASSERT_TRUE(call_library_fn_1("seed", CLOX_NUMBER(42.0), &result));
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn("random", 0, NULL, &result));
    EXPECT_EQ(first[i], CLOX_AS_NUMBER(result.value));
  }
}

UTEST(library, a_different_seed_does_not_replay_the_same_draws) {
  clox_native_result_t result;
  double first[8];

  ASSERT_TRUE(call_library_fn_1("seed", CLOX_NUMBER(1.0), &result));
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn("random", 0, NULL, &result));
    first[i] = CLOX_AS_NUMBER(result.value);
  }

  ASSERT_TRUE(call_library_fn_1("seed", CLOX_NUMBER(2.0), &result));
  bool any_differs = false;
  for (size_t i = 0; i < 8; i++) {
    ASSERT_TRUE(call_library_fn("random", 0, NULL, &result));
    any_differs = any_differs || CLOX_AS_NUMBER(result.value) != first[i];
  }

  EXPECT_TRUE(any_differs);
}

UTEST(library, random_int_stays_below_its_bound) {
  clox_native_result_t result;

  for (size_t i = 0; i < 1000; i++) {
    ASSERT_TRUE(call_library_fn_1("random_int", CLOX_NUMBER(10.0), &result));
    ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
    double drawn = CLOX_AS_NUMBER(result.value);
    EXPECT_TRUE(drawn >= 0.0);
    EXPECT_TRUE(drawn < 10.0);
    EXPECT_TRUE(CLOX_IS_INTEGER(result.value));
  }
}

UTEST(library, random_int_with_a_bound_of_one_is_always_zero) {
  clox_native_result_t result;

  for (size_t i = 0; i < 100; i++) {
    ASSERT_TRUE(call_library_fn_1("random_int", CLOX_NUMBER(1.0), &result));
    EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), result.value);
  }
}

UTEST(library, random_int_rejects_a_bound_of_zero) {
  clox_native_result_t result;

  // an empty range has no value to return
  EXPECT_FALSE(call_library_fn_1("random_int", CLOX_NUMBER(0.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "positive") != NULL);
}

UTEST(library, random_int_rejects_a_non_integer_bound) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1("random_int", CLOX_NUMBER(2.5), &result));
  EXPECT_TRUE(strstr(result.error_msg, "integer") != NULL);
}

UTEST(library, sqrt_takes_the_square_root) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1("sqrt", CLOX_NUMBER(9.0), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), result.value);
}

UTEST(library, sqrt_of_a_negative_number_is_not_a_number) {
  clox_native_result_t result;

  // OP_DIVIDE lets 1/0 through as an infinity, so the arithmetic is IEEE all
  // the way down and this is not the one operation that raises instead
  ASSERT_TRUE(call_library_fn_1("sqrt", CLOX_NUMBER(-1.0), &result));
  ASSERT_TRUE(CLOX_IS_NUMBER(result.value));
  EXPECT_TRUE(isnan(CLOX_AS_NUMBER(result.value)));
}

UTEST(library, pow_raises_its_first_argument_to_its_second) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(2.0), CLOX_NUMBER(10.0)};

  ASSERT_TRUE(call_library_fn("pow", 2, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1024.0), result.value);
}

UTEST(library, pow_rejects_a_non_number_in_either_position) {
  clox_native_result_t result;
  clox_value_t first_bad[] = {CLOX_NIL, CLOX_NUMBER(2.0)};
  clox_value_t second_bad[] = {CLOX_NUMBER(2.0), CLOX_NIL};

  EXPECT_FALSE(call_library_fn("pow", 2, first_bad, &result));
  EXPECT_TRUE(strstr(result.error_msg, "first") != NULL);

  EXPECT_FALSE(call_library_fn("pow", 2, second_bad, &result));
  EXPECT_TRUE(strstr(result.error_msg, "second") != NULL);
}

UTEST(library, abs_drops_the_sign) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1("abs", CLOX_NUMBER(-3.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.5), result.value);

  ASSERT_TRUE(call_library_fn_1("abs", CLOX_NUMBER(3.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.5), result.value);
}

UTEST(library, floor_and_ceil_round_in_opposite_directions) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1("floor", CLOX_NUMBER(2.7), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), result.value);

  ASSERT_TRUE(call_library_fn_1("ceil", CLOX_NUMBER(2.1), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), result.value);
}

UTEST(library, floor_and_ceil_round_a_negative_number_away_from_each_other) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1("floor", CLOX_NUMBER(-2.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-3.0), result.value);

  ASSERT_TRUE(call_library_fn_1("ceil", CLOX_NUMBER(-2.5), &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(-2.0), result.value);
}

UTEST(library, the_one_argument_math_natives_reject_a_non_number) {
  const char *names[] = {"sqrt", "abs", "floor", "ceil"};

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    clox_native_result_t result;
    EXPECT_FALSE_MSG(call_library_fn_1(names[i], CLOX_BOOL(true), &result), names[i]);
    EXPECT_TRUE_MSG(strstr(result.error_msg, "number") != NULL, names[i]);
  }
}

UTEST(library, min_and_max_pick_from_their_arguments) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(3.0), CLOX_NUMBER(1.0), CLOX_NUMBER(2.0)};

  ASSERT_TRUE(call_library_fn("min", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), result.value);

  ASSERT_TRUE(call_library_fn("max", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), result.value);
}

UTEST(library, min_and_max_of_one_argument_are_that_argument) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(7.0)};

  ASSERT_TRUE(call_library_fn("min", 1, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), result.value);

  ASSERT_TRUE(call_library_fn("max", 1, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(7.0), result.value);
}

UTEST(library, min_and_max_reject_no_arguments_at_all) {
  clox_native_result_t result;

  // the arity is variadic, so this is the one bound the VM cannot enforce
  EXPECT_FALSE(call_library_fn("min", 0, NULL, &result));
  EXPECT_TRUE(strstr(result.error_msg, "at least one") != NULL);

  EXPECT_FALSE(call_library_fn("max", 0, NULL, &result));
  EXPECT_TRUE(strstr(result.error_msg, "at least one") != NULL);
}

UTEST(library, min_and_max_reject_a_non_number_anywhere_in_the_arguments) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(1.0), CLOX_NUMBER(2.0), CLOX_NIL};

  EXPECT_FALSE(call_library_fn("min", 3, args, &result));
  // the position is what tells the caller which argument to fix
  EXPECT_TRUE(strstr(result.error_msg, "3") != NULL);

  EXPECT_FALSE(call_library_fn("max", 3, args, &result));
  EXPECT_TRUE(strstr(result.error_msg, "3") != NULL);
}

UTEST(library, min_and_max_ignore_a_nan_among_real_numbers) {
  clox_native_result_t result;
  clox_value_t args[] = {CLOX_NUMBER(1.0), CLOX_NUMBER(NAN), CLOX_NUMBER(2.0)};

  // one nan in the middle must not swallow the whole result
  ASSERT_TRUE(call_library_fn("min", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), result.value);

  ASSERT_TRUE(call_library_fn("max", 3, args, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), result.value);
}

UTEST(library, the_type_predicates_answer_true_for_their_own_type) {
  clox_native_result_t result;

  ASSERT_TRUE(call_library_fn_1("is_bool", CLOX_BOOL(false), &result));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), result.value);

  ASSERT_TRUE(call_library_fn_1("is_nil", CLOX_NIL, &result));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), result.value);

  ASSERT_TRUE(call_library_fn_1("is_number", CLOX_NUMBER(1.0), &result));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), result.value);
}

UTEST(library, the_type_predicates_answer_false_for_another_type) {
  const char *names[] = {"is_bool", "is_nil", "is_number", "is_string"};
  clox_value_t others[] = {CLOX_NUMBER(1.0), CLOX_BOOL(true), CLOX_NIL, CLOX_NUMBER(1.0)};

  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    clox_native_result_t result;
    ASSERT_TRUE_MSG(call_library_fn_1(names[i], others[i], &result), names[i]);
    EXPECT_VALUE_EQ(CLOX_BOOL(false), result.value);
  }
}

UTEST(library, the_type_predicates_accept_any_value_without_failing) {
  const char *names[] = {"is_bool", "is_nil", "is_number", "is_string"};
  clox_value_t values[] = {CLOX_NIL, CLOX_BOOL(true), CLOX_NUMBER(0.0)};

  // a predicate that refused an argument would leave no way to ask about it
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    for (size_t j = 0; j < sizeof(values) / sizeof(values[0]); j++) {
      clox_native_result_t result;
      // asserted, not expected: reading the result of a call that failed
      // would be reading the half of the union the body never wrote
      ASSERT_TRUE_MSG(call_library_fn_1(names[i], values[j], &result), names[i]);
      EXPECT_TRUE(CLOX_IS_BOOL(result.value));
    }
  }
}

// The string natives need an allocator to have a string to be given.
struct library_strings {
  clox_allocator_t alloc;
};

UTEST_F_SETUP(library_strings) {
  clox_allocator_init(&utest_fixture->alloc);
}

UTEST_F_TEARDOWN(library_strings) {
  clox_allocator_free(&utest_fixture->alloc);
}

UTEST_F(library_strings, len_reports_the_length_of_a_string) {
  clox_native_result_t result;
  clox_value_t text = CLOX_STRING_COPY(&utest_fixture->alloc, "hello", 5);

  ASSERT_TRUE(call_library_fn_1("len", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(5.0), result.value);
}

UTEST_F(library_strings, len_of_the_empty_string_is_zero) {
  clox_native_result_t result;
  clox_value_t text = CLOX_STRING_COPY(&utest_fixture->alloc, "", 0);

  ASSERT_TRUE(call_library_fn_1("len", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(0.0), result.value);
}

UTEST_F(library_strings, len_counts_bytes_and_not_characters) {
  clox_native_result_t result;
  // two characters, five bytes: the scanner does not decode UTF-8, so neither
  // does this
  clox_value_t text = CLOX_STRING_COPY(&utest_fixture->alloc, "e\xcc\x81z", 4);

  ASSERT_TRUE(call_library_fn_1("len", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(4.0), result.value);
}

UTEST_F(library_strings, len_rejects_a_value_that_is_not_a_string) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1("len", CLOX_NUMBER(1.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
}

UTEST_F(library_strings, ord_reports_the_byte_value_of_a_one_byte_string) {
  clox_native_result_t result;
  clox_value_t text = CLOX_STRING_COPY(&utest_fixture->alloc, "A", 1);

  ASSERT_TRUE(call_library_fn_1("ord", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(65.0), result.value);
}

UTEST_F(library_strings, ord_reports_a_high_byte_as_an_unsigned_value) {
  clox_native_result_t result;
  // char may be signed, so this byte would come back negative if it were read
  // as one
  clox_value_t text = CLOX_STRING_COPY(&utest_fixture->alloc, "\xff", 1);

  ASSERT_TRUE(call_library_fn_1("ord", text, &result));
  EXPECT_VALUE_EQ(CLOX_NUMBER(255.0), result.value);
}

UTEST_F(library_strings, ord_rejects_a_string_that_is_not_one_byte_long) {
  clox_native_result_t result;
  clox_value_t two = CLOX_STRING_COPY(&utest_fixture->alloc, "ab", 2);
  clox_value_t none = CLOX_STRING_COPY(&utest_fixture->alloc, "", 0);

  EXPECT_FALSE(call_library_fn_1("ord", two, &result));
  EXPECT_TRUE(strstr(result.error_msg, "one byte") != NULL);

  EXPECT_FALSE(call_library_fn_1("ord", none, &result));
  EXPECT_TRUE(strstr(result.error_msg, "one byte") != NULL);
}

UTEST_F(library_strings, ord_rejects_a_value_that_is_not_a_string) {
  clox_native_result_t result;

  EXPECT_FALSE(call_library_fn_1("ord", CLOX_NUMBER(65.0), &result));
  EXPECT_TRUE(strstr(result.error_msg, "string") != NULL);
}

UTEST_F(library_strings, is_string_tells_a_string_from_every_other_value) {
  clox_native_result_t result;
  clox_value_t text = CLOX_STRING_COPY(&utest_fixture->alloc, "text", 4);

  ASSERT_TRUE(call_library_fn_1("is_string", text, &result));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), result.value);

  ASSERT_TRUE(call_library_fn_1("is_string", CLOX_NUMBER(1.0), &result));
  EXPECT_VALUE_EQ(CLOX_BOOL(false), result.value);
}

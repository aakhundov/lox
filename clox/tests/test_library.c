#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "library.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

// The body registered under name, or NULL when the library has no such entry.
static clox_native_fn_t *library_fn(const char *name) {
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    if (strcmp(clox_library_fn_names[i], name) == 0) {
      return clox_library_fns[i];
    }
  }

  return NULL;
}

UTEST(library, the_library_is_not_empty) {
  // the tests below are vacuous if the library ever empties
  EXPECT_TRUE(CLOX_LIBRARY_SIZE > 0);
}

UTEST(library, every_entry_has_a_name_and_a_body) {
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    ASSERT_TRUE(clox_library_fn_names[i] != NULL);
    ASSERT_TRUE(clox_library_fn_names[i][0] != '\0');
    ASSERT_TRUE(clox_library_fns[i] != NULL);
  }
}

UTEST(library, the_two_arrays_line_up_entry_by_entry) {
  // a name and the body under it come from one line of natives.def, so a name
  // must find its own index back
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    EXPECT_TRUE(library_fn(clox_library_fn_names[i]) == clox_library_fns[i]);
  }
}

UTEST(library, no_two_entries_share_a_name) {
  // a duplicate would shadow itself once the VM defines the globals
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    for (size_t j = i + 1; j < CLOX_LIBRARY_SIZE; j++) {
      EXPECT_STRNE(clox_library_fn_names[i], clox_library_fn_names[j]);
    }
  }
}

UTEST(library, clock_is_registered) {
  ASSERT_TRUE(library_fn("clock") != NULL);
}

UTEST(library, clock_returns_a_number_of_seconds) {
  clox_native_fn_t *clock_fn = library_fn("clock");
  ASSERT_TRUE(clock_fn != NULL);

  clox_value_t elapsed = clock_fn(0, NULL);

  ASSERT_TRUE(CLOX_IS_NUMBER(elapsed));
  EXPECT_TRUE(CLOX_AS_NUMBER(elapsed) >= 0.0);
}

UTEST(library, clock_does_not_run_backwards) {
  clox_native_fn_t *clock_fn = library_fn("clock");
  ASSERT_TRUE(clock_fn != NULL);

  double first = CLOX_AS_NUMBER(clock_fn(0, NULL));
  // work the processor rather than the wall clock: this is CPU time
  volatile double sink = 0.0;
  for (size_t i = 0; i < 100000; i++) {
    sink += (double)i;
  }
  double second = CLOX_AS_NUMBER(clock_fn(0, NULL));

  EXPECT_TRUE(second >= first);
}

UTEST(library, sleep_is_registered) {
  ASSERT_TRUE(library_fn("sleep") != NULL);
}

UTEST(library, sleep_returns_nil) {
  clox_native_fn_t *sleep_fn = library_fn("sleep");
  ASSERT_TRUE(sleep_fn != NULL);

  // zero seconds: the test says what the call yields, not how long it takes
  clox_value_t args[] = {CLOX_NUMBER(0.0)};

  EXPECT_VALUE_EQ(CLOX_NIL, sleep_fn(1, args));
}

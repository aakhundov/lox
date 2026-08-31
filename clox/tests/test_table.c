#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#include "support/harness.h"

#define MANY_KEYS 256

struct table {
  clox_allocator_t alloc;
  clox_table_t table;
};

UTEST_F_SETUP(table) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_table_init(&utest_fixture->table, &utest_fixture->alloc);
}

UTEST_F_TEARDOWN(table) {
  clox_table_free(&utest_fixture->table);
  clox_allocator_free(&utest_fixture->alloc);
}

UTEST_F(table, get_finds_nothing_in_an_empty_table) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "a");

  clox_value_t value = CLOX_NIL;
  EXPECT_FALSE(clox_table_get(&utest_fixture->table, key, &value));
}

UTEST_F(table, delete_removes_nothing_from_an_empty_table) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "a");

  EXPECT_FALSE(clox_table_delete(&utest_fixture->table, key));
}

UTEST_F(table, get_key_string_finds_nothing_in_an_empty_table) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "a");

  EXPECT_EQ(NULL,
            clox_table_get_key_string(&utest_fixture->table, key->chars, key->length, key->hash));
}

UTEST_F(table, set_reports_a_new_key_and_stores_its_value) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "answer");

  EXPECT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(42.0)));

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(&utest_fixture->table, key, &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), value);
}

UTEST_F(table, set_over_an_existing_key_replaces_the_value) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "answer");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(42.0)));
  EXPECT_FALSE(clox_table_set(&utest_fixture->table, key, CLOX_BOOL(true)));

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(&utest_fixture->table, key, &value));
  EXPECT_VALUE_EQ(CLOX_BOOL(true), value);
}

UTEST_F(table, a_stored_nil_is_distinguishable_from_an_absent_key) {
  const clox_string_t *present = clox_test_intern(&utest_fixture->alloc, "present");
  const clox_string_t *absent = clox_test_intern(&utest_fixture->alloc, "absent");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, present, CLOX_NIL));

  clox_value_t value = CLOX_NUMBER(1.0);
  EXPECT_TRUE(clox_table_get(&utest_fixture->table, present, &value));
  EXPECT_VALUE_EQ(CLOX_NIL, value);
  EXPECT_FALSE(clox_table_get(&utest_fixture->table, absent, &value));
}

UTEST_F(table, keys_with_equal_content_are_one_key) {
  const clox_string_t *first = clox_test_intern(&utest_fixture->alloc, "same");
  const clox_string_t *second = clox_test_intern(&utest_fixture->alloc, "same");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, first, CLOX_NUMBER(1.0)));
  EXPECT_FALSE(clox_table_set(&utest_fixture->table, second, CLOX_NUMBER(2.0)));

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(&utest_fixture->table, first, &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), value);
}

UTEST_F(table, distinct_keys_hold_distinct_values) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "one"), CLOX_NUMBER(1.0)));
  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "two"), CLOX_NUMBER(2.0)));
  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "three"), CLOX_NUMBER(3.0)));

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(table, clox_test_intern(alloc, "one"), &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), value);
  ASSERT_TRUE(clox_table_get(table, clox_test_intern(alloc, "two"), &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), value);
  ASSERT_TRUE(clox_table_get(table, clox_test_intern(alloc, "three"), &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), value);
}

UTEST_F(table, delete_removes_the_key) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "gone");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(1.0)));
  EXPECT_TRUE(clox_table_delete(&utest_fixture->table, key));

  clox_value_t value = CLOX_NIL;
  EXPECT_FALSE(clox_table_get(&utest_fixture->table, key, &value));
  EXPECT_FALSE(clox_table_delete(&utest_fixture->table, key));
}

UTEST_F(table, delete_removes_nothing_for_an_absent_key) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "present"), CLOX_NUMBER(1.0)));

  EXPECT_FALSE(clox_table_delete(table, clox_test_intern(alloc, "absent")));
}

UTEST_F(table, delete_leaves_the_other_keys_reachable) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "one"), CLOX_NUMBER(1.0)));
  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "two"), CLOX_NUMBER(2.0)));
  ASSERT_TRUE(clox_table_delete(table, clox_test_intern(alloc, "one")));

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(table, clox_test_intern(alloc, "two"), &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), value);
}

UTEST_F(table, a_deleted_key_can_be_set_again) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "again");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(1.0)));
  ASSERT_TRUE(clox_table_delete(&utest_fixture->table, key));
  EXPECT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(2.0)));

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(&utest_fixture->table, key, &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), value);
}

UTEST_F(table, many_keys_are_all_reachable) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  for (size_t i = 0; i < MANY_KEYS; i++) {
    ASSERT_TRUE(clox_table_set(table, clox_test_intern_indexed(alloc, i), CLOX_NUMBER((double)i)));
  }

  for (size_t i = 0; i < MANY_KEYS; i++) {
    clox_value_t value = CLOX_NIL;
    ASSERT_TRUE(clox_table_get(table, clox_test_intern_indexed(alloc, i), &value));
    ASSERT_VALUE_EQ(CLOX_NUMBER((double)i), value);
  }
}

UTEST_F(table, deleting_some_of_many_keys_leaves_the_rest_reachable) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  for (size_t i = 0; i < MANY_KEYS; i++) {
    ASSERT_TRUE(clox_table_set(table, clox_test_intern_indexed(alloc, i), CLOX_NUMBER((double)i)));
  }
  for (size_t i = 0; i < MANY_KEYS; i += 2) {
    ASSERT_TRUE(clox_table_delete(table, clox_test_intern_indexed(alloc, i)));
  }

  for (size_t i = 0; i < MANY_KEYS; i++) {
    clox_value_t value = CLOX_NIL;
    bool found = clox_table_get(table, clox_test_intern_indexed(alloc, i), &value);
    if (i % 2 == 0) {
      ASSERT_FALSE(found);
    } else {
      ASSERT_TRUE(found);
      ASSERT_VALUE_EQ(CLOX_NUMBER((double)i), value);
    }
  }
}

UTEST_F(table, copy_adds_the_source_entries_to_the_destination) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *src = &utest_fixture->table;

  ASSERT_TRUE(clox_table_set(src, clox_test_intern(alloc, "one"), CLOX_NUMBER(1.0)));
  ASSERT_TRUE(clox_table_set(src, clox_test_intern(alloc, "two"), CLOX_NUMBER(2.0)));

  clox_table_t dst;
  clox_table_init(&dst, alloc);
  ASSERT_TRUE(clox_table_set(&dst, clox_test_intern(alloc, "kept"), CLOX_NUMBER(3.0)));

  clox_table_copy(&dst, src);

  clox_value_t value = CLOX_NIL;
  EXPECT_TRUE(clox_table_get(&dst, clox_test_intern(alloc, "one"), &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), value);
  EXPECT_TRUE(clox_table_get(&dst, clox_test_intern(alloc, "two"), &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), value);
  EXPECT_TRUE(clox_table_get(&dst, clox_test_intern(alloc, "kept"), &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(3.0), value);

  clox_table_free(&dst);
}

UTEST_F(table, copy_replaces_the_values_of_keys_already_in_the_destination) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *src = &utest_fixture->table;
  const clox_string_t *key = clox_test_intern(alloc, "shared");

  ASSERT_TRUE(clox_table_set(src, key, CLOX_NUMBER(1.0)));

  clox_table_t dst;
  clox_table_init(&dst, alloc);
  ASSERT_TRUE(clox_table_set(&dst, key, CLOX_NUMBER(2.0)));

  clox_table_copy(&dst, src);

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(&dst, key, &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), value);

  clox_table_free(&dst);
}

UTEST_F(table, copy_from_an_empty_table_changes_nothing) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *src = &utest_fixture->table;
  const clox_string_t *key = clox_test_intern(alloc, "kept");

  clox_table_t dst;
  clox_table_init(&dst, alloc);
  ASSERT_TRUE(clox_table_set(&dst, key, CLOX_NUMBER(1.0)));

  clox_table_copy(&dst, src);

  clox_value_t value = CLOX_NIL;
  ASSERT_TRUE(clox_table_get(&dst, key, &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), value);

  clox_table_free(&dst);
}

UTEST_F(table, get_key_string_finds_a_key_by_content) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "interned");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NIL));

  const char *chars = "interned";
  EXPECT_EQ(key, clox_table_get_key_string(&utest_fixture->table, chars, strlen(chars), key->hash));
}

UTEST_F(table, get_key_string_finds_nothing_for_absent_content) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "present"), CLOX_NIL));

  const clox_string_t *absent = clox_test_intern(alloc, "absent");
  EXPECT_EQ(NULL, clox_table_get_key_string(table, absent->chars, absent->length, absent->hash));
}

UTEST_F(table, get_key_string_finds_nothing_after_the_key_is_deleted) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "gone");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NIL));
  ASSERT_TRUE(clox_table_delete(&utest_fixture->table, key));

  EXPECT_EQ(NULL,
            clox_table_get_key_string(&utest_fixture->table, key->chars, key->length, key->hash));
}

UTEST_F(table, next_finds_nothing_in_an_empty_table) {
  EXPECT_EQ(NULL, clox_table_next(&utest_fixture->table, NULL));
}

UTEST_F(table, next_finds_nothing_in_a_freed_table) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "a");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(1.0)));
  clox_table_free(&utest_fixture->table);

  EXPECT_EQ(NULL, clox_table_next(&utest_fixture->table, NULL));

  // the teardown frees the fixture table, which a freed table is not ready for
  clox_table_init(&utest_fixture->table, &utest_fixture->alloc);
}

UTEST_F(table, next_visits_the_only_entry_and_then_stops) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "only");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(1.0)));

  const clox_table_entry_t *entry = clox_table_next(&utest_fixture->table, NULL);
  ASSERT_TRUE(entry != NULL);
  EXPECT_EQ(key, entry->key);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), entry->value);
  EXPECT_EQ(NULL, clox_table_next(&utest_fixture->table, entry));
}

UTEST_F(table, next_visits_every_entry_exactly_once) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;
  bool seen[MANY_KEYS] = {false};

  for (size_t i = 0; i < MANY_KEYS; i++) {
    ASSERT_TRUE(clox_table_set(table, clox_test_intern_indexed(alloc, i), CLOX_NUMBER((double)i)));
  }

  size_t visited = 0;
  const clox_table_entry_t *entry = NULL;
  while ((entry = clox_table_next(table, entry)) != NULL) {
    // the value says which key this entry holds
    ASSERT_TRUE(CLOX_IS_NUMBER(entry->value));
    size_t index = (size_t)CLOX_AS_NUMBER(entry->value);

    ASSERT_TRUE(index < MANY_KEYS);
    ASSERT_FALSE(seen[index]);
    seen[index] = true;
    ASSERT_EQ(clox_test_intern_indexed(alloc, index), entry->key);
    visited++;
  }

  EXPECT_EQ((size_t)MANY_KEYS, visited);
}

UTEST_F(table, next_passes_over_deleted_entries) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  for (size_t i = 0; i < MANY_KEYS; i++) {
    ASSERT_TRUE(clox_table_set(table, clox_test_intern_indexed(alloc, i), CLOX_NUMBER((double)i)));
  }
  for (size_t i = 0; i < MANY_KEYS; i += 2) {
    ASSERT_TRUE(clox_table_delete(table, clox_test_intern_indexed(alloc, i)));
  }

  size_t visited = 0;
  const clox_table_entry_t *entry = NULL;
  while ((entry = clox_table_next(table, entry)) != NULL) {
    size_t index = (size_t)CLOX_AS_NUMBER(entry->value);

    ASSERT_TRUE(index % 2 == 1); // the even keys are gone
    ASSERT_EQ(clox_test_intern_indexed(alloc, index), entry->key);
    visited++;
  }

  EXPECT_EQ((size_t)MANY_KEYS / 2, visited);
}

UTEST_F(table, next_finds_nothing_once_every_entry_is_deleted) {
  clox_allocator_t *alloc = &utest_fixture->alloc;
  clox_table_t *table = &utest_fixture->table;

  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "one"), CLOX_NUMBER(1.0)));
  ASSERT_TRUE(clox_table_set(table, clox_test_intern(alloc, "two"), CLOX_NUMBER(2.0)));
  ASSERT_TRUE(clox_table_delete(table, clox_test_intern(alloc, "one")));
  ASSERT_TRUE(clox_table_delete(table, clox_test_intern(alloc, "two")));

  EXPECT_EQ(NULL, clox_table_next(table, NULL));
}

UTEST_F(table, a_table_is_empty_after_being_freed_and_usable_after_a_second_init) {
  const clox_string_t *key = clox_test_intern(&utest_fixture->alloc, "a");

  ASSERT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(1.0)));
  clox_table_free(&utest_fixture->table);

  clox_value_t value = CLOX_NIL;
  EXPECT_FALSE(clox_table_get(&utest_fixture->table, key, &value));

  // free gives the storage back and keeps no allocator; init is what makes a
  // table writable, whether it is the first time or the second
  clox_table_init(&utest_fixture->table, &utest_fixture->alloc);

  EXPECT_TRUE(clox_table_set(&utest_fixture->table, key, CLOX_NUMBER(2.0)));
  ASSERT_TRUE(clox_table_get(&utest_fixture->table, key, &value));
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), value);
}

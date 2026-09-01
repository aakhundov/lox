#include "table.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"

#define MAX_LENGTH(capacity) ((capacity) / 2)

static inline clox_table_entry_t *find_entry(clox_table_entry_t *entries, size_t capacity,
                                             const clox_string_t *key) {
  assert(capacity > 0);
  size_t index = key->hash % capacity;
  clox_table_entry_t *entry = entries + index;
  clox_table_entry_t *tombstone = NULL;

  while (1) {
    if (entry->key == NULL) {
      if (CLOX_IS_NIL(entry->value)) { // non-tombstone
        // key not found
        // if tombstone found on the path, return it instead
        return (tombstone != NULL) ? tombstone : entry;
      }
      if (tombstone == NULL) {
        // first tombstone on the path
        tombstone = entry;
      }
    } else if (entry->key == key) { // compared by address
      // key found
      return entry;
    }

    index++;
    entry++;
    if (index >= capacity) {
      // wrap around
      index = 0;
      entry = entries;
    }
  }
}

static inline void adjust_capacity(clox_table_t *t, size_t new_capacity) {
  // allocate new storage
  clox_table_entry_t *new_entries =
      CLOX_ARRAY_ALLOCATE(t->allocator, clox_table_entry_t, new_capacity);

  // clear new storage
  for (clox_table_entry_t *e = new_entries; e < new_entries + new_capacity; e++) {
    e->key = NULL;
    e->value = CLOX_NIL;
  }

  t->length = 0;
  if (t->entries != NULL) {
    // copy the non-empty (and non-tombstone) entries from old to storage
    for (clox_table_entry_t *src = t->entries; src < t->entries + t->capacity; src++) {
      if (src->key != NULL) {
        clox_table_entry_t *dst = find_entry(new_entries, new_capacity, src->key);
        dst->key = src->key;
        dst->value = src->value;
        t->length++;
      }
    }
  }

  // free old storage
  CLOX_ARRAY_FREE(t->allocator, clox_table_entry_t, t->entries, t->capacity);

  t->entries = new_entries;
  t->capacity = new_capacity;
}

void clox_table_init(clox_table_t *table, clox_allocator_t *alloc) {
  assert(table != NULL);
  assert(alloc != NULL);

  table->length = 0;
  table->capacity = 0;
  table->entries = NULL;
  table->allocator = alloc;
}

void clox_table_free(clox_table_t *table) {
  assert(table != NULL);

  CLOX_ARRAY_FREE(table->allocator, clox_table_entry_t, table->entries, table->capacity);

  table->length = 0;
  table->capacity = 0;
  table->entries = NULL;
  table->allocator = NULL;
}

bool clox_table_get(const clox_table_t *table, const clox_string_t *key, clox_value_t *value) {
  assert(table != NULL);
  assert(key != NULL);
  assert(value != NULL);

  if (table->length == 0) {
    return false;
  }

  clox_table_entry_t *entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  *value = entry->value;

  return true;
}

bool clox_table_set(clox_table_t *table, const clox_string_t *key, clox_value_t value) {
  assert(table != NULL);
  assert(key != NULL);

  assert(table->length < SIZE_MAX);
  if (table->length + 1 > MAX_LENGTH(table->capacity)) {
    size_t new_capacity = CLOX_ARRAY_GROW_SIZE(table->capacity);
    adjust_capacity(table, new_capacity);
  }

  clox_table_entry_t *entry = find_entry(table->entries, table->capacity, key);
  bool is_new_key = (entry->key == NULL);
  if (is_new_key && CLOX_IS_NIL(entry->value)) {
    // increment only when non-tombstone
    table->length++;
  }

  entry->key = key;
  entry->value = value;

  return is_new_key;
}

bool clox_table_delete(clox_table_t *table, const clox_string_t *key) {
  assert(table != NULL);
  assert(key != NULL);

  if (table->length == 0) {
    return false;
  }

  clox_table_entry_t *entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL) {
    return false;
  }

  // place a tombstone
  entry->key = NULL;
  entry->value = CLOX_BOOL(true);

  return true;
}

void clox_table_copy(clox_table_t *dst, const clox_table_t *src) {
  assert(dst != NULL);
  assert(src != NULL);

  if (src->entries == NULL || src->length == 0) {
    return;
  }

  for (clox_table_entry_t *e = src->entries; e < src->entries + src->capacity; e++) {
    if (e->key != NULL) {
      clox_table_set(dst, e->key, e->value);
    }
  }
}

const clox_string_t *clox_table_get_key_string(const clox_table_t *table, const char *chars,
                                               size_t length, clox_hash_t hash) {
  assert(table != NULL);
  assert(chars != NULL);

  if (table->length == 0) {
    return NULL;
  }

  size_t index = hash % table->capacity;
  clox_table_entry_t *entry = table->entries + index;

  while (1) {
    if (entry->key == NULL) {
      if (CLOX_IS_NIL(entry->value)) { // non-tombstone
        // key not found
        return NULL;
      }
    } else if (entry->key->length == length && entry->key->hash == hash &&
               memcmp(entry->key->chars, chars, length) == 0) {
      // key found
      return entry->key;
    }

    index++;
    entry++;
    if (index >= table->capacity) {
      // wrap around
      index = 0;
      entry = table->entries;
    }
  }
}

const clox_table_entry_t *clox_table_next(const clox_table_t *table,
                                          const clox_table_entry_t *prev) {
  assert(table != NULL);

  if (table->entries == NULL) {
    // the table is empty
    return NULL;
  }

  const clox_table_entry_t *current;
  if (prev != NULL) {
    // make sure prev belongs to the table
    assert((uintptr_t)prev >= (uintptr_t)table->entries);
    assert((uintptr_t)prev - (uintptr_t)table->entries <
           sizeof(clox_table_entry_t) * table->capacity);

    // start at the entry past prev
    current = prev + 1;
  } else {
    // start at the first entry
    current = table->entries;
  }

  while (current < table->entries + table->capacity) {
    if (current->key != NULL) {
      return current;
    }
    current++;
  }

  // no entries past prev
  return NULL;
}

void clox_table_mark_entries(clox_table_t *table) {
  assert(table != NULL);

  if (table->entries == NULL || table->length == 0) {
    return;
  }

  for (clox_table_entry_t *e = table->entries; e < table->entries + table->capacity; e++) {
    if (e->key != NULL) {
      // cast is safe: all string objects are allocated on the heap
      clox_mark_object(table->allocator, (clox_object_t *)e->key);
      clox_mark_value(table->allocator, e->value);
    }
  }
}

void clox_table_remove_unmarked_keys(clox_table_t *table) {
  assert(table != NULL);

  if (table->entries == NULL || table->length == 0) {
    return;
  }

  // more efficient but lower-level than calling
  // clox_table_delete on each loop iteration
  for (clox_table_entry_t *e = table->entries; e < table->entries + table->capacity; e++) {
    if (e->key != NULL && !((const clox_object_t *)e->key)->is_marked) {
      // place a tombstone
      e->key = NULL;
      e->value = CLOX_BOOL(true);
    }
  }
}

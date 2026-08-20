#ifndef CLOX_TABLE_H
#define CLOX_TABLE_H

#include <stdbool.h>
#include <stddef.h>

#include "value.h"

typedef struct {
  const clox_string_t *key;
  clox_value_t value;
} clox_table_entry_t;

typedef struct {
  size_t length;
  size_t capacity;
  clox_table_entry_t *entries;
} clox_table_t;

void clox_table_init(clox_table_t *table);
void clox_table_free(clox_table_t *table);
bool clox_table_get(const clox_table_t *table, const clox_string_t *key, clox_value_t *value);
bool clox_table_set(clox_table_t *table, const clox_string_t *key, clox_value_t value);
bool clox_table_delete(clox_table_t *table, const clox_string_t *key);
void clox_table_copy(clox_table_t *dst, const clox_table_t *src);
const clox_string_t *clox_table_get_key_string(const clox_table_t *table, const char *chars,
                                               size_t length, clox_hash_t hash);

#endif

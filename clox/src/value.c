#include "value.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "memory.h"

void clox_init_value_array(clox_value_array_t *arr) {
  arr->values = NULL;
  arr->capacity = 0;
  arr->length = 0;
}

void clox_write_value_array(clox_value_array_t *arr, clox_value_t value) {
  if (arr->length == arr->capacity) {
    size_t new_capacity = CLOX_GROW_SIZE(arr->capacity);
    arr->values = CLOX_GROW_ARRAY(clox_value_t, arr->values, arr->capacity, new_capacity);
    arr->capacity = new_capacity;
  }

  arr->values[arr->length] = value;
  arr->length++;
}

clox_value_t clox_pop_value_array(clox_value_array_t *arr) {
  assert(arr->length > 0);

  arr->length--;
  clox_value_t value = arr->values[arr->length];
  return value;
}

void clox_free_value_array(clox_value_array_t *arr) {
  CLOX_FREE_ARRAY(clox_value_t, arr->values, arr->capacity);

  arr->values = NULL;
  arr->capacity = 0;
  arr->length = 0;
}

void clox_print_value(clox_value_t value) {
  printf("%g", value);
}

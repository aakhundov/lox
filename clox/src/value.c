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
  switch (value.type) {
  case VAL_BOOL:
    printf(CLOX_AS_BOOL(value) ? "true" : "false");
    break;
  case VAL_NIL:
    printf("nil");
    break;
  case VAL_NUMBER:
    printf("%g", CLOX_AS_NUMBER(value));
    break;
  }
}

bool clox_is_truthy(clox_value_t value) {
  switch (value.type) {
  case VAL_BOOL:
    return CLOX_AS_BOOL(value);
  case VAL_NIL:
    return false;
  case VAL_NUMBER:
    return CLOX_AS_NUMBER(value) != 0.0;
  default:
    return true;
  }
}

bool clox_is_equal(clox_value_t a, clox_value_t b) {
  if (a.type != b.type) {
    return false;
  }

  switch (a.type) {
  case VAL_BOOL:
    return CLOX_AS_BOOL(a) == CLOX_AS_BOOL(b);
  case VAL_NIL:
    return true;
  case VAL_NUMBER:
    return CLOX_AS_NUMBER(a) == CLOX_AS_NUMBER(b);
  }
}

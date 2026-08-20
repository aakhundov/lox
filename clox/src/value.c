#include "value.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

#include "memory.h"
#include "object.h"

void clox_value_array_init(clox_value_array_t *arr) {
  arr->values = NULL;
  arr->capacity = 0;
  arr->length = 0;
}

void clox_value_array_write(clox_value_array_t *arr, clox_value_t val) {
  if (arr->length == arr->capacity) {
    size_t new_capacity = CLOX_GROW_SIZE(arr->capacity);
    arr->values = CLOX_GROW_ARRAY(clox_value_t, arr->values, arr->capacity, new_capacity);
    arr->capacity = new_capacity;
  }

  arr->values[arr->length] = val;
  arr->length++;
}

clox_value_t clox_value_array_pop(clox_value_array_t *arr) {
  assert(arr->length > 0);

  arr->length--;
  clox_value_t val = arr->values[arr->length];
  return val;
}

void clox_value_array_free(clox_value_array_t *arr) {
  CLOX_FREE_ARRAY(clox_value_t, arr->values, arr->capacity);

  arr->values = NULL;
  arr->capacity = 0;
  arr->length = 0;
}

void clox_value_fprintf(FILE *stream, clox_value_t val) {
  switch (val.type) {
  case VAL_BOOL:
    (void)fprintf(stream, CLOX_AS_BOOL(val) ? "true" : "false");
    break;
  case VAL_NIL:
    (void)fprintf(stream, "nil");
    break;
  case VAL_NUMBER:
    (void)fprintf(stream, "%g", CLOX_AS_NUMBER(val));
    break;
  case VAL_OBJECT:
    clox_object_fprintf(stream, val);
    break;
  }
}

void clox_value_printf(clox_value_t val) {
  clox_value_fprintf(stdout, val);
}

bool clox_value_is_truthy(clox_value_t val) {
  switch (val.type) {
  case VAL_BOOL:
    return CLOX_AS_BOOL(val);
  case VAL_NIL:
    return false;
  case VAL_NUMBER:
    return CLOX_AS_NUMBER(val) != 0.0;
  case VAL_OBJECT:
    return clox_object_is_truthy(val);
  }
}

bool clox_value_equals(clox_value_t a, clox_value_t b) {
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
  case VAL_OBJECT:
    return clox_object_equals(a, b);
  }
}

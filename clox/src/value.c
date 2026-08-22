#include "value.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "memory.h"
#include "object.h"

// no double needs more digits after the point: the smallest
// subnormal, about 4.9e-324, is the deepest one can sit
#define REPR_MAX_DIGITS 324
// the longest text that takes: sign, "0." and every digit
#define REPR_SIZE 512

// Writes the shortest plain decimal text that parses back to the same double.
// A Lox number has no exponent syntax, so %g output would not always be read
// back; %f keeps every value positional, at the price of length.
static inline void number_repr_fprintf(FILE *stream, double num) {
  char text[REPR_SIZE];

  for (int digits = 0; digits <= REPR_MAX_DIGITS; digits++) {
    (void)snprintf(text, sizeof(text), "%.*f", digits, num);
    if (strtod(text, NULL) == num) {
      break; // parses back unchanged
    }
  }

  (void)fprintf(stream, "%s", text);
}

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
  case VAL_SIZE:
    (void)fprintf(stream, "%zu", CLOX_AS_SIZE(val));
    break;
  case VAL_OBJECT:
    clox_object_fprintf(stream, val);
    break;
  }
}

void clox_value_printf(clox_value_t val) {
  clox_value_fprintf(stdout, val);
}

void clox_value_repr_fprintf(FILE *stream, clox_value_t val) {
  switch (val.type) {
  case VAL_BOOL:
  case VAL_NIL:
  case VAL_SIZE:
    // repr and str are identical
    clox_value_fprintf(stream, val);
    break;
  case VAL_NUMBER:
    number_repr_fprintf(stream, CLOX_AS_NUMBER(val));
    break;
  case VAL_OBJECT:
    clox_object_repr_fprintf(stream, val);
    break;
  }
}

void clox_value_repr_printf(clox_value_t val) {
  clox_value_repr_fprintf(stdout, val);
}

bool clox_value_is_truthy(clox_value_t val) {
  switch (val.type) {
  case VAL_BOOL:
    return CLOX_AS_BOOL(val);
  case VAL_NIL:
    return false;
  case VAL_NUMBER:
    return CLOX_AS_NUMBER(val) != 0.0;
  case VAL_SIZE:
    return CLOX_AS_SIZE(val) != 0;
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
  case VAL_SIZE:
    return CLOX_AS_SIZE(a) == CLOX_AS_SIZE(b);
  case VAL_OBJECT:
    return clox_object_equals(a, b);
  }
}

#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef unsigned char clox_byte_t;
typedef uint32_t clox_hash_t;

typedef enum clox_value_type_t {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_SIZE,
  VAL_OBJECT,
} clox_value_type_t;

typedef struct clox_object_t clox_object_t;
typedef struct clox_string_t clox_string_t;

typedef struct clox_value_t {
  clox_value_type_t type;
  union {
    bool boolean;
    double number;
    size_t size;
    clox_object_t *object;
  } as;
} clox_value_t;

#define CLOX_BOOL(val) ((clox_value_t){VAL_BOOL, {.boolean = (val)}})
#define CLOX_NIL ((clox_value_t){VAL_NIL, {.number = (0)}}) // no param
#define CLOX_NUMBER(val) ((clox_value_t){VAL_NUMBER, {.number = (val)}})
#define CLOX_SIZE(val) ((clox_value_t){VAL_SIZE, {.size = (val)}})
#define CLOX_OBJECT(obj) ((clox_value_t){VAL_OBJECT, {.object = (clox_object_t *)(obj)}})

#define CLOX_IS_BOOL(val) ((val).type == VAL_BOOL)
#define CLOX_IS_NIL(val) ((val).type == VAL_NIL)
#define CLOX_IS_NUMBER(val) ((val).type == VAL_NUMBER)
#define CLOX_IS_INTEGER(val) (is_integer(val))
#define CLOX_IS_SIZE(val) ((val).type == VAL_SIZE)
#define CLOX_IS_OBJECT(val) ((val).type == VAL_OBJECT)

#define CLOX_AS_BOOL(val) ((val).as.boolean)
#define CLOX_AS_NUMBER(val) ((val).as.number)
#define CLOX_AS_SIZE(val) ((val).as.size)
#define CLOX_AS_OBJECT(val) ((val).as.object)

typedef struct clox_allocator_t clox_allocator_t;

typedef struct clox_value_array_t {
  size_t length;
  size_t capacity;
  clox_value_t *values;
  clox_allocator_t *allocator;
} clox_value_array_t;

void clox_value_array_init(clox_value_array_t *arr, clox_allocator_t *alloc);
void clox_value_array_write(clox_value_array_t *arr, clox_value_t val);
clox_value_t clox_value_array_pop(clox_value_array_t *arr);
void clox_value_array_free(clox_value_array_t *arr);

bool clox_value_is_truthy(clox_value_t val);
bool clox_value_equals(clox_value_t a, clox_value_t b);
void clox_value_fprintf(FILE *stream, clox_value_t val);
void clox_value_printf(clox_value_t val);
void clox_value_repr_fprintf(FILE *stream, clox_value_t val);
void clox_value_repr_printf(clox_value_t val);

static inline bool is_integer(clox_value_t val) {
  if (!CLOX_IS_NUMBER(val)) {
    return false;
  }
  double num = CLOX_AS_NUMBER(val);
  return isfinite(num) && trunc(num) == num;
}

#endif

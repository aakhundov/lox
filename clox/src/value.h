#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "common.h"

typedef unsigned char clox_byte_t;
typedef uint32_t clox_hash_t;

typedef struct clox_object_t clox_object_t;
typedef struct clox_string_t clox_string_t;

#if CLOX_NAN_BOXING

#define CLOX_QNAN ((uint64_t)0x7ffc000000000000)
#define CLOX_SIGN_BIT ((uint64_t)0x8000000000000000)
#define CLOX_TRUE_BIT ((uint64_t)0x0001000000000000)

#define CLOX_NIL_VALUE (CLOX_QNAN | (uint64_t)0x0001000000000000)
#define CLOX_FALSE_VALUE (CLOX_QNAN | (uint64_t)0x0002000000000000)
#define CLOX_TRUE_VALUE (CLOX_FALSE_VALUE | CLOX_TRUE_BIT)

#define CLOX_SIZE_TAG CLOX_QNAN
#define CLOX_OBJ_TAG (CLOX_SIGN_BIT | CLOX_QNAN)
#define CLOX_TAG_MASK ((uint64_t)0xffff000000000000)  // upper 16 bits
#define CLOX_SIZE_MASK ((uint64_t)0x00000000ffffffff) // lower 32 bits
#define CLOX_OBJ_MASK ((uint64_t)0x0000ffffffffffff)  // lower 48 bits

#define CLOX_BOOL(b) ((b) ? CLOX_TRUE_VALUE : CLOX_FALSE_VALUE)
#define CLOX_NIL CLOX_NIL_VALUE // no param
#define CLOX_NUMBER(num) number_to_value(num)
#define CLOX_SIZE(sz)                                                                              \
  ((((uint64_t)(sz)) & CLOX_SIZE_MASK) | CLOX_SIZE_TAG) // sz must be <= UINT32_MAX
#define CLOX_OBJECT(obj) ((((uint64_t)((uintptr_t)(obj))) & CLOX_OBJ_MASK) | CLOX_OBJ_TAG)

#define CLOX_IS_BOOL(val) (((val) | CLOX_TRUE_BIT) == CLOX_TRUE_VALUE)
#define CLOX_IS_NIL(val) ((val) == CLOX_NIL_VALUE)
#define CLOX_IS_NUMBER(val) (((val) & CLOX_QNAN) != CLOX_QNAN)
#define CLOX_IS_SIZE(val) (((val) & CLOX_TAG_MASK) == CLOX_SIZE_TAG)
#define CLOX_IS_OBJECT(val) (((val) & CLOX_TAG_MASK) == CLOX_OBJ_TAG)

#define CLOX_AS_BOOL(val) (((val) == CLOX_TRUE_VALUE) ? true : false)
#define CLOX_AS_NUMBER(val) value_to_number(val)
#define CLOX_AS_SIZE(val) ((uint32_t)(val))
#define CLOX_AS_OBJECT(val) ((clox_object_t *)((uintptr_t)((val) & CLOX_OBJ_MASK)))

typedef uint64_t clox_value_t;

_Static_assert(sizeof(double) == sizeof(uint64_t), "double must be 8 bytes");

static inline clox_value_t number_to_value(double num) {
  union {
    double number;
    uint64_t value;
  } data;

  data.number = num;
  return data.value;
}

static inline double value_to_number(clox_value_t val) {
  union {
    double number;
    uint64_t value;
  } data;

  data.value = val;
  return data.number;
}

#else

typedef enum clox_value_type_t {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
  VAL_SIZE,
  VAL_OBJECT,
} clox_value_type_t;

typedef struct clox_value_t {
  clox_value_type_t type;
  union {
    bool boolean;
    double number;
    uint32_t size;
    clox_object_t *object;
  } as;
} clox_value_t;

#define CLOX_BOOL(b) ((clox_value_t){VAL_BOOL, {.boolean = (b)}})
#define CLOX_NIL ((clox_value_t){VAL_NIL, {.number = (0)}}) // no param
#define CLOX_NUMBER(num) ((clox_value_t){VAL_NUMBER, {.number = (num)}})
#define CLOX_SIZE(sz) ((clox_value_t){VAL_SIZE, {.size = (sz)}})
#define CLOX_OBJECT(obj) ((clox_value_t){VAL_OBJECT, {.object = (clox_object_t *)(obj)}})

#define CLOX_IS_BOOL(val) ((val).type == VAL_BOOL)
#define CLOX_IS_NIL(val) ((val).type == VAL_NIL)
#define CLOX_IS_NUMBER(val) ((val).type == VAL_NUMBER)
#define CLOX_IS_SIZE(val) ((val).type == VAL_SIZE)
#define CLOX_IS_OBJECT(val) ((val).type == VAL_OBJECT)

#define CLOX_AS_BOOL(val) ((val).as.boolean)
#define CLOX_AS_NUMBER(val) ((val).as.number)
#define CLOX_AS_SIZE(val) ((val).as.size)
#define CLOX_AS_OBJECT(val) ((val).as.object)

#endif

#define CLOX_IS_INTEGER(val) (is_integer(val))

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

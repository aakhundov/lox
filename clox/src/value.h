#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
} clox_value_type_t;

typedef struct {
  clox_value_type_t type;
  union {
    bool boolean;
    double number;
  } as;
} clox_value_t;

#define CLOX_BOOL(value) ((clox_value_t){VAL_BOOL, {.boolean = (value)}})
#define CLOX_NIL ((clox_value_t){VAL_NIL, {.number = (0)}}) // no param
#define CLOX_NUMBER(value) ((clox_value_t){VAL_NUMBER, {.number = (value)}})

#define CLOX_IS_BOOL(value) ((value).type == VAL_BOOL)
#define CLOX_IS_NIL(value) ((value).type == VAL_NIL)
#define CLOX_IS_NUMBER(value) ((value).type == VAL_NUMBER)

#define CLOX_AS_BOOL(value) ((value).as.boolean)
#define CLOX_AS_NUMBER(value) ((value).as.number)

typedef struct {
  size_t length;
  size_t capacity;
  clox_value_t *values;
} clox_value_array_t;

void clox_init_value_array(clox_value_array_t *arr);
void clox_write_value_array(clox_value_array_t *arr, clox_value_t value);
clox_value_t clox_pop_value_array(clox_value_array_t *arr);
void clox_free_value_array(clox_value_array_t *arr);

bool clox_is_truthy(clox_value_t value);
bool clox_is_equal(clox_value_t a, clox_value_t b);

void clox_print_value(clox_value_t value);

#endif

#ifndef CLOX_VALUE_H
#define CLOX_VALUE_H

#include <stddef.h>

typedef double clox_value_t;

typedef struct {
  size_t length;
  size_t capacity;
  clox_value_t *values;
} clox_value_array_t;

void clox_init_value_array(clox_value_array_t *arr);
void clox_write_value_array(clox_value_array_t *arr, clox_value_t value);
clox_value_t clox_pop_value_array(clox_value_array_t *arr);
void clox_free_value_array(clox_value_array_t *arr);

void clox_print_value(clox_value_t value);

#endif

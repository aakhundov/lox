#ifndef CLOX_COMMON_H
#define CLOX_COMMON_H

#include <stddef.h>

#define CLOX_ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

typedef struct clox_pos_t {
  size_t line;
  size_t col;
} clox_pos_t;

#endif

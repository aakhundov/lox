#ifndef CLOX_COMMON_H
#define CLOX_COMMON_H

#include <stddef.h>

typedef struct {
  size_t line;
  size_t col;
} clox_pos_t;

#define CLOX_ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

#endif

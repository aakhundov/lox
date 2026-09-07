#ifndef CLOX_COMMON_H
#define CLOX_COMMON_H

#include <stddef.h>

#ifndef CLOX_LIBRARY
#define CLOX_LIBRARY 1
#endif

#ifndef CLOX_NAN_BOXING
#define CLOX_NAN_BOXING 1
#endif

#define CLOX_ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

typedef struct clox_pos_t {
  size_t line;
  size_t col;
} clox_pos_t;

#endif

#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include <stddef.h>

#define CLOX_INITIAL_SIZE 8

#define CLOX_GROW_SIZE(size)                                                                       \
  /* overflow here isn't feasible: realloc will fail much earlier than SIZE_MAX */                 \
  ((size) < CLOX_INITIAL_SIZE ? CLOX_INITIAL_SIZE : (size) * 2) // doubling

#define CLOX_GROW_ARRAY(type, pointer, old_size, new_size)                                         \
  (type *)clox_reallocate((pointer), sizeof(type) * (old_size), sizeof(type) * (new_size))

#define CLOX_FREE_ARRAY(type, pointer, old_size)                                                   \
  (type *)clox_reallocate((pointer), sizeof(type) * (old_size), 0) // free on zero

void *clox_reallocate(void *pointer, size_t old_bytes, size_t new_bytes);

#endif

#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include <stddef.h>

#include "table.h"
#include "value.h"

// in the macros below, overflow isn't feasible:
// allocator will fail earlier than SIZE_MAX is hit

#define CLOX_OBJECT_FREE(alloc, type, obj) clox_reallocate(alloc, (obj), sizeof(type), 0)

#define CLOX_ARRAY_ALLOCATE(alloc, type, size)                                                     \
  (type *)clox_reallocate(alloc, NULL, 0, sizeof(type) * (size))

#define CLOX_ARRAY_FREE(alloc, type, pointer, old_size)                                            \
  (type *)clox_reallocate(alloc, (pointer), sizeof(type) * (old_size), 0) // free on zero

#define CLOX_ARRAY_INITIAL_SIZE 8

#define CLOX_ARRAY_GROW_SIZE(size)                                                                 \
  ((size) < CLOX_ARRAY_INITIAL_SIZE ? CLOX_ARRAY_INITIAL_SIZE : (size) * 2) // doubling

#define CLOX_ARRAY_GROW(alloc, type, pointer, old_size, new_size)                                  \
  (type *)clox_reallocate(alloc, (pointer), sizeof(type) * (old_size), sizeof(type) * (new_size))

typedef struct clox_allocator_t clox_allocator_t;

typedef void clox_mark_callback_t(clox_allocator_t *alloc, void *ctx);

typedef struct clox_mark_callback_node_t {
  clox_mark_callback_t *callback;
  void *context;
  struct clox_mark_callback_node_t *next;
} clox_mark_callback_node_t;

typedef struct clox_allocator_t {
  clox_object_t *head;
  size_t allocated_size;
  clox_table_t strings;
  clox_mark_callback_node_t *mark_callbacks;
} clox_allocator_t;

void clox_allocator_init(clox_allocator_t *alloc);
void clox_allocator_free(clox_allocator_t *alloc);
void clox_allocator_mark_value(clox_allocator_t *alloc, clox_value_t val);
void clox_allocator_register_mark_callback(clox_allocator_t *alloc, clox_mark_callback_t *callback,
                                           void *ctx);

void *clox_reallocate(clox_allocator_t *a, void *pointer, size_t old_bytes, size_t new_bytes);

#endif

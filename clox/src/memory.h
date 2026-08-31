#ifndef CLOX_MEMORY_H
#define CLOX_MEMORY_H

#include <stddef.h>

#include "table.h"
#include "value.h"

#define CLOX_GC_MIN_SIZE 65536
#define CLOX_GC_FIRST_SIZE 1048576
#define CLOX_GC_HEAP_GROW_FACTOR 2

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
  // allocated
  clox_object_t *objects;
  size_t allocated_size;
  size_t next_gc_size;
  // interned strings
  clox_table_t strings;
  // callbacks for marking during GC
  clox_mark_callback_node_t *mark_callbacks;
  // GC worklist
  size_t gray_length;
  size_t gray_capacity;
  clox_object_t **gray_stack;
  // non-GC storage
  size_t durable_length;
  size_t durable_capacity;
  clox_object_t **durable_stack;
} clox_allocator_t;

void clox_allocator_init(clox_allocator_t *alloc);
void clox_allocator_free(clox_allocator_t *alloc);

void clox_mark_value(clox_allocator_t *alloc, clox_value_t val);
void clox_mark_object(clox_allocator_t *alloc, clox_object_t *obj);

// register fn returns a void* handle that must be later passed to unregister fn
void *clox_register_mark_callback(clox_allocator_t *alloc, clox_mark_callback_t *callback,
                                  void *ctx);
bool clox_unregister_mark_callback(clox_allocator_t *alloc, void *handle);

void clox_push_durable(clox_allocator_t *alloc, clox_object_t *obj);
void clox_pop_durable(clox_allocator_t *alloc);

void *clox_reallocate(clox_allocator_t *a, void *pointer, size_t old_bytes, size_t new_bytes);

void clox_collect_garbage(clox_allocator_t *a);

#endif

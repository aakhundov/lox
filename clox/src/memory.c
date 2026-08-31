#include "memory.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "debug.h"
#include "error.h"
#include "object.h"
#include "table.h"
#include "value.h"

static inline void free_object(clox_allocator_t *a, clox_object_t *obj) {
#if CLOX_DEBUG_ALLOCATION
  printf("---- FREE ");
  clox_object_repr_printf(CLOX_OBJECT(obj));
  printf(" @ %p\n", (void *)(obj));
#endif

  switch (obj->type) {
  case OBJ_STRING: {
    clox_string_t *string = (clox_string_t *)obj;
    CLOX_ARRAY_FREE(a, char, (void *)string->chars, string->length + 1);
    CLOX_OBJECT_FREE(a, clox_string_t, string);
    break;
  }
  case OBJ_FUNCTION: {
    clox_function_t *function = (clox_function_t *)obj;
    CLOX_ARRAY_FREE(a, char, (void *)function->name, strlen(function->name) + 1);
    clox_chunk_free(&function->chunk);
    CLOX_OBJECT_FREE(a, clox_function_t, function);
    break;
  }
  case OBJ_NATIVE: {
    clox_native_t *native = (clox_native_t *)obj;
    CLOX_ARRAY_FREE(a, char, (void *)native->name, strlen(native->name) + 1);
    CLOX_OBJECT_FREE(a, clox_native_t, native);
    break;
  }
  case OBJ_UPVALUE: {
    clox_upvalue_t *upvalue = (clox_upvalue_t *)obj;
    CLOX_OBJECT_FREE(a, clox_upvalue_t, upvalue);
    break;
  }
  case OBJ_CLOSURE: {
    clox_closure_t *closure = (clox_closure_t *)obj;
    CLOX_ARRAY_FREE(a, clox_upvalue_t *, (void *)closure->upvalues, closure->upvalue_count);
    CLOX_OBJECT_FREE(a, clox_closure_t, closure);
    break;
  }
  }
}

static inline void *realloc_or_error(void *ptr, size_t new_bytes) {
  void *result = realloc(ptr, new_bytes);
  if (result == NULL) {
    CLOX_FATAL_ERROR("memory allocation failed", CLOX_EX_OSERR);
  }
  return result;
}

void *clox_reallocate(clox_allocator_t *a, void *pointer, size_t old_bytes, size_t new_bytes) {
  assert(a != NULL);

  if (new_bytes > old_bytes) {
    size_t allocated = new_bytes - old_bytes;
    a->allocated_size += allocated;
  } else {
    size_t freed = old_bytes - new_bytes;
    assert(a->allocated_size >= freed);
    a->allocated_size -= freed;
  }

  if (new_bytes == 0) {
#if CLOX_DEBUG_MEMORY
    if (old_bytes > 0) {
      printf("---- MEMO %p: free %zu [%zu]\n", pointer, old_bytes, a->allocated_size);
    }
#endif

    free(pointer);
    return NULL;
  }

  void *result = realloc_or_error(pointer, new_bytes);

#if CLOX_DEBUG_MEMORY
  if (old_bytes == 0) {
    printf("---- MEMO %p: malloc %zu [%zu]\n", result, new_bytes, a->allocated_size);
  } else if (new_bytes != old_bytes) {
    printf("---- MEMO %p: realloc %zu (%p) -> %zu [%zu]\n", result, old_bytes, pointer, new_bytes,
           a->allocated_size);
  }
#endif

  return result;
}

void clox_allocator_init(clox_allocator_t *alloc) {
  alloc->head = NULL;
  alloc->allocated_size = 0;
  alloc->mark_callbacks = NULL;

  clox_table_init(&alloc->strings, alloc);
}

void clox_allocator_free(clox_allocator_t *alloc) {
  clox_table_free(&alloc->strings);

  clox_object_t *running = alloc->head;
  while (running != NULL) {
    clox_object_t *next = running->next;
    free_object(alloc, running);
    running = next;
  }
  alloc->head = NULL;

  clox_mark_callback_node_t *head = alloc->mark_callbacks;
  while (head != NULL) {
    clox_mark_callback_node_t *next = head->next;
    free(head);
    head = next;
  }
  alloc->mark_callbacks = NULL;

  // all allocated memory must be freed
  assert(alloc->allocated_size == 0);
}

void clox_allocator_mark_value(clox_allocator_t *alloc, clox_value_t val) {
}

void clox_allocator_register_mark_callback(clox_allocator_t *alloc, clox_mark_callback_t *callback,
                                           void *ctx) {
  clox_mark_callback_node_t *node = realloc_or_error(NULL, sizeof *node);
  node->callback = callback;
  node->context = ctx;
  node->next = alloc->mark_callbacks;
  alloc->mark_callbacks = node;
}

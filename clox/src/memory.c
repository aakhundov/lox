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

static inline void mark_object(clox_allocator_t *a, clox_object_t *obj) {
  if (obj == NULL || obj->is_marked) {
    return;
  }

#if CLOX_DEBUG_GC
  printf("---- GC mark ");
  clox_object_repr_printf(CLOX_OBJECT(obj));
  printf(" @ %p\n", (void *)(obj));
#endif

  obj->is_marked = true;

  if (obj->type == OBJ_STRING || obj->type == OBJ_NATIVE) {
    // no transitively linked objects to mark
    return;
  }

  if (a->gray_length == a->gray_capacity) {
    a->gray_capacity = CLOX_ARRAY_GROW_SIZE(a->gray_capacity);
    a->gray_stack = (clox_object_t **)realloc_or_error((void *)a->gray_stack,
                                                       a->gray_capacity * sizeof(*a->gray_stack));
  }

  // add object to the gray stack
  a->gray_stack[a->gray_length++] = obj;
}

static inline void mark_value(clox_allocator_t *a, clox_value_t val) {
  if (CLOX_IS_OBJECT(val)) {
    mark_object(a, CLOX_AS_OBJECT(val));
  }
}

static inline void mark_roots(clox_allocator_t *a) {
  // call every registered mark callback
  clox_mark_callback_node_t *node = a->mark_callbacks;
  while (node != NULL) {
    node->callback(a, node->context);
    node = node->next;
  }

  // durables are safe from GC
  for (size_t i = 0; i < a->durable_length; i++) {
    mark_object(a, a->durable_stack[i]);
  }
}

static inline void mark_value_array(clox_allocator_t *a, clox_value_array_t *arr) {
  for (size_t i = 0; i < arr->length; i++) {
    mark_value(a, arr->values[i]);
  }
}

static inline void blacken_object(clox_allocator_t *a, clox_object_t *obj) {
#if CLOX_DEBUG_GC
  printf("---- GC blacken ");
  clox_object_repr_printf(CLOX_OBJECT(obj));
  printf(" @ %p\n", (void *)(obj));
#endif

  switch (obj->type) {
  case OBJ_UPVALUE:
    mark_value(a, ((clox_upvalue_t *)obj)->closed);
    break;
  case OBJ_FUNCTION:
    mark_value_array(a, &((clox_function_t *)obj)->chunk.constants);
    break;
  case OBJ_CLOSURE: {
    clox_closure_t *closure = (clox_closure_t *)obj;
    mark_object(a, (clox_object_t *)closure->function);
    for (size_t i = 0; i < closure->upvalue_count; i++) {
      mark_object(a, (clox_object_t *)closure->upvalues[i]);
    }
    break;
  }
  case OBJ_STRING:
  case OBJ_NATIVE:
    assert(0 && "unreachable");
  }
}

static inline void trace_refs(clox_allocator_t *a) {
  while (a->gray_length > 0) {
    clox_object_t *obj = a->gray_stack[--a->gray_length];
    blacken_object(a, obj);
  }
}

static inline void sweep(clox_allocator_t *a) {
  clox_object_t *previous = NULL;
  clox_object_t *running = a->objects;
  while (running != NULL) {
    if (running->is_marked) {
      // lower the flag for next GC
      running->is_marked = false;
      previous = running;
      running = running->next;
    } else {
      clox_object_t *unmarked = running;

      running = running->next;
      if (previous != NULL) {
        previous->next = running;
      } else {
        a->objects = running;
      }

#if CLOX_DEBUG_GC
      printf("---- GC sweep ");
      clox_object_repr_printf(CLOX_OBJECT(unmarked));
      printf(" @ %p\n", (void *)(unmarked));
#endif

      free_object(a, unmarked);
    }
  }
}

void clox_collect_garbage(clox_allocator_t *a) {
#if CLOX_DEBUG_GC
  printf("---- GC begin\n");
  size_t before = a->allocated_size;
#endif

  mark_roots(a);
  trace_refs(a);

  // a table holds weak rereferences to the keys:
  // remove unmarked interned strings before sweeping
  clox_table_remove_unmarked_keys(&a->strings);

  sweep(a);

  a->next_gc_size = a->allocated_size * CLOX_GC_HEAP_GROW_FACTOR;
  if (a->next_gc_size < CLOX_GC_MIN_SIZE) {
    a->next_gc_size = CLOX_GC_MIN_SIZE;
  }

#if CLOX_DEBUG_GC
  size_t after = a->allocated_size;
  assert(after <= before);
  printf("---- GC end\n");
  printf("---- GC collected %zu (%zu -> %zu)\n", before - after, before, after);
  printf("---- GC next at %zu\n", a->next_gc_size);
#endif
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

  if (new_bytes > old_bytes) {
    // GC only on allocation
#if CLOX_STRESS_GC
    clox_collect_garbage(a);
#else
    if (a->allocated_size >= a->next_gc_size) {
      clox_collect_garbage(a);
    }
#endif
  }

  if (new_bytes == 0) {
#if CLOX_DEBUG_MEMORY
    if (old_bytes > 0) {
      printf("---- MEM %p: free %zu [%zu]\n", pointer, old_bytes, a->allocated_size);
    }
#endif

    free(pointer);
    return NULL;
  }

  void *result = realloc_or_error(pointer, new_bytes);

#if CLOX_DEBUG_MEMORY
  if (old_bytes == 0) {
    printf("---- MEM %p: malloc %zu [%zu]\n", result, new_bytes, a->allocated_size);
  } else if (new_bytes != old_bytes) {
    printf("---- MEM %p: realloc %zu (%p) -> %zu [%zu]\n", result, old_bytes, pointer, new_bytes,
           a->allocated_size);
  }
#endif

  return result;
}

void clox_allocator_init(clox_allocator_t *alloc) {
  assert(alloc != NULL);

  alloc->objects = NULL;
  alloc->allocated_size = 0;
  alloc->next_gc_size = CLOX_GC_FIRST_SIZE;
  alloc->mark_callbacks = NULL;
  alloc->gray_length = 0;
  alloc->gray_capacity = 0;
  alloc->gray_stack = NULL;
  alloc->durable_length = 0;
  alloc->durable_capacity = 0;
  alloc->durable_stack = NULL;

  clox_table_init(&alloc->strings, alloc);
}

void clox_allocator_free(clox_allocator_t *alloc) {
  assert(alloc != NULL);

  clox_table_free(&alloc->strings);

  clox_object_t *running = alloc->objects;
  while (running != NULL) {
    clox_object_t *next = running->next;
    free_object(alloc, running);
    running = next;
  }
  alloc->objects = NULL;

  // all allocated memory must be freed
  assert(alloc->allocated_size == 0);

  clox_mark_callback_node_t *head = alloc->mark_callbacks;
  while (head != NULL) {
    clox_mark_callback_node_t *next = head->next;
    free(head); // using C's free, as malloc-ed
    head = next;
  }
  alloc->mark_callbacks = NULL;

  // using C's free, as malloc-ed
  free((void *)alloc->gray_stack);
  free((void *)alloc->durable_stack);

  alloc->gray_length = 0;
  alloc->gray_capacity = 0;
  alloc->gray_stack = NULL;
  alloc->durable_length = 0;
  alloc->durable_capacity = 0;
  alloc->durable_stack = NULL;
}

void clox_mark_value(clox_allocator_t *alloc, clox_value_t val) {
  assert(alloc != NULL);

  mark_value(alloc, val);
}

void clox_mark_object(clox_allocator_t *alloc, clox_object_t *obj) {
  assert(alloc != NULL);
  assert(obj != NULL);

  mark_object(alloc, obj);
}

void *clox_register_mark_callback(clox_allocator_t *alloc, clox_mark_callback_t *callback,
                                  void *ctx) {
  assert(alloc != NULL);
  assert(callback != NULL);

  clox_mark_callback_node_t *node = realloc_or_error(NULL, sizeof *node);
  node->callback = callback;
  node->context = ctx;
  node->next = alloc->mark_callbacks;
  alloc->mark_callbacks = node;

  return node; // handle
}

bool clox_unregister_mark_callback(clox_allocator_t *alloc, void *handle) {
  assert(alloc != NULL);
  assert(handle != NULL);

  clox_mark_callback_node_t *previous = NULL;
  clox_mark_callback_node_t *running = alloc->mark_callbacks;
  while (running != NULL) {
    if (running == handle) {
      if (previous != NULL) {
        previous->next = running->next;
      } else {
        alloc->mark_callbacks = running->next;
      }
      free(running); // using C's free, as malloc-ed
      return true;   // found
    }
    previous = running;
    running = running->next;
  }

  return false; // not found
}

void clox_push_durable(clox_allocator_t *alloc, clox_object_t *obj) {
  if (alloc->durable_length == alloc->durable_capacity) {
    alloc->durable_capacity = CLOX_ARRAY_GROW_SIZE(alloc->durable_capacity);
    alloc->durable_stack = (clox_object_t **)realloc_or_error(
        (void *)alloc->durable_stack, alloc->durable_capacity * sizeof(*alloc->durable_stack));
  }

  alloc->durable_stack[alloc->durable_length++] = obj;
}

void clox_pop_durable(clox_allocator_t *alloc) {
  assert(alloc->durable_length > 0);
  alloc->durable_length--;
}

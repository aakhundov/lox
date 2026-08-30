#include "object.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "debug.h"
#include "memory.h"
#include "table.h"
#include "value.h"

#define ALLOCATE_OBJECT(alloc, c_type, obj_type)                                                   \
  (c_type *)allocate_object(alloc, sizeof(c_type), (obj_type))

#if CLOX_DEBUG_ALLOCATION
#define LOG_ALLOCATION_EVENT(event, object)                                                        \
  do {                                                                                             \
    printf("---- " #event " ");                                                                    \
    clox_object_repr_printf(CLOX_OBJECT(object));                                                  \
    printf(" @ %p\n", (void *)(object));                                                           \
  } while (0)
#else
#define LOG_ALLOCATION_EVENT(event, object) (void)0
#endif

#define LOG_ALLOCATE(object) LOG_ALLOCATION_EVENT(ALLC, object)
#define LOG_FREE(object) LOG_ALLOCATION_EVENT(FREE, object)

static inline clox_object_t *allocate_object(clox_allocator_t *a, size_t size,
                                             clox_object_type_t type) {
  clox_object_t *obj = (clox_object_t *)clox_reallocate(NULL, 0, size);
  obj->type = type;

  // add to allocator
  obj->next = a->head;
  a->head = obj;

  return obj;
}

static inline void free_object(clox_object_t *obj) {
  LOG_FREE(obj);
  switch (obj->type) {
  case OBJ_STRING: {
    clox_string_t *string = (clox_string_t *)obj;
    CLOX_FREE_ARRAY(char, (void *)string->chars, string->length + 1);
    CLOX_FREE_OBJECT(clox_string_t, string);
    break;
  }
  case OBJ_FUNCTION: {
    clox_function_t *function = (clox_function_t *)obj;
    CLOX_FREE_ARRAY(char, (void *)function->name, strlen(function->name) + 1);
    clox_chunk_free(&function->chunk);
    CLOX_FREE_OBJECT(clox_function_t, function);
    break;
  }
  case OBJ_NATIVE: {
    clox_native_t *native = (clox_native_t *)obj;
    CLOX_FREE_ARRAY(char, (void *)native->name, strlen(native->name) + 1);
    CLOX_FREE_OBJECT(clox_native_t, native);
    break;
  }
  case OBJ_UPVALUE: {
    clox_upvalue_t *upvalue = (clox_upvalue_t *)obj;
    CLOX_FREE_OBJECT(clox_upvalue_t, upvalue);
    break;
  }
  case OBJ_CLOSURE: {
    clox_closure_t *closure = (clox_closure_t *)obj;
    CLOX_FREE_ARRAY(clox_upvalue_t *, (void *)closure->upvalues, closure->upvalue_count);
    CLOX_FREE_OBJECT(clox_closure_t, closure);
    break;
  }
  }
}

static inline const char *duplicate_cstring(const char *str, size_t length) {
  char *dup = CLOX_ALLOCATE_ARRAY(char, length + 1);
  memcpy(dup, str, length);
  dup[length] = '\0'; // NUL
  return dup;
}

static inline const clox_string_t *allocate_string(clox_allocator_t *a, const char *chars,
                                                   size_t length, clox_hash_t hash) {
  clox_string_t *string = ALLOCATE_OBJECT(a, clox_string_t, OBJ_STRING);
  string->chars = chars;
  string->length = length;
  string->hash = hash;

  // intern the freshly allocated string object
  clox_table_set(&a->strings, string, CLOX_NIL);

  LOG_ALLOCATE(string);
  return string;
}

#define FNV_1A_HASH_INIT 2166136261U
#define FNV_1A_HASH_FACTOR 16777619U

static inline clox_hash_t hash_string(const char *chars, size_t length) {
  clox_hash_t hash = FNV_1A_HASH_INIT;
  for (size_t i = 0; i < length; i++) {
    hash ^= (unsigned char)chars[i];
    hash *= FNV_1A_HASH_FACTOR;
  }
  return hash;
}

const clox_string_t *clox_string_copy(clox_allocator_t *a, const char *chars, size_t length) {
  clox_hash_t hash = hash_string(chars, length);
  const clox_string_t *interned = clox_table_get_key_string(&a->strings, chars, length, hash);
  if (interned != NULL) {
    return interned;
  }

  // chars are copied into new string object
  const char *chars_copy = duplicate_cstring(chars, length);
  return allocate_string(a, chars_copy, length, hash);
}

const clox_string_t *clox_string_move(clox_allocator_t *a, const char *chars, size_t length) {
  clox_hash_t hash = hash_string(chars, length);
  const clox_string_t *interned = clox_table_get_key_string(&a->strings, chars, length, hash);
  if (interned != NULL) {
    // free the moved chars as not needed
    CLOX_FREE_ARRAY(char, (void *)chars, length + 1);
    return interned;
  }

  // ownership of chars is moved to new string object
  return allocate_string(a, chars, length, hash);
}

clox_value_t clox_string_concat(clox_allocator_t *a, clox_value_t s1, clox_value_t s2) {
  assert(CLOX_IS_STRING(s1));
  assert(CLOX_IS_STRING(s2));

  const clox_string_t *left = CLOX_AS_STRING(s1);
  const clox_string_t *right = CLOX_AS_STRING(s2);

  size_t total_length = left->length + right->length;
  char *chars = CLOX_ALLOCATE_ARRAY(char, total_length + 1);
  memcpy(chars, left->chars, left->length);
  memcpy(chars + left->length, right->chars, right->length);
  chars[total_length] = '\0';

  return CLOX_STRING_MOVE(a, chars, total_length);
}

clox_function_t *clox_new_function(clox_allocator_t *alloc, const char *name, size_t length,
                                   size_t arity, const char *file_name, const char *source) {
  assert(name != NULL);

  clox_function_t *function = ALLOCATE_OBJECT(alloc, clox_function_t, OBJ_FUNCTION);
  function->name = duplicate_cstring(name, length);
  function->arity = arity;
  function->file_name = file_name;
  function->source = source;
  function->upvalue_count = 0;
  clox_chunk_init(&function->chunk);

  LOG_ALLOCATE(function);
  return function;
}

clox_native_t *clox_new_native(clox_allocator_t *alloc, const char *name, size_t arity,
                               clox_native_fn_t *fn) {
  assert(name != NULL);
  assert(fn != NULL);

  clox_native_t *native = ALLOCATE_OBJECT(alloc, clox_native_t, OBJ_NATIVE);
  native->name = duplicate_cstring(name, strlen(name));
  native->arity = arity;
  native->function = fn;

  LOG_ALLOCATE(native);
  return native;
}

clox_upvalue_t *clox_new_upvalue(clox_allocator_t *alloc, clox_value_t *location) {
  assert(location != NULL);

  clox_upvalue_t *upvalue = ALLOCATE_OBJECT(alloc, clox_upvalue_t, OBJ_UPVALUE);
  upvalue->location = location;
  upvalue->closed = CLOX_NIL;
  upvalue->next = NULL;

  LOG_ALLOCATE(upvalue);
  return upvalue;
}

clox_closure_t *clox_new_closure(clox_allocator_t *alloc, const clox_function_t *function) {
  assert(function != NULL);

  // allocate and NULL-initialize array of pointers to upvalues;
  // the upvalues themselves are *not owned*, just referenced
  clox_upvalue_t **upvalues = CLOX_ALLOCATE_ARRAY(clox_upvalue_t *, function->upvalue_count);
  for (size_t i = 0; i < function->upvalue_count; i++) {
    upvalues[i] = NULL;
  }

  clox_closure_t *closure = ALLOCATE_OBJECT(alloc, clox_closure_t, OBJ_CLOSURE);
  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalue_count = function->upvalue_count;

  LOG_ALLOCATE(closure);
  return closure;
}

bool clox_object_is_truthy(clox_value_t val) {
  assert(CLOX_IS_OBJECT(val));

  switch (CLOX_AS_OBJECT(val)->type) {
  case OBJ_STRING:
    return CLOX_AS_STRING(val)->length > 0;
  case OBJ_FUNCTION:
  case OBJ_NATIVE:
  case OBJ_UPVALUE:
  case OBJ_CLOSURE:
    return true;
  }
}

bool clox_object_equals(clox_value_t a, clox_value_t b) {
  assert(CLOX_IS_OBJECT(a));
  assert(CLOX_IS_OBJECT(b));

  if (CLOX_AS_OBJECT(a)->type != CLOX_AS_OBJECT(b)->type) {
    return false;
  }

  switch (CLOX_AS_OBJECT(a)->type) {
  case OBJ_STRING:
    // compare raw pointers to string objects:
    // this works due to the string interning
    return CLOX_AS_STRING(a) == CLOX_AS_STRING(b);
  case OBJ_FUNCTION:
  case OBJ_NATIVE:
  case OBJ_UPVALUE:
  case OBJ_CLOSURE:
    // compare object pointers
    return CLOX_AS_OBJECT(a) == CLOX_AS_OBJECT(b);
  }
}

void clox_object_fprintf(FILE *stream, clox_value_t val) {
  assert(CLOX_IS_OBJECT(val));

  switch (CLOX_AS_OBJECT(val)->type) {
  case OBJ_STRING:
    (void)fprintf(stream, "%s", CLOX_AS_CSTRING(val));
    break;
  case OBJ_FUNCTION: {
    clox_function_t *function = CLOX_AS_FUNCTION(val);
    if (strcmp(function->name, CLOX_SCRIPT_NAME) == 0) {
      (void)fprintf(stream, "%s", CLOX_SCRIPT_NAME);
    } else {
      (void)fprintf(stream, "<fn %s>", function->name);
    }
    break;
  }
  case OBJ_NATIVE:
    (void)fprintf(stream, "<nt %s>", CLOX_AS_NATIVE(val)->name);
    break;
  case OBJ_UPVALUE: {
    clox_value_t *location = CLOX_AS_UPVALUE(val)->location;
    (void)fprintf(stream, "<up %p", (void *)location);
    if (location != NULL) {
      (void)fprintf(stream, " (");
      clox_value_repr_fprintf(stream, *location);
      (void)fprintf(stream, ")");
    }
    (void)fprintf(stream, ">");
    break;
  }
  case OBJ_CLOSURE:
    (void)fprintf(stream, "<cl %s>", CLOX_AS_CLOSURE(val)->function->name);
    break;
  }
}

void clox_object_printf(clox_value_t val) {
  clox_object_fprintf(stdout, val);
}

void clox_object_repr_fprintf(FILE *stream, clox_value_t val) {
  assert(CLOX_IS_OBJECT(val));

  switch (CLOX_AS_OBJECT(val)->type) {
  case OBJ_STRING:
    (void)fprintf(stream, "\"%s\"", CLOX_AS_CSTRING(val));
    break;
  case OBJ_FUNCTION:
  case OBJ_NATIVE:
  case OBJ_UPVALUE:
  case OBJ_CLOSURE:
    clox_object_fprintf(stream, val);
    break;
  }
}

void clox_object_repr_printf(clox_value_t val) {
  clox_object_repr_fprintf(stdout, val);
}

void clox_allocator_init(clox_allocator_t *alloc) {
  alloc->head = NULL;
  clox_table_init(&alloc->strings);
}

void clox_allocator_free(clox_allocator_t *alloc) {
  clox_table_free(&alloc->strings);
  clox_object_t *running = alloc->head;
  while (running != NULL) {
    clox_object_t *next = running->next;
    free_object(running);
    running = next;
  }
  alloc->head = NULL;
}

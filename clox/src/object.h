#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "chunk.h"
#include "error.h"
#include "value.h"

typedef enum clox_object_type_t {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_UPVALUE,
  OBJ_CLOSURE,
} clox_object_type_t;

typedef struct clox_object_t {
  clox_object_type_t type;
  struct clox_object_t *next;
  bool is_marked;
} clox_object_t;

typedef struct clox_string_t {
  clox_object_t object;
  size_t length;     // without NUL
  const char *chars; // (length + 1) long
  clox_hash_t hash;  // fixed per object
} clox_string_t;

#define CLOX_SCRIPT_NAME "<script>"

typedef struct clox_function_t {
  clox_object_t object;
  const char *name; // NUL-terminated
  size_t arity;
  clox_chunk_t chunk;
  const char *file_name; // not owned
  const char *source;    // not owned
  size_t upvalue_count;
} clox_function_t;

typedef union clox_native_result_t {
  clox_value_t value;
  char error_msg[MAX_ERROR_LENGTH + 1];
} clox_native_result_t;

typedef struct clox_vm_t clox_vm_t;
typedef bool clox_native_fn_t(size_t arg_count, clox_value_t *args, clox_native_result_t *result,
                              clox_vm_t *vm);

typedef struct clox_native_t {
  clox_object_t object;
  const char *name; // NUL-terminated
  size_t arity;     // SIZE_MAX for variadic
  clox_native_fn_t *function;
} clox_native_t;

typedef struct clox_upvalue_t {
  clox_object_t object;
  clox_value_t *location;
  clox_value_t closed;
  struct clox_upvalue_t *next;
} clox_upvalue_t;

typedef struct clox_closure_t {
  clox_object_t object;
  const clox_function_t *function;
  clox_upvalue_t **upvalues;
  size_t upvalue_count;
} clox_closure_t;

#define CLOX_OBJECT_TYPE(val) (CLOX_AS_OBJECT(val)->type)

#define CLOX_STRING_COPY(alloc, chars, length) CLOX_OBJECT(clox_string_copy(alloc, chars, length))
#define CLOX_STRING_MOVE(alloc, chars, length) CLOX_OBJECT(clox_string_move(alloc, chars, length))
#define CLOX_FUNCTION(alloc, name, length, arity, file_name, source)                               \
  CLOX_OBJECT(clox_new_function(alloc, name, length, arity, file_name, source))
#define CLOX_NATIVE(alloc, name, arity, fn) CLOX_OBJECT(clox_new_native(alloc, name, arity, fn))
#define CLOX_UPVALUE(alloc, location) CLOX_OBJECT(clox_new_upvalue(alloc, location))
#define CLOX_CLOSURE(alloc, function) CLOX_OBJECT(clox_new_closure(alloc, function))

#define CLOX_IS_STRING(val) is_object_type((val), OBJ_STRING)
#define CLOX_IS_FUNCTION(val) is_object_type((val), OBJ_FUNCTION)
#define CLOX_IS_NATIVE(val) is_object_type((val), OBJ_NATIVE)
#define CLOX_IS_UPVALUE(val) is_object_type((val), OBJ_UPVALUE)
#define CLOX_IS_CLOSURE(val) is_object_type((val), OBJ_CLOSURE)

#define CLOX_AS_STRING(val) ((const clox_string_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_CSTRING(val) (((const clox_string_t *)CLOX_AS_OBJECT(val))->chars)
#define CLOX_AS_FUNCTION(val) ((clox_function_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_NATIVE(val) ((clox_native_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_UPVALUE(val) ((clox_upvalue_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_CLOSURE(val) ((clox_closure_t *)CLOX_AS_OBJECT(val))

bool clox_object_is_truthy(clox_value_t val);
bool clox_object_equals(clox_value_t a, clox_value_t b);
void clox_object_fprintf(FILE *stream, clox_value_t val);
void clox_object_printf(clox_value_t val);
void clox_object_repr_fprintf(FILE *stream, clox_value_t val);
void clox_object_repr_printf(clox_value_t val);

// (chars) points to at least (length) chars with no NUL among them
const clox_string_t *clox_string_copy(clox_allocator_t *alloc, const char *chars, size_t length);
// (chars) is a heap-allocated buffer of size (length + 1)
// holding a NUL-terminated string of (length) chars and
// must have been allocated with the (alloc)
const clox_string_t *clox_string_move(clox_allocator_t *alloc, const char *chars, size_t length);
clox_value_t clox_string_concat(clox_allocator_t *alloc, clox_value_t s1, clox_value_t s2);

// (name) should be NULL for script
// (length) is the number of chars in (name) not counting NUL
clox_function_t *clox_new_function(clox_allocator_t *alloc, const char *name, size_t length,
                                   size_t arity, const char *file_name, const char *source);

// (name) is NULL-terminated C-string
clox_native_t *clox_new_native(clox_allocator_t *alloc, const char *name, size_t arity,
                               clox_native_fn_t *fn);

clox_upvalue_t *clox_new_upvalue(clox_allocator_t *alloc, clox_value_t *location);
clox_closure_t *clox_new_closure(clox_allocator_t *alloc, const clox_function_t *function);

static inline bool is_object_type(clox_value_t val, clox_object_type_t type) {
  return CLOX_IS_OBJECT(val) && CLOX_OBJECT_TYPE(val) == type;
}

#endif

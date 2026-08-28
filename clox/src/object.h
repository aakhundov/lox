#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "chunk.h"
#include "error.h"
#include "table.h"
#include "value.h"

typedef struct {
  clox_object_t *head;
  clox_table_t strings;
} clox_allocator_t;

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE,
} clox_object_type_t;

typedef struct clox_object_t {
  clox_object_type_t type;
  clox_object_t *next;
} clox_object_t;

typedef struct clox_string_t {
  // "inherits" from clox_object_t
  clox_object_t object;
  // string-specific fields
  size_t length;     // without NUL
  const char *chars; // (length + 1) long
  clox_hash_t hash;  // fixed per object
} clox_string_t;

typedef struct clox_function_t {
  // "inherits" from clox_object_t
  clox_object_t object;
  // function-specific fields
  const char *name; // NUL-terminated
  size_t arity;
  clox_chunk_t chunk;
} clox_function_t;

typedef union {
  clox_value_t value;
  char error_msg[MAX_ERROR_LENGTH + 1];
} clox_native_result_t;

typedef bool clox_native_fn_t(size_t arg_count, clox_value_t *args, clox_native_result_t *result);

typedef struct clox_native_t {
  // "inherits" from clox_object_t
  clox_object_t object;
  // native-specific fields
  const char *name; // NUL-terminated
  size_t arity;     // SIZE_MAX for variadic
  clox_native_fn_t *function;
} clox_native_t;

#define CLOX_OBJECT_TYPE(val) (CLOX_AS_OBJECT(val)->type)

#define CLOX_STRING_COPY(alloc, chars, length) CLOX_OBJECT(clox_string_copy(alloc, chars, length))
#define CLOX_STRING_MOVE(alloc, chars, length) CLOX_OBJECT(clox_string_move(alloc, chars, length))
#define CLOX_FUNCTION(alloc, name, length, arity)                                                  \
  CLOX_OBJECT(clox_new_function(alloc, name, length, arity))
#define CLOX_NATIVE(alloc, name, arity, fn) CLOX_OBJECT(clox_new_native(alloc, name, arity, fn))

#define CLOX_IS_STRING(val) is_object_type((val), OBJ_STRING)
#define CLOX_IS_FUNCTION(val) is_object_type((val), OBJ_FUNCTION)
#define CLOX_IS_NATIVE(val) is_object_type((val), OBJ_NATIVE)

#define CLOX_AS_STRING(val) ((const clox_string_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_CSTRING(val) (((const clox_string_t *)CLOX_AS_OBJECT(val))->chars)
#define CLOX_AS_FUNCTION(val) ((clox_function_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_NATIVE(val) ((clox_native_t *)CLOX_AS_OBJECT(val))

void clox_allocator_init(clox_allocator_t *alloc);
void clox_allocator_free(clox_allocator_t *alloc);

bool clox_object_is_truthy(clox_value_t val);
bool clox_object_equals(clox_value_t a, clox_value_t b);
void clox_object_fprintf(FILE *stream, clox_value_t val);
void clox_object_printf(clox_value_t val);
void clox_object_repr_fprintf(FILE *stream, clox_value_t val);
void clox_object_repr_printf(clox_value_t val);

// (chars) points to at least (length) chars with no NUL among them
const clox_string_t *clox_string_copy(clox_allocator_t *alloc, const char *chars, size_t length);
// (chars) is a heap-allocated buffer of size (length + 1)
// holding a NUL-terminated string of (length) chars
const clox_string_t *clox_string_move(clox_allocator_t *alloc, const char *chars, size_t length);
clox_value_t clox_string_concat(clox_allocator_t *alloc, clox_value_t s1, clox_value_t s2);

// (name) should be NULL for script
// (length) is the number of chars in (name) not counting NUL
clox_function_t *clox_new_function(clox_allocator_t *alloc, const char *name, size_t length,
                                   size_t arity);

// (name) is NULL-terminated C-string
clox_native_t *clox_new_native(clox_allocator_t *alloc, const char *name, size_t arity,
                               clox_native_fn_t *fn);

static inline bool is_object_type(clox_value_t val, clox_object_type_t type) {
  return CLOX_IS_OBJECT(val) && CLOX_OBJECT_TYPE(val) == type;
}

#endif

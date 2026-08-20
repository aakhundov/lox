#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "table.h"
#include "value.h"

typedef struct {
  clox_object_t *head;
  clox_table_t strings;
} clox_allocator_t;

typedef enum {
  OBJ_STRING,
} clox_object_type_t;

typedef struct clox_object_t {
  clox_object_type_t type;
  clox_object_t *next;
} clox_object_t;

typedef struct clox_string_t {
  // "inherits" from clox_object_t
  clox_object_t object;
  // string-specific fields
  size_t length;    // without NUL
  char *chars;      // (length + 1) long
  clox_hash_t hash; // fixed per object
} clox_string_t;

#define CLOX_OBJECT_TYPE(val) (CLOX_AS_OBJECT(val)->type)

#define CLOX_STRING_COPY(alloc, chars, length) CLOX_OBJECT(clox_string_copy(alloc, chars, length))
#define CLOX_STRING_MOVE(alloc, chars, length) CLOX_OBJECT(clox_string_move(alloc, chars, length))

#define CLOX_IS_STRING(val) is_object_type((val), OBJ_STRING)

#define CLOX_AS_STRING(val) ((const clox_string_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_CSTRING(val) (((const clox_string_t *)CLOX_AS_OBJECT(val))->chars)

void clox_allocator_init(clox_allocator_t *alloc);
void clox_allocator_free(clox_allocator_t *alloc);

bool clox_object_is_truthy(clox_value_t val);
bool clox_object_equals(clox_value_t a, clox_value_t b);
void clox_object_fprintf(FILE *stream, clox_value_t val);
void clox_object_printf(clox_value_t val);

// (chars) points to at least (length) chars with no NUL among them
const clox_string_t *clox_string_copy(clox_allocator_t *alloc, const char *chars, size_t length);
// (chars) is a heap-allocated buffer of size (length + 1)
// holding a NUL-terminated string of (length) chars
const clox_string_t *clox_string_move(clox_allocator_t *alloc, char *chars, size_t length);
clox_value_t clox_string_concat(clox_allocator_t *alloc, clox_value_t s1, clox_value_t s2);

static inline bool is_object_type(clox_value_t val, clox_object_type_t type) {
  return CLOX_IS_OBJECT(val) && CLOX_OBJECT_TYPE(val) == type;
}

#endif

#ifndef CLOX_OBJECT_H
#define CLOX_OBJECT_H

#include <stddef.h>

#include "value.h"

typedef enum {
  OBJ_STRING,
} clox_object_type_t;

typedef struct clox_object_t {
  clox_object_type_t type;
  clox_object_t *next;
} clox_object_t;

typedef struct {
  clox_object_t *head;
} clox_allocator_t;

typedef struct {
  // "inherits" from clox_object_t
  clox_object_t object;
  // string-specific fields
  size_t length;
  char *chars; // length + 1 char
} clox_string_t;

#define CLOX_OBJECT_TYPE(val) (CLOX_AS_OBJECT(val)->type)

#define CLOX_STRING_COPY(alloc, chars, length) CLOX_OBJECT(clox_string_copy(alloc, chars, length))
#define CLOX_STRING_MOVE(alloc, chars, length) CLOX_OBJECT(clox_string_move(alloc, chars, length))

#define CLOX_IS_STRING(val) is_object_type((val), OBJ_STRING)

#define CLOX_AS_STRING(val) ((clox_string_t *)CLOX_AS_OBJECT(val))
#define CLOX_AS_CSTRING(val) (((clox_string_t *)CLOX_AS_OBJECT(val))->chars)

bool clox_object_equals(clox_value_t a, clox_value_t b);
void clox_object_print(clox_value_t val);

void clox_allocator_init(clox_allocator_t *alloc);
void clox_allocator_free(clox_allocator_t *alloc);

clox_string_t *clox_string_copy(clox_allocator_t *alloc, const char *chars, size_t length);
clox_string_t *clox_string_move(clox_allocator_t *alloc, char *chars, size_t length);
clox_value_t clox_string_concat(clox_allocator_t *alloc, clox_value_t s1, clox_value_t s2);

static inline bool is_object_type(clox_value_t val, clox_object_type_t type) {
  return CLOX_IS_OBJECT(val) && CLOX_OBJECT_TYPE(val) == type;
}

#endif

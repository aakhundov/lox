#include "object.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "memory.h"
#include "value.h"

#define ALLOCATE_OBJECT(alloc, c_type, obj_type)                                                   \
  (c_type *)allocate_object(alloc, sizeof(c_type), (obj_type))

static clox_object_t *allocate_object(clox_allocator_t *a, size_t size, clox_object_type_t type) {
  clox_object_t *obj = (clox_object_t *)clox_reallocate(NULL, 0, size);
  obj->type = type;

  // add to allocator
  obj->next = a->head;
  a->head = obj;

  return obj;
}

static void free_object(clox_object_t *obj) {
#if CLOX_DEBUG_ALLOCATION
  printf("-- FREE [");
  clox_object_print(CLOX_OBJECT(obj));
  printf("] @ %p\n", (void *)obj);
#endif

  switch (obj->type) {
  case OBJ_STRING: {
    clox_string_t *string = (clox_string_t *)obj;
    CLOX_FREE_ARRAY(char, string->chars, string->length + 1);
    CLOX_FREE_OBJECT(clox_string_t, string);
    break;
  }
  }
}

static clox_string_t *allocate_string(clox_allocator_t *a, char *chars, size_t length) {
  clox_string_t *string = ALLOCATE_OBJECT(a, clox_string_t, OBJ_STRING);
  string->chars = chars;
  string->length = length;

#if CLOX_DEBUG_ALLOCATION
  printf("-- ALLOCATE [");
  clox_object_print(CLOX_OBJECT(string));
  printf("] @ %p\n", (void *)string);
#endif

  return string;
}

clox_string_t *clox_string_copy(clox_allocator_t *a, const char *chars, size_t length) {
  // chars are copied into new string object
  char *chars_copy = CLOX_ALLOCATE_ARRAY(char, length + 1);
  memcpy(chars_copy, chars, length);
  chars_copy[length] = '\0';
  return allocate_string(a, chars_copy, length);
}

clox_string_t *clox_string_move(clox_allocator_t *a, char *chars, size_t length) {
  // ownership of chars is moved to new string object
  return allocate_string(a, chars, length);
}

clox_value_t clox_string_concat(clox_allocator_t *a, clox_value_t s1, clox_value_t s2) {
  assert(CLOX_IS_STRING(s1));
  assert(CLOX_IS_STRING(s2));

  clox_string_t *left = CLOX_AS_STRING(s1);
  clox_string_t *right = CLOX_AS_STRING(s2);

  size_t total_length = left->length + right->length;
  char *chars = CLOX_ALLOCATE_ARRAY(char, total_length + 1);
  memcpy(chars, left->chars, left->length);
  memcpy(chars + left->length, right->chars, right->length);
  chars[total_length] = '\0';

  return CLOX_STRING_MOVE(a, chars, total_length);
}

bool clox_object_equals(clox_value_t a, clox_value_t b) {
  assert(CLOX_IS_OBJECT(a));
  assert(CLOX_IS_OBJECT(b));

  if (CLOX_AS_OBJECT(a)->type != CLOX_AS_OBJECT(b)->type) {
    return false;
  }

  switch (CLOX_AS_OBJECT(a)->type) {
  case OBJ_STRING: {
    size_t a_len = CLOX_AS_STRING(a)->length;
    size_t b_len = CLOX_AS_STRING(b)->length;
    return a_len == b_len && memcmp(CLOX_AS_STRING(a)->chars, CLOX_AS_STRING(b)->chars, a_len) == 0;
  }
  }
}

void clox_object_print(clox_value_t val) {
  assert(CLOX_IS_OBJECT(val));

  switch (CLOX_AS_OBJECT(val)->type) {
  case OBJ_STRING:
    printf("%s", CLOX_AS_CSTRING(val));
    break;
  }
}

void clox_allocator_init(clox_allocator_t *alloc) {
  alloc->head = NULL;
}

void clox_allocator_free(clox_allocator_t *alloc) {
  clox_object_t *running = alloc->head;
  while (running != NULL) {
    clox_object_t *next = running->next;
    free_object(running);
    running = next;
  }
  alloc->head = NULL;
}

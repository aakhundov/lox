#include "object.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "memory.h"
#include "table.h"
#include "value.h"

#define ALLOCATE_OBJECT(alloc, c_type, obj_type)                                                   \
  (c_type *)allocate_object(alloc, sizeof(c_type), (obj_type))

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
#if CLOX_DEBUG_ALLOCATION
  printf("---- FREE [");
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

static inline const clox_string_t *allocate_string(clox_allocator_t *a, char *chars, size_t length,
                                                   clox_hash_t hash) {
  clox_string_t *string = ALLOCATE_OBJECT(a, clox_string_t, OBJ_STRING);
  string->chars = chars;
  string->length = length;
  string->hash = hash;

  // intern the freshly allocated string object
  clox_table_set(&a->strings, string, CLOX_NIL);

#if CLOX_DEBUG_ALLOCATION
  printf("---- ALLC [");
  clox_object_print(CLOX_OBJECT(string));
  printf("] @ %p\n", (void *)string);
#endif

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
  char *chars_copy = CLOX_ALLOCATE_ARRAY(char, length + 1);
  memcpy(chars_copy, chars, length);
  chars_copy[length] = '\0';
  return allocate_string(a, chars_copy, length, hash);
}

const clox_string_t *clox_string_move(clox_allocator_t *a, char *chars, size_t length) {
  clox_hash_t hash = hash_string(chars, length);
  const clox_string_t *interned = clox_table_get_key_string(&a->strings, chars, length, hash);
  if (interned != NULL) {
    // free the moved chars as not needed
    CLOX_FREE_ARRAY(char, chars, length + 1);
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

bool clox_object_equals(clox_value_t a, clox_value_t b) {
  assert(CLOX_IS_OBJECT(a));
  assert(CLOX_IS_OBJECT(b));

  if (CLOX_AS_OBJECT(a)->type != CLOX_AS_OBJECT(b)->type) {
    return false;
  }

  switch (CLOX_AS_OBJECT(a)->type) {
  case OBJ_STRING:
    // compare raw pointers to string objects
    // this works due to the string interning
    return CLOX_AS_STRING(a) == CLOX_AS_STRING(b);
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

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

#define START_ALLOCATION(alloc, c_type, obj_type)                                                  \
  (c_type *)start_allocation(alloc, sizeof(c_type), obj_type)

// this must be the last statement in allocator functions,
// as it links the allocated object to the allocator->objects
// list which makes it visible to GC (invisible before that)
#define FINISH_ALLOCATION(alloc, c_type, obj)                                                      \
  do {                                                                                             \
    return (c_type *)finish_allocation(alloc, (clox_object_t *)(obj));                             \
  } while (0)

static inline clox_object_t *start_allocation(clox_allocator_t *a, size_t size,
                                              clox_object_type_t type) {
  clox_object_t *obj = (clox_object_t *)clox_reallocate(a, NULL, 0, size);

  obj->type = type;
  obj->is_marked = false;

  return obj;
}

static inline clox_object_t *finish_allocation(clox_allocator_t *a, clox_object_t *obj) {
  // add to allocator
  obj->next = a->objects;
  a->objects = obj;

#if CLOX_DEBUG_ALLOCATION
  printf("---- ALLC ");
  clox_object_repr_printf(CLOX_OBJECT(obj));
  printf(" @ %p\n", (void *)(obj));
#endif

  return obj;
}

static inline const char *duplicate_cstring(clox_allocator_t *a, const char *str, size_t length) {
  char *dup = CLOX_ARRAY_ALLOCATE(a, char, length + 1);
  memcpy(dup, str, length);
  dup[length] = '\0'; // NUL
  return dup;
}

static inline const clox_string_t *allocate_string(clox_allocator_t *a, const char *chars,
                                                   size_t length, clox_hash_t hash) {
  clox_string_t *string = START_ALLOCATION(a, clox_string_t, OBJ_STRING);

  string->chars = chars;
  string->length = length;
  string->hash = hash;

  // intern the freshly allocated string object
  clox_table_set(&a->strings, string, CLOX_NIL);

  FINISH_ALLOCATION(a, clox_string_t, string);
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

const clox_string_t *clox_string_copy(clox_allocator_t *alloc, const char *chars, size_t length) {
  assert(alloc != NULL);
  assert(chars != NULL);

  clox_hash_t hash = hash_string(chars, length);
  const clox_string_t *interned = clox_table_get_key_string(&alloc->strings, chars, length, hash);
  if (interned != NULL) {
    return interned;
  }

  // chars are copied into new string object
  const char *chars_copy = duplicate_cstring(alloc, chars, length);
  return allocate_string(alloc, chars_copy, length, hash);
}

const clox_string_t *clox_string_move(clox_allocator_t *alloc, const char *chars, size_t length) {
  assert(alloc != NULL);
  assert(chars != NULL);

  clox_hash_t hash = hash_string(chars, length);
  const clox_string_t *interned = clox_table_get_key_string(&alloc->strings, chars, length, hash);
  if (interned != NULL) {
    // free the moved chars as not needed
    CLOX_ARRAY_FREE(alloc, char, (void *)chars, length + 1);
    return interned;
  }

  // ownership of chars is moved to new string object
  return allocate_string(alloc, chars, length, hash);
}

clox_value_t clox_string_concat(clox_allocator_t *alloc, clox_value_t s1, clox_value_t s2) {
  assert(alloc != NULL);

  assert(CLOX_IS_STRING(s1));
  assert(CLOX_IS_STRING(s2));

  const clox_string_t *left = CLOX_AS_STRING(s1);
  const clox_string_t *right = CLOX_AS_STRING(s2);

  size_t total_length = left->length + right->length;

  clox_push_durable(alloc, CLOX_AS_OBJECT(s1));
  clox_push_durable(alloc, CLOX_AS_OBJECT(s2));
  char *chars = CLOX_ARRAY_ALLOCATE(alloc, char, total_length + 1);
  clox_pop_durable(alloc); // s2
  clox_pop_durable(alloc); // s1

  memcpy(chars, left->chars, left->length);
  memcpy(chars + left->length, right->chars, right->length);
  chars[total_length] = '\0';

  return CLOX_STRING_MOVE(alloc, chars, total_length);
}

clox_function_t *clox_new_function(clox_allocator_t *alloc, const char *name, size_t length,
                                   size_t arity, const char *file_name, const char *source) {
  assert(alloc != NULL);
  assert(name != NULL);
  assert(file_name != NULL);
  assert(source != NULL);

  clox_function_t *function = START_ALLOCATION(alloc, clox_function_t, OBJ_FUNCTION);

  function->name = duplicate_cstring(alloc, name, length);
  function->arity = arity;
  function->file_name = file_name;
  function->source = source;
  function->upvalue_count = 0;
  clox_chunk_init(&function->chunk, alloc);

  FINISH_ALLOCATION(alloc, clox_function_t, function);
}

clox_native_t *clox_new_native(clox_allocator_t *alloc, const char *name, size_t arity,
                               clox_native_fn_t *fn) {
  assert(alloc != NULL);
  assert(name != NULL);
  assert(fn != NULL);

  clox_native_t *native = START_ALLOCATION(alloc, clox_native_t, OBJ_NATIVE);

  native->name = duplicate_cstring(alloc, name, strlen(name));
  native->arity = arity;
  native->function = fn;

  FINISH_ALLOCATION(alloc, clox_native_t, native);
}

clox_upvalue_t *clox_new_upvalue(clox_allocator_t *alloc, clox_value_t *location) {
  assert(alloc != NULL);
  assert(location != NULL);

  clox_upvalue_t *upvalue = START_ALLOCATION(alloc, clox_upvalue_t, OBJ_UPVALUE);

  upvalue->location = location;
  upvalue->closed = CLOX_NIL;
  upvalue->next = NULL;

  FINISH_ALLOCATION(alloc, clox_upvalue_t, upvalue);
}

clox_closure_t *clox_new_closure(clox_allocator_t *alloc, const clox_function_t *function) {
  assert(alloc != NULL);
  assert(function != NULL);

  clox_push_durable(alloc, (clox_object_t *)function);

  // allocate and NULL-initialize array of pointers to upvalues;
  // the upvalues themselves are *not owned*, just referenced
  clox_upvalue_t **upvalues = CLOX_ARRAY_ALLOCATE(alloc, clox_upvalue_t *, function->upvalue_count);
  for (size_t i = 0; i < function->upvalue_count; i++) {
    upvalues[i] = NULL;
  }

  clox_closure_t *closure = START_ALLOCATION(alloc, clox_closure_t, OBJ_CLOSURE);

  clox_pop_durable(alloc); // function

  closure->function = function;
  closure->upvalues = upvalues;
  closure->upvalue_count = function->upvalue_count;

  FINISH_ALLOCATION(alloc, clox_closure_t, closure);
}

clox_class_t *clox_new_class(clox_allocator_t *alloc, const clox_string_t *name) {
  assert(alloc != NULL);
  assert(name != NULL);

  clox_push_durable(alloc, (clox_object_t *)name);

  clox_class_t *class_ = START_ALLOCATION(alloc, clox_class_t, OBJ_CLASS);

  clox_pop_durable(alloc); // name

  class_->name = name;
  class_->init = CLOX_NIL;
  clox_table_init(&class_->methods, alloc);

  FINISH_ALLOCATION(alloc, clox_class_t, class_);
}

clox_instance_t *clox_new_instance(clox_allocator_t *alloc, const clox_class_t *class_) {
  assert(alloc != NULL);
  assert(class_ != NULL);

  clox_push_durable(alloc, (clox_object_t *)class_);

  clox_instance_t *instance = START_ALLOCATION(alloc, clox_instance_t, OBJ_INSTANCE);

  clox_pop_durable(alloc); // class_

  instance->class_ = class_;
  clox_table_init(&instance->fields, alloc);

  FINISH_ALLOCATION(alloc, clox_instance_t, instance);
}

clox_bound_method_t *clox_new_bound_method(clox_allocator_t *alloc, clox_value_t receiver,
                                           clox_value_t method) {
  assert(alloc != NULL);
  assert(CLOX_IS_INSTANCE(receiver));
  assert(CLOX_IS_FUNCTION(method) || CLOX_IS_CLOSURE(method));

  clox_push_durable(alloc, CLOX_AS_OBJECT(receiver));
  clox_push_durable(alloc, CLOX_AS_OBJECT(method));

  clox_bound_method_t *bm = START_ALLOCATION(alloc, clox_bound_method_t, OBJ_BOUND_METHOD);

  clox_pop_durable(alloc); // method
  clox_pop_durable(alloc); // receiver

  bm->receiver = receiver;
  bm->method = method;

  FINISH_ALLOCATION(alloc, clox_bound_method_t, bm);
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
  case OBJ_CLASS:
  case OBJ_INSTANCE:
  case OBJ_BOUND_METHOD:
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
  case OBJ_CLOSURE:
    // compare closures' underlying functions
    return clox_object_equals(CLOX_OBJECT(CLOX_AS_CLOSURE(a)->function),
                              CLOX_OBJECT(CLOX_AS_CLOSURE(b)->function));
  case OBJ_BOUND_METHOD:
    // compare bound methods' underlying methods
    return clox_value_equals(CLOX_AS_BOUND_METHOD(a)->method, CLOX_AS_BOUND_METHOD(b)->method);
  case OBJ_FUNCTION:
  case OBJ_NATIVE:
  case OBJ_UPVALUE:
  case OBJ_CLASS:
  case OBJ_INSTANCE:
    // compare object pointers
    return CLOX_AS_OBJECT(a) == CLOX_AS_OBJECT(b);
  }
}

void clox_object_fprintf(FILE *stream, clox_value_t val) {
  assert(stream != NULL);
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
  case OBJ_CLASS:
    (void)fprintf(stream, "<cl %s>", CLOX_AS_CLASS(val)->name->chars);
    break;
  case OBJ_INSTANCE:
    (void)fprintf(stream, "<in %s>", CLOX_AS_INSTANCE(val)->class_->name->chars);
    break;
  case OBJ_CLOSURE:
    clox_object_fprintf(stream, CLOX_OBJECT(CLOX_AS_CLOSURE(val)->function));
    break;
  case OBJ_BOUND_METHOD:
    clox_object_fprintf(stream, CLOX_AS_BOUND_METHOD(val)->method);
    break;
  }
}

void clox_object_printf(clox_value_t val) {
  clox_object_fprintf(stdout, val);
}

void clox_object_repr_fprintf(FILE *stream, clox_value_t val) {
  assert(stream != NULL);
  assert(CLOX_IS_OBJECT(val));

  switch (CLOX_AS_OBJECT(val)->type) {
  case OBJ_STRING:
    (void)fprintf(stream, "\"%s\"", CLOX_AS_CSTRING(val));
    break;
  case OBJ_CLOSURE:
    (void)fprintf(stream, "<co %s>", CLOX_AS_CLOSURE(val)->function->name);
    break;
  case OBJ_BOUND_METHOD: {
    clox_bound_method_t *bm = CLOX_AS_BOUND_METHOD(val);
    if (CLOX_IS_CLOSURE(bm->method)) {
      (void)fprintf(stream, "<bm %s>", CLOX_AS_CLOSURE(bm->method)->function->name);
    } else { // bm->method is function by construction
      (void)fprintf(stream, "<bm %s>", CLOX_AS_FUNCTION(bm->method)->name);
    }
    break;
  }
  case OBJ_FUNCTION:
  case OBJ_NATIVE:
  case OBJ_UPVALUE:
  case OBJ_CLASS:
  case OBJ_INSTANCE:
    clox_object_fprintf(stream, val);
    break;
  }
}

void clox_object_repr_printf(clox_value_t val) {
  clox_object_repr_fprintf(stdout, val);
}

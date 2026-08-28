#ifndef CLOX_VM_H
#define CLOX_VM_H

#include <stdbool.h>
#include <stddef.h>

#include "error.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define CLOX_MAX_FRAMES 64
#define CLOX_STACK_SIZE ((size_t)CLOX_MAX_FRAMES * 1024)

typedef void clox_print_fn_t(const clox_value_t *vals, size_t n, void *ctx);

typedef struct {
  const clox_function_t *function;
  const clox_byte_t *ip;
  clox_value_t *slots;
} clox_call_frame_t;

typedef struct {
  clox_table_t globals;
  clox_value_t *stack_top;
  clox_value_t stack[CLOX_STACK_SIZE];
  size_t frame_count;
  clox_call_frame_t frames[CLOX_MAX_FRAMES];
  clox_allocator_t *allocator;
  clox_error_handler_t *error_handler;
  void *error_ctx;
  clox_print_fn_t *print_fn;
  void *print_ctx;
} clox_vm_t;

void clox_vm_init(clox_vm_t *vm, clox_allocator_t *alloc);
void clox_vm_free(clox_vm_t *vm);
void clox_vm_set_error_handler(clox_vm_t *vm, clox_error_handler_t *error_handler, void *error_ctx);
void clox_vm_reset_error_handler(clox_vm_t *vm);
void clox_vm_set_print_fn(clox_vm_t *vm, clox_print_fn_t *print_fn, void *print_ctx);
void clox_vm_set_default_print_fn(clox_vm_t *vm);
void clox_vm_define_native(clox_vm_t *vm, const char *name, size_t arity, clox_native_fn_t *fn);

bool clox_interpret(clox_vm_t *vm, const clox_function_t *script);

#endif

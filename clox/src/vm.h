#ifndef CLOX_VM_H
#define CLOX_VM_H

#include <stdbool.h>
#include <stddef.h>

#include "chunk.h"
#include "error.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define CLOX_STACK_SIZE 1024

typedef void clox_print_fn_t(const clox_value_t *vals, size_t n, void *ctx);

typedef struct {
  const clox_chunk_t *chunk;
  const clox_byte_t *ip;
  clox_value_t *stack_top;
  clox_value_t stack[CLOX_STACK_SIZE];
  clox_allocator_t *allocator;
  clox_table_t globals;
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

bool clox_interpret(clox_vm_t *vm, const clox_chunk_t *chunk);

#endif

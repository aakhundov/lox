#ifndef CLOX_VM_H
#define CLOX_VM_H

#include <assert.h>

#include "chunk.h"
#include "value.h"

#define CLOX_STACK_SIZE 1024

typedef struct {
  const clox_chunk_t *chunk;
  const clox_byte_t *ip;
  clox_value_t *stack_top;
  clox_value_t stack[CLOX_STACK_SIZE];
} clox_vm_t;

void clox_init_vm(clox_vm_t *vm);
void clox_free_vm(clox_vm_t *vm);

typedef enum {
  CLOX_INTERPRET_OK,
  CLOX_INTERPRET_COMPILE_ERROR,
  CLOX_INTERPRET_RUNTIME_ERROR,
} clox_interpret_result_t;

clox_interpret_result_t clox_interpret(clox_vm_t *vm, const clox_chunk_t *chunk);

#endif

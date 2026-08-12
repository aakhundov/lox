#include "vm.h"

#include <assert.h>
#include <stdio.h>

#include "chunk.h"
#include "debug.h"
#include "error.h"
#include "value.h"

#if CLOX_DEBUG_TRACE
static void print_stack(const clox_vm_t *vm) {
  assert(vm->stack_top >= vm->stack);
  // cast is safe: assert above
  size_t size = (size_t)(vm->stack_top - vm->stack);

  printf("[ ");
  for (size_t i = 0; i < size; i++) {
    clox_print_value(*(vm->stack + i));
    if (i < size - 1) {
      printf(" | ");
    }
  }
  printf(" ]\n");
}
#endif

static void reset_stack(clox_vm_t *vm) {
  vm->stack_top = vm->stack;
}

static inline void push_stack(clox_vm_t *vm, clox_value_t value) {
  if (vm->stack_top >= vm->stack + CLOX_STACK_SIZE) {
    CLOX_FATAL_ERROR("stack overflow", 1);
  }

  *vm->stack_top = value;
  vm->stack_top++;
}

static inline clox_value_t pop_stack(clox_vm_t *vm) {
  assert(vm->stack_top > vm->stack);

  vm->stack_top--;
  return *vm->stack_top;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static clox_interpret_result_t run(clox_vm_t *vm) {
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT(opcode) clox_read_constant(vm->chunk, (opcode), &vm->ip)
#define BINARY_OP(op)                                                                              \
  do {                                                                                             \
    clox_value_t right = pop_stack(vm);                                                            \
    clox_value_t left = pop_stack(vm);                                                             \
    push_stack(vm, left op right);                                                                 \
  } while (0)

  while (1) {
    assert(vm->ip >= vm->chunk->code);
    assert(vm->ip < vm->chunk->code + vm->chunk->length);

#if CLOX_DEBUG_TRACE
    print_stack(vm);
    clox_disassemble_instruction(vm->chunk, (size_t)(vm->ip - vm->chunk->code));
#endif

    clox_byte_t byte = READ_BYTE();
    assert(byte < OP_CODE_COUNT);

    clox_op_code_t opcode;
    switch (opcode = (clox_op_code_t)byte) {
    case OP_CONSTANT:
    case OP_CONSTANT_LONG:
      push_stack(vm, READ_CONSTANT(opcode));
      break;
    case OP_ADD:
      BINARY_OP(+);
      break;
    case OP_SUBTRACT:
      BINARY_OP(-);
      break;
    case OP_MULTIPLY:
      BINARY_OP(*);
      break;
    case OP_DIVIDE:
      BINARY_OP(/);
      break;
    case OP_NEGATE:
      push_stack(vm, -pop_stack(vm));
      break;
    case OP_RETURN:
      while (vm->stack_top > vm->stack) {
        clox_print_value(pop_stack(vm));
        printf("\n");
      }
      return CLOX_INTERPRET_OK;
    case OP_CODE_COUNT:
      assert(0 && "unreachable");
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

void clox_init_vm(clox_vm_t *vm) {
  reset_stack(vm);
}

void clox_free_vm(clox_vm_t *vm) {
}

clox_interpret_result_t clox_interpret(clox_vm_t *vm, const clox_chunk_t *chunk) {
  vm->chunk = chunk;
  vm->ip = chunk->code;
  reset_stack(vm);

  return run(vm);
}

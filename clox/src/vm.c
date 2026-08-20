#include "vm.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "object.h"
#include "value.h"

#define ERROR_MESSAGE_SIZE 512

#if CLOX_DEBUG_EXECUTION
static void print_stack(const clox_vm_t *vm) {
  assert(vm->stack_top >= vm->stack);
  // cast is safe: assert above
  size_t size = (size_t)(vm->stack_top - vm->stack);

  printf("---- STCK [ ");
  for (size_t i = 0; i < size; i++) {
    clox_value_print(*(vm->stack + i));
    if (i < size - 1) {
      printf(" | ");
    }
  }
  printf(" ]\n");
}
#endif

static void default_print_fn(clox_value_t val, void *ctx) {
  (void)ctx; // unused
  clox_value_print(val);
  printf("\n");
}

__attribute__((format(printf, 2, 3))) static inline void error(const clox_vm_t *vm, const char *fmt,
                                                               ...) {
  char message[ERROR_MESSAGE_SIZE];

  va_list ap;
  va_start(ap, fmt);
  (void)vsnprintf(message, sizeof(message), fmt, ap);
  va_end(ap);

  // ip is incremented before processing instruction
  assert(vm->ip > vm->chunk->code); // non-zero difference
  size_t offset = (size_t)(vm->ip - vm->chunk->code - 1);
  clox_pos_t pos = vm->chunk->positions[offset];

  // report the error
  vm->error_handler(
      (clox_error_info_t){
          .message = message,
          .pos = pos,
      },
      vm->error_ctx);
}

static inline void reset_stack(clox_vm_t *vm) {
  vm->stack_top = vm->stack;
}

static inline clox_value_t peek_stack(const clox_vm_t *vm, size_t distance) {
  assert(distance < (size_t)(vm->stack_top - vm->stack));
  return vm->stack_top[-1 - (int)distance];
}

static inline bool push_stack(clox_vm_t *vm, clox_value_t val) {
  if (vm->stack_top >= vm->stack + CLOX_STACK_SIZE) {
    return false; // stack overflow
  }

  *vm->stack_top = val;
  vm->stack_top++;
  return true;
}

static inline clox_value_t pop_stack(clox_vm_t *vm) {
  assert(vm->stack_top > vm->stack); // not empty

  vm->stack_top--;
  return *vm->stack_top;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool run(clox_vm_t *vm) {
#define ERROR(...)                                                                                 \
  do {                                                                                             \
    if (vm->error_handler != NULL) {                                                               \
      error(vm, __VA_ARGS__);                                                                      \
    }                                                                                              \
    return false;                                                                                  \
  } while (0)
#define PRINT(val) vm->print_fn((val), vm->print_ctx)
#define PUSH(val)                                                                                  \
  do {                                                                                             \
    if (!push_stack(vm, (val))) {                                                                  \
      ERROR("stack overflow");                                                                     \
    }                                                                                              \
  } while (0)
#define POP() pop_stack(vm)
#define PEEK(distance) peek_stack(vm, (distance))
#define READ_BYTE() (*vm->ip++)
#define READ_CONSTANT(opcode) clox_read_constant(vm->chunk, (opcode), &vm->ip)
#define BINARY_OP(op, RESULT_TYPE)                                                                 \
  do {                                                                                             \
    if (!CLOX_IS_NUMBER(PEEK(0)) || !CLOX_IS_NUMBER(PEEK(1))) {                                    \
      ERROR("operands must be numbers");                                                           \
    }                                                                                              \
    double right = CLOX_AS_NUMBER(POP());                                                          \
    double left = CLOX_AS_NUMBER(POP());                                                           \
    PUSH(RESULT_TYPE(left op right));                                                              \
  } while (0)

  while (1) {
    assert(vm->ip >= vm->chunk->code);
    assert(vm->ip < vm->chunk->code + vm->chunk->length);

#if CLOX_DEBUG_EXECUTION
    print_stack(vm);
    printf("---- EXEC ");
    clox_disassemble_instruction(vm->chunk, (size_t)(vm->ip - vm->chunk->code));
#endif

    clox_byte_t byte = READ_BYTE();
    assert(byte < OP_CODE_COUNT);

    clox_op_code_t opcode;
    switch (opcode = (clox_op_code_t)byte) {
    case OP_CONSTANT:
    case OP_CONSTANT_LONG:
      PUSH(READ_CONSTANT(opcode));
      break;
    case OP_NIL:
      PUSH(CLOX_NIL);
      break;
    case OP_TRUE:
      PUSH(CLOX_BOOL(true));
      break;
    case OP_FALSE:
      PUSH(CLOX_BOOL(false));
      break;
    case OP_EQUAL: {
      clox_value_t b = POP();
      clox_value_t a = POP();
      PUSH(CLOX_BOOL(clox_value_equals(a, b)));
      break;
    }
    case OP_NOT_EQUAL: {
      clox_value_t b = POP();
      clox_value_t a = POP();
      PUSH(CLOX_BOOL(!clox_value_equals(a, b)));
      break;
    }
    case OP_GREATER:
      BINARY_OP(>, CLOX_BOOL);
      break;
    case OP_GREATER_EQUAL:
      BINARY_OP(>=, CLOX_BOOL);
      break;
    case OP_LESS:
      BINARY_OP(<, CLOX_BOOL);
      break;
    case OP_LESS_EQUAL:
      BINARY_OP(<=, CLOX_BOOL);
      break;
    case OP_ADD: {
      if (CLOX_IS_NUMBER(PEEK(0)) && CLOX_IS_NUMBER(PEEK(1))) {
        double right = CLOX_AS_NUMBER(POP());
        double left = CLOX_AS_NUMBER(POP());
        PUSH(CLOX_NUMBER(left + right));
      } else if (CLOX_IS_STRING(PEEK(0)) && CLOX_IS_STRING(PEEK(1))) {
        clox_value_t right = POP();
        clox_value_t left = POP();
        PUSH(clox_string_concat(vm->allocator, left, right));
      } else {
        ERROR("operands must be two numbers or two strings");
      }
      break;
    }
    case OP_SUBTRACT:
      BINARY_OP(-, CLOX_NUMBER);
      break;
    case OP_MULTIPLY:
      BINARY_OP(*, CLOX_NUMBER);
      break;
    case OP_DIVIDE:
      BINARY_OP(/, CLOX_NUMBER);
      break;
    case OP_NOT:
      PUSH(CLOX_BOOL(!clox_value_is_truthy(POP())));
      break;
    case OP_NEGATE:
      if (!CLOX_IS_NUMBER(PEEK(0))) {
        ERROR("operand must be a number");
      }
      PUSH(CLOX_NUMBER(-CLOX_AS_NUMBER(POP())));
      break;
    case OP_RETURN:
      while (vm->stack_top > vm->stack) {
        PRINT(POP()); // temporary
      }
      return true;
    case OP_CODE_COUNT:
      assert(0 && "unreachable");
    }
  }

#undef ERROR
#undef PRINT
#undef PUSH
#undef POP
#undef PEEK
#undef READ_BYTE
#undef READ_CONSTANT
#undef BINARY_OP
}

void clox_vm_init(clox_vm_t *vm, clox_allocator_t *alloc) {
  vm->ip = NULL;
  vm->chunk = NULL;
  vm->allocator = alloc;
  clox_vm_reset_error_handler(vm);
  clox_vm_reset_print_fn(vm);
  reset_stack(vm);
}

void clox_vm_free(clox_vm_t *vm) {
  vm->ip = NULL;
  vm->chunk = NULL;
  vm->allocator = NULL;
  clox_vm_reset_error_handler(vm);
  clox_vm_reset_print_fn(vm);
  reset_stack(vm);
}

void clox_vm_set_error_handler(clox_vm_t *vm, clox_error_handler_t *error_handler,
                               void *error_ctx) {
  vm->error_handler = error_handler;
  vm->error_ctx = error_ctx;
}

void clox_vm_set_print_fn(clox_vm_t *vm, clox_print_fn_t *print_fn, void *print_ctx) {
  vm->print_fn = print_fn;
  vm->print_ctx = print_ctx;
}

void clox_vm_reset_error_handler(clox_vm_t *vm) {
  vm->error_handler = NULL;
  vm->error_ctx = NULL;
}

void clox_vm_reset_print_fn(clox_vm_t *vm) {
  vm->print_fn = default_print_fn;
  vm->print_ctx = NULL;
}

bool clox_interpret(clox_vm_t *vm, const clox_chunk_t *chunk) {
  // init
  vm->chunk = chunk;
  vm->ip = chunk->code;
  reset_stack(vm);

  bool result = run(vm);

  // cleanup
  vm->chunk = NULL;
  vm->ip = NULL;

  return result;
}

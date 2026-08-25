#include "vm.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "library.h"
#include "object.h"
#include "table.h"
#include "value.h"

#if CLOX_DEBUG_EXECUTION
static void print_stack(const clox_vm_t *vm) {
  if (vm->stack_top == vm->stack) {
    return;
  }

  assert(vm->stack_top > vm->stack);
  // cast is safe: assert above
  size_t size = (size_t)(vm->stack_top - vm->stack);

  printf("---- STCK [ ");
  const char *sep = "";
  for (size_t i = 0; i < size; i++) {
    printf("%s", sep);
    clox_value_repr_printf(*(vm->stack + i));
    sep = " | ";
  }
  printf(" ]\n");
}

static void print_globals(const clox_vm_t *vm) {
  if (vm->globals.entries == NULL) {
    return;
  }

  bool empty = true;
  const char *sep = "";
  const clox_table_entry_t *running = NULL;
  while ((running = clox_table_next(&vm->globals, running))) {
    if (CLOX_IS_FUNCTION(running->value) || CLOX_IS_NATIVE(running->value)) {
      // skip non-primitives
      continue;
    }
    if (empty) {
      printf("---- GLOB { ");
      empty = false;
    }
    printf("%s%s = ", sep, running->key->chars);
    clox_value_repr_printf(running->value);
    sep = " | ";
  }
  if (!empty) {
    printf(" }\n");
  }
}
#endif

static void default_print_fn(const clox_value_t *vals, size_t n, void *ctx) {
  (void)ctx; // unused
  for (size_t i = 0; i < n; i++) {
    clox_value_printf(vals[i]);
    if (i < n - 1) {
      printf(" ");
    }
  }
  printf("\n");
}

__attribute__((format(printf, 2, 3))) static inline void error(const clox_vm_t *vm, const char *fmt,
                                                               ...) {
  if (vm->error_handler == NULL) {
    return;
  }

  va_list ap;
  va_start(ap, fmt);
  char message[MAX_ERROR_LENGTH + 1];
  clox_format_error(&message, fmt, ap);
  va_end(ap);

  const clox_call_frame_t *frame = &vm->frames[vm->frame_count - 1];

  // ip is incremented before processing instruction
  assert(frame->ip > frame->function->chunk.code); // non-zero difference
  size_t offset = (size_t)(frame->ip - frame->function->chunk.code - 1);
  clox_pos_t pos = frame->function->chunk.positions[offset];

  // report the error
  vm->error_handler(
      (clox_error_info_t){
          .message = message,
          .pos = pos,
      },
      vm->error_ctx);
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

static inline void pop_n_stack(clox_vm_t *vm, size_t n) {
  assert(n <= CLOX_STACK_SIZE);
  assert(vm->stack_top >= vm->stack + n); // has n items

  vm->stack_top -= n;
}

static inline bool call_function(clox_vm_t *vm, const clox_function_t *function, size_t arg_count) {
  if (vm->frame_count >= CLOX_MAX_FRAMES) {
    error(vm, "call stack overflow");
    return false;
  }
  if (arg_count != function->arity) {
    error(vm, "expected %zu args but got %zu", function->arity, arg_count);
    return false;
  }

  // set up new call frame
  clox_call_frame_t *frame = &vm->frames[vm->frame_count++];
  frame->function = function;
  frame->ip = function->chunk.code;
  // function and args are in the new call frame
  frame->slots = vm->stack_top - arg_count - 1;

  return true;
}

static inline bool call_native(clox_vm_t *vm, const clox_native_t *native, size_t arg_count) {
  clox_value_t result = native->function(arg_count, vm->stack_top - arg_count);
  pop_n_stack(vm, arg_count + 1); // discard callee and args
  push_stack(vm, result);         // can't overflow: callee popped

  return true;
}

static inline bool call_value(clox_vm_t *vm, clox_value_t val, size_t arg_count) {
  // make sure callee and args are on the stack
  assert(vm->stack_top - vm->stack >= (ptrdiff_t)arg_count + 1);

  if (CLOX_IS_OBJECT(val)) {
    switch (CLOX_AS_OBJECT(val)->type) {
    case OBJ_FUNCTION:
      return call_function(vm, CLOX_AS_FUNCTION(val), arg_count);
    case OBJ_NATIVE:
      return call_native(vm, CLOX_AS_NATIVE(val), arg_count);
    case OBJ_STRING:
      break; // non-callable object type
    }
  }

  error(vm, "can only call functions");
  return false;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool run(clox_vm_t *vm) {
  clox_call_frame_t *frame = &vm->frames[vm->frame_count - 1];

#define ERROR(...)                                                                                 \
  do {                                                                                             \
    error(vm, __VA_ARGS__);                                                                        \
    return false;                                                                                  \
  } while (0)
#define PUSH(val)                                                                                  \
  do {                                                                                             \
    if (!push_stack(vm, (val))) {                                                                  \
      ERROR("value stack overflow");                                                               \
    }                                                                                              \
  } while (0)
#define POP() pop_stack(vm)
#define POP_N(n) pop_n_stack(vm, n)
#define PEEK(distance) peek_stack(vm, (distance))
#define READ_BYTE() (*frame->ip++)
#define READ_TWO_BYTES() (frame->ip += 2, ((size_t)frame->ip[-2] << CHAR_BIT) | frame->ip[-1])
#define READ_CONSTANT(opcode) clox_read_constant(&frame->function->chunk, (opcode), &frame->ip)
#define READ_STRING(opcode) CLOX_AS_STRING(READ_CONSTANT(opcode))
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
    assert(frame->ip >= frame->function->chunk.code);
    assert(frame->ip < frame->function->chunk.code + frame->function->chunk.length);

#if CLOX_DEBUG_EXECUTION
    print_stack(vm);
    print_globals(vm);
    printf("---- EXEC ");
    clox_disassemble_instruction(&frame->function->chunk,
                                 (size_t)(frame->ip - frame->function->chunk.code));
#endif

    clox_byte_t byte = READ_BYTE();
    assert(byte < OP_CODE_COUNT);

    clox_op_code_t opcode;
    switch (opcode = (clox_op_code_t)byte) {
    case OP_CONSTANT:
    case OP_CONSTANT_LONG:
      PUSH(READ_CONSTANT(opcode));
      break;
    case OP_DEF_GLOBAL:
    case OP_DEF_GLOBAL_LONG: {
      const clox_string_t *name = READ_STRING(opcode);
      clox_table_set(&vm->globals, name, PEEK(0));
      POP(); // pop after peek to avoid GC in-flight
      break;
    }
    case OP_GET_GLOBAL:
    case OP_GET_GLOBAL_LONG: {
      const clox_string_t *name = READ_STRING(opcode);
      clox_value_t value;
      if (!clox_table_get(&vm->globals, name, &value)) {
        ERROR("undefined variable '%s'", name->chars);
      }
      PUSH(value);
      break;
    }
    case OP_GET_LOCAL:
      PUSH(frame->slots[READ_BYTE()]);
      break;
    case OP_SET_GLOBAL:
    case OP_SET_GLOBAL_LONG: {
      const clox_string_t *name = READ_STRING(opcode);
      // no POP(): assignment is expression
      if (clox_table_set(&vm->globals, name, PEEK(0))) {
        // the name didn't exist among the globals
        clox_table_delete(&vm->globals, name); // revert
        ERROR("undefined variable '%s'", name->chars);
      }
      break;
    }
    case OP_SET_LOCAL:
      // no POP(): assignment is expression
      frame->slots[READ_BYTE()] = PEEK(0);
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
    case OP_POP:
      POP();
      break;
    case OP_POP_N:
      POP_N(READ_BYTE());
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
    case OP_PRINT:
      vm->print_fn(vm->stack_top - 1, 1, vm->print_ctx);
      POP();
      break;
    case OP_PRINT_N: {
      size_t n = READ_BYTE();
      vm->print_fn(vm->stack_top - n, n, vm->print_ctx);
      POP_N(n);
      break;
    }
    case OP_JUMP_TRUE: {
      size_t offset = READ_TWO_BYTES();
      if (clox_value_is_truthy(PEEK(0))) {
        frame->ip += offset;
      }
      break;
    }
    case OP_JUMP_FALSE: {
      size_t offset = READ_TWO_BYTES();
      if (!clox_value_is_truthy(PEEK(0))) {
        frame->ip += offset;
      }
      break;
    }
    case OP_JUMP_FALSE_POP: {
      size_t offset = READ_TWO_BYTES();
      if (!clox_value_is_truthy(POP())) {
        frame->ip += offset;
      }
      break;
    }
    case OP_JUMP: {
      size_t offset = READ_TWO_BYTES();
      frame->ip += offset;
      break;
    }
    case OP_LOOP: {
      size_t offset = READ_TWO_BYTES();
      frame->ip -= offset;
      break;
    }
    case OP_CALL: {
      size_t arg_count = READ_BYTE();
      if (!call_value(vm, PEEK(arg_count), arg_count)) {
        return false; // call failed
      }
      // pick up the new call frame locally
      frame = &vm->frames[vm->frame_count - 1];
      break;
    }
    case OP_RETURN: {
      clox_value_t result = POP();
      vm->frame_count--;
      if (vm->frame_count == 0) {
        return true; // return from script
      }
      vm->stack_top = frame->slots;             // rewind to caller's stack
      PUSH(result);                             // push result on caller's stack
      frame = &vm->frames[vm->frame_count - 1]; // restore caller's frame
      break;
    }
    case OP_CODE_COUNT:
      assert(0 && "unreachable");
    }
  }

#undef ERROR
#undef PUSH
#undef POP
#undef POP_N
#undef PEEK
#undef READ_BYTE
#undef READ_TWO_BYTES
#undef READ_CONSTANT
#undef READ_STRING
#undef BINARY_OP
}

void clox_vm_init(clox_vm_t *vm, clox_allocator_t *alloc) {
  vm->allocator = alloc;
  vm->stack_top = vm->stack;
  vm->frame_count = 0;

  clox_table_init(&vm->globals);
  clox_vm_reset_error_handler(vm);
  clox_vm_set_default_print_fn(vm);

  // define built-in native functions
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    clox_vm_define_native(vm, clox_library_fn_names[i], clox_library_fns[i]);
  }
}

void clox_vm_free(clox_vm_t *vm) {
  vm->allocator = NULL;
  vm->stack_top = vm->stack;
  vm->frame_count = 0;

  clox_table_free(&vm->globals);
  clox_vm_reset_error_handler(vm);
  clox_vm_set_default_print_fn(vm);
}

void clox_vm_set_error_handler(clox_vm_t *vm, clox_error_handler_t *error_handler,
                               void *error_ctx) {
  vm->error_handler = error_handler;
  vm->error_ctx = error_ctx;
}

void clox_vm_reset_error_handler(clox_vm_t *vm) {
  vm->error_handler = NULL;
  vm->error_ctx = NULL;
}

void clox_vm_set_print_fn(clox_vm_t *vm, clox_print_fn_t *print_fn, void *print_ctx) {
  vm->print_fn = print_fn;
  vm->print_ctx = print_ctx;
}

void clox_vm_set_default_print_fn(clox_vm_t *vm) {
  vm->print_fn = default_print_fn;
  vm->print_ctx = NULL;
}

void clox_vm_define_native(clox_vm_t *vm, const char *name, clox_native_fn_t *native) {
  // temporary stack placement to avoid in-flight GC
  push_stack(vm, CLOX_NATIVE(vm->allocator, name, native));
  push_stack(vm, CLOX_STRING_COPY(vm->allocator, name, strlen(name)));
  clox_table_set(&vm->globals, CLOX_AS_STRING(peek_stack(vm, 0)), peek_stack(vm, 1));
  pop_stack(vm); // name
  pop_stack(vm); // native
}

bool clox_interpret(clox_vm_t *vm, const clox_function_t *script) {
  // init
  vm->stack_top = vm->stack;
  vm->frame_count = 0;

  // set up the script's call frame
  push_stack(vm, CLOX_OBJECT(script));
  call_function(vm, script, 0);

  if (!run(vm)) {
    return false;
  }

  // cleanup
  pop_stack(vm); // script

  assert(vm->frame_count == 0);
  assert(vm->stack_top == vm->stack);

  return true;
}

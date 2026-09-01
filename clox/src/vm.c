#include "vm.h"

#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "debug.h"
#include "error.h"
#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#if CLOX_ENABLE_LIBRARY
#include "library.h"
#endif

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
    if (CLOX_IS_FUNCTION(running->value) || CLOX_IS_NATIVE(running->value) ||
        CLOX_IS_CLOSURE(running->value)) {
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

  clox_error_info_t info = {
      .message = message,
      .num_locations = 0,
  };

  // unwind the stack inside out
  for (int i = (int)vm->frame_count - 1; i >= 0; i--) {
    const clox_call_frame_t *frame = &vm->frames[i];
    // ip is incremented before processing instruction
    assert(frame->ip > frame->closure->function->chunk.code); // non-zero difference
    size_t offset = (size_t)(frame->ip - frame->closure->function->chunk.code - 1);
    info.positions[info.num_locations] = frame->closure->function->chunk.positions[offset];
    info.function_names[info.num_locations] = frame->closure->function->name;
    info.file_names[info.num_locations] = frame->closure->function->file_name;
    info.sources[info.num_locations] = frame->closure->function->source;
    if (++info.num_locations == CLOX_MAX_ERROR_STACK_SIZE) {
      break;
    }
  }

  // report the error
  vm->error_handler(&info, vm->error_ctx);
}

static inline clox_value_t peek_stack(const clox_vm_t *vm, size_t distance) {
  assert(distance < (size_t)(vm->stack_top - vm->stack));
  return vm->stack_top[-1 - (int)distance];
}

static inline bool push_stack(clox_vm_t *vm, clox_value_t val) {
  if (vm->stack_top >= vm->stack + CLOX_STACK_SIZE) {
    return false; // stack overflow
  }

  *vm->stack_top++ = val;
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

static inline bool call_closure(clox_vm_t *vm, const clox_closure_t *closure, size_t arg_count) {
  if (vm->frame_count >= CLOX_MAX_FRAMES) {
    error(vm, "call stack overflow");
    return false;
  }
  if (arg_count != closure->function->arity) {
    error(vm, "expected %zu args but got %zu", closure->function->arity, arg_count);
    return false;
  }

  // set up new call frame
  clox_call_frame_t *frame = &vm->frames[vm->frame_count++];
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;
  // function and args are in the new call frame
  frame->slots = vm->stack_top - arg_count - 1;

  return true;
}

static inline bool call_native(clox_vm_t *vm, const clox_native_t *native, size_t arg_count) {
  // SIZE_MAX arity means variadic native
  if (native->arity != SIZE_MAX && arg_count != native->arity) {
    error(vm, "expected %zu args but got %zu", native->arity, arg_count);
    return false;
  }

  clox_native_result_t result;
  bool success = native->function(arg_count, vm->stack_top - arg_count, &result);
  pop_n_stack(vm, arg_count + 1); // discard callee and args

  if (!success) {
    error(vm, "%s", result.error_msg);
    return false;
  }

  push_stack(vm, result.value); // can't overflow: callee popped
  return true;
}

static inline bool call_value(clox_vm_t *vm, clox_value_t val, size_t arg_count) {
  // make sure callee and args are on the stack
  assert(vm->stack_top - vm->stack >= (ptrdiff_t)arg_count + 1);

  if (CLOX_IS_OBJECT(val)) {
    switch (CLOX_AS_OBJECT(val)->type) {
    case OBJ_CLOSURE:
      return call_closure(vm, CLOX_AS_CLOSURE(val), arg_count);
    case OBJ_NATIVE:
      return call_native(vm, CLOX_AS_NATIVE(val), arg_count);
    case OBJ_STRING:
    case OBJ_FUNCTION:
    case OBJ_UPVALUE:
      break; // non-callable object type
    }
  }

  error(vm, "can only call functions");
  return false;
}

static inline clox_upvalue_t *capture_upvalue(clox_vm_t *vm, clox_value_t *local) {
  clox_upvalue_t *previous = NULL;
  clox_upvalue_t *running = vm->open_upvalues;
  // traverse the link list from stack top to bottom
  while (running != NULL && running->location > local) {
    previous = running;
    running = running->next;
  }

  if (running != NULL && running->location == local) {
    // found: return existing upvalue closing over local
    return running;
  }

  // not found: create new upvalue closing over local
  clox_upvalue_t *new_upvalue = clox_new_upvalue(vm->allocator, local);

  // insert new upvalue into the list
  new_upvalue->next = running;
  if (previous == NULL) {
    vm->open_upvalues = new_upvalue;
  } else {
    previous->next = new_upvalue;
  }

  return new_upvalue;
}

static inline void close_upvalues(clox_vm_t *vm, clox_value_t *last) {
  while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
    clox_upvalue_t *upvalue = vm->open_upvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    vm->open_upvalues = upvalue->next;
  }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool run(clox_vm_t *vm) {
  clox_call_frame_t *frame = &vm->frames[vm->frame_count - 1];
  const clox_byte_t *ip = frame->ip;

#define ERROR(...)                                                                                 \
  do {                                                                                             \
    frame->ip = ip;                                                                                \
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
#define READ_BYTE() (*ip++)
#define READ_TWO_BYTES() (ip += 2, ((size_t)ip[-2] << CHAR_BIT) | ip[-1])
#define READ_CONSTANT(opcode) clox_read_constant(&frame->closure->function->chunk, (opcode), &ip)
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
    assert(ip >= frame->closure->function->chunk.code);
    assert(ip < frame->closure->function->chunk.code + frame->closure->function->chunk.length);

#if CLOX_DEBUG_EXECUTION
    print_stack(vm);
    print_globals(vm);
    printf("---- EXEC ");
    clox_disassemble_instruction(&frame->closure->function->chunk,
                                 (size_t)(ip - frame->closure->function->chunk.code));
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
    case OP_GET_UPVALUE:
      PUSH(*frame->closure->upvalues[READ_BYTE()]->location);
      break;
    case OP_SET_GLOBAL:
    case OP_SET_GLOBAL_LONG:
    case OP_SET_GLOBAL_POP:
    case OP_SET_GLOBAL_POP_LONG: {
      clox_value_t existing_value;
      const clox_string_t *name = READ_STRING(opcode);
      bool name_exists = clox_table_get(&vm->globals, name, &existing_value);
      if (name_exists) {
        if (!CLOX_IS_NATIVE(existing_value)) {
          clox_table_set(&vm->globals, name, PEEK(0));
          if (opcode == OP_SET_GLOBAL_POP || opcode == OP_SET_GLOBAL_POP_LONG) {
            // GC: pop after clox_table_set ends
            POP();
          }
        } else {
          ERROR("can't assign to native '%s'", name->chars);
        }
      } else {
        ERROR("undefined variable '%s'", name->chars);
      }
      break;
    }
    case OP_SET_LOCAL:
      // no POP: assignment is expression
      frame->slots[READ_BYTE()] = PEEK(0);
      break;
    case OP_SET_LOCAL_POP:
      frame->slots[READ_BYTE()] = POP();
      break;
    case OP_SET_UPVALUE:
      // no POP: assignment is expression
      *frame->closure->upvalues[READ_BYTE()]->location = PEEK(0);
      break;
    case OP_SET_UPVALUE_POP:
      *frame->closure->upvalues[READ_BYTE()]->location = POP();
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
    case OP_CLOSE_UPVALUE:
      // close one upvalue at the top of stack
      close_upvalues(vm, vm->stack_top - 1);
      POP(); // pop after closing
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
        ip += offset;
      }
      break;
    }
    case OP_JUMP_FALSE: {
      size_t offset = READ_TWO_BYTES();
      if (!clox_value_is_truthy(PEEK(0))) {
        ip += offset;
      }
      break;
    }
    case OP_JUMP_FALSE_POP: {
      size_t offset = READ_TWO_BYTES();
      if (!clox_value_is_truthy(POP())) {
        ip += offset;
      }
      break;
    }
    case OP_JUMP: {
      size_t offset = READ_TWO_BYTES();
      ip += offset;
      break;
    }
    case OP_LOOP: {
      size_t offset = READ_TWO_BYTES();
      ip -= offset;
      break;
    }
    case OP_CALL: {
      size_t arg_count = READ_BYTE();
      frame->ip = ip; // for transitive error() calls
      if (!call_value(vm, PEEK(arg_count), arg_count)) {
        return false; // call failed
      }
      // pick up the new call frame locally
      frame = &vm->frames[vm->frame_count - 1];
      ip = frame->ip;
      break;
    }
    case OP_CLOSURE:
    case OP_CLOSURE_LONG: {
      const clox_function_t *function = CLOX_AS_FUNCTION(READ_CONSTANT(opcode));
      clox_closure_t *closure = clox_new_closure(vm->allocator, function);
      PUSH(CLOX_OBJECT(closure));
      for (size_t i = 0; i < function->upvalue_count; i++) {
        bool is_local = (READ_BYTE() == 1);
        size_t index = READ_BYTE();
        if (is_local) {
          closure->upvalues[i] = capture_upvalue(vm, frame->slots + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OP_RETURN: {
      clox_value_t result = POP();
      close_upvalues(vm, frame->slots);
      assert(vm->frame_count >= 1);               // above script level
      vm->stack_top = frame->slots;               // rewind caller's stack
      frame = &vm->frames[--vm->frame_count - 1]; // restore caller's frame
      ip = frame->ip;                             // copy ip from restored frame
      *vm->stack_top++ = result;                  // push result on caller's stack
      break;
    }
    case OP_RETURN_NIL: {
      close_upvalues(vm, frame->slots);
      vm->frame_count--;
      // the script always returns via OP_RETURN_NIL
      if (vm->frame_count == 0) {
        return true;
      }
      vm->stack_top = frame->slots;             // rewind caller's stack
      frame = &vm->frames[vm->frame_count - 1]; // restore caller's frame
      ip = frame->ip;                           // copy ip from restored frame
      *vm->stack_top++ = CLOX_NIL;              // push NIL on caller's stack
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

static inline void reset_vm(clox_vm_t *vm) {
  vm->stack_top = vm->stack;
  vm->open_upvalues = NULL;
  vm->frame_count = 0;
}

static inline void mark_callback(clox_allocator_t *alloc, void *ctx) {
  clox_vm_t *vm = ctx;

  // values on the stack stack
  for (clox_value_t *slot = vm->stack; slot < vm->stack_top; slot++) {
    clox_mark_value(alloc, *slot);
  }

  // globals
  clox_table_mark_entries(&vm->globals);

  // active closures
  for (clox_call_frame_t *frame = vm->frames; frame < vm->frames + vm->frame_count; frame++) {
    clox_mark_object(alloc, (clox_object_t *)frame->closure);
  }

  // open upvalues
  for (clox_upvalue_t *upvalue = vm->open_upvalues; upvalue != NULL; upvalue = upvalue->next) {
    clox_mark_object(alloc, (clox_object_t *)upvalue);
  }
}

void clox_vm_init(clox_vm_t *vm, clox_allocator_t *alloc) {
  assert(vm != NULL);
  assert(alloc != NULL);

  vm->allocator = alloc;
  reset_vm(vm);

  clox_table_init(&vm->globals, alloc);
  clox_vm_reset_error_handler(vm);
  clox_vm_set_default_print_fn(vm);

  vm->mark_callback_handle = clox_register_mark_callback(vm->allocator, mark_callback, vm);

#if CLOX_ENABLE_LIBRARY
  // define built-in native functions
  for (size_t i = 0; i < CLOX_LIBRARY_SIZE; i++) {
    clox_library_fn_t lib_fn = clox_library_fns[i];
    clox_vm_define_native(vm, lib_fn.name, lib_fn.arity, lib_fn.fn);
  }
#endif
}

void clox_vm_free(clox_vm_t *vm) {
  assert(vm != NULL);

  bool unregistered = clox_unregister_mark_callback(vm->allocator, vm->mark_callback_handle);
  assert(unregistered);
  (void)unregistered;

  vm->allocator = NULL;
  reset_vm(vm);

  clox_table_free(&vm->globals);
  clox_vm_reset_error_handler(vm);
  clox_vm_set_default_print_fn(vm);
}

void clox_vm_set_error_handler(clox_vm_t *vm, clox_error_handler_t *error_handler,
                               void *error_ctx) {
  assert(vm != NULL);
  assert(error_handler != NULL);

  vm->error_handler = error_handler;
  vm->error_ctx = error_ctx;
}

void clox_vm_reset_error_handler(clox_vm_t *vm) {
  assert(vm != NULL);

  vm->error_handler = NULL;
  vm->error_ctx = NULL;
}

void clox_vm_set_print_fn(clox_vm_t *vm, clox_print_fn_t *print_fn, void *print_ctx) {
  assert(vm != NULL);
  assert(print_fn != NULL);

  vm->print_fn = print_fn;
  vm->print_ctx = print_ctx;
}

void clox_vm_set_default_print_fn(clox_vm_t *vm) {
  assert(vm != NULL);

  vm->print_fn = default_print_fn;
  vm->print_ctx = NULL;
}

void clox_vm_define_native(clox_vm_t *vm, const char *name, size_t arity, clox_native_fn_t *fn) {
  assert(vm != NULL);
  assert(name != NULL);
  assert(fn != NULL);

  clox_native_t *native = clox_new_native(vm->allocator, name, arity, fn);
  clox_push_durable(vm->allocator, (clox_object_t *)native);
  const clox_string_t *str_name = clox_string_copy(vm->allocator, name, strlen(name));
  clox_push_durable(vm->allocator, (clox_object_t *)str_name);
  clox_table_set(&vm->globals, str_name, CLOX_OBJECT(native));
  clox_pop_durable(vm->allocator); // str_name
  clox_pop_durable(vm->allocator); // native
}

bool clox_interpret(clox_vm_t *vm, const clox_function_t *script) {
  assert(vm != NULL);
  assert(script != NULL);

  // init
  reset_vm(vm);

  // set up the script's call frame
  clox_closure_t *closure = clox_new_closure(vm->allocator, script);
  push_stack(vm, CLOX_OBJECT(closure));
  call_closure(vm, closure, 0);

  if (!run(vm)) {
    return false;
  }

  // cleanup
  pop_stack(vm); // script

  assert(vm->frame_count == 0);
  assert(vm->stack_top == vm->stack);
  assert(vm->open_upvalues == NULL);

  return true;
}

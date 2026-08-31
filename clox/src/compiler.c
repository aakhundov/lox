#include "compiler.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "debug.h"
#include "error.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

#define MAX_PARSER_DEPTH 20000
#define MAX_DECLARATION_DEPTH 200

#define FRAME(compiler) ((compiler)->frame)
#define LOOP(compiler) ((compiler)->frame->loop)
#define FUNCTION(compiler) ((compiler)->frame->function)
#define CHUNK(compiler) ((compiler)->frame->function->chunk)

__attribute__((format(printf, 3, 4))) static inline void
error(clox_compiler_t *c, const clox_token_t *token, const char *fmt, ...) {
  if (c->panic_mode) {
    // do nothing in panic mode
    return;
  }

  c->panic_mode = true;
  c->had_error = true;

  if (c->error_handler != NULL) {
    va_list ap;
    va_start(ap, fmt);
    char message[MAX_ERROR_LENGTH + 1];
    clox_format_error(&message, fmt, ap);
    va_end(ap);

    // report the error
    c->error_handler(
        &(clox_error_info_t){
            .message = message,
            .num_locations = 1,
            .positions = {token->pos},
            .function_names = {c->frame->function->name},
            .file_names = {c->frame->function->file_name},
            .sources = {c->frame->function->source},
        },
        c->error_ctx);
  }
}

static inline void advance(clox_compiler_t *c) {
  c->previous = c->current;
  while (1) {
    c->current = clox_scan(&c->scanner);
    if (c->current.type != TOKEN_ERROR) {
      // scanned a legal token
      break;
    }
    // scanner error messages are in the token
    error(c, &c->current, "%s", c->current.start);
  }
}

static inline void consume(clox_compiler_t *c, clox_token_type_t type, const char *error_msg) {
  assert(type < TOKEN_TYPE_COUNT);
  if (c->current.type == type) {
    advance(c);
    return;
  }

  error(c, &c->current, "%s", error_msg);
}

static inline bool check(clox_compiler_t *c, clox_token_type_t type) {
  return c->current.type == type;
}

static inline bool match(clox_compiler_t *c, clox_token_type_t type) {
  if (!check(c, type)) {
    return false;
  }
  advance(c);
  return true;
}

static inline void synchronize(clox_compiler_t *c) {
  c->panic_mode = false;

  while (!check(c, TOKEN_EOF)) {
    if (c->previous.type == TOKEN_SEMICOLON) {
      // finished a statement
      return;
    }

    switch (c->current.type) {
    case TOKEN_BREAK:
    case TOKEN_CLASS:
    case TOKEN_CONTINUE:
    case TOKEN_FUN:
    case TOKEN_VAR:
    case TOKEN_FOR:
    case TOKEN_IF:
    case TOKEN_WHILE:
    case TOKEN_PRINT:
    case TOKEN_RETURN:
      // starting a keyword-led statement
      return;
    default:
      break;
    }

    advance(c);
  }
}

static inline void emit_byte(const clox_compiler_t *c, clox_byte_t byte,
                             const clox_token_t *token) {
  clox_chunk_write(&CHUNK(c), byte, token->pos);
}

static inline void emit_byte_op(const clox_compiler_t *c, clox_op_code_t opcode, clox_byte_t byte,
                                const clox_token_t *token) {
  // cast is safe by construction
  clox_chunk_write(&CHUNK(c), (clox_byte_t)opcode, token->pos);
  clox_chunk_write(&CHUNK(c), byte, token->pos);
}

static inline void emit_constant(clox_compiler_t *c, clox_op_code_t opcode, clox_value_t val,
                                 const clox_token_t *token) {
  if (!clox_write_constant(&CHUNK(c), opcode, val, token->pos)) {
    error(c, token, "constant limit exceeded");
  }
}

static inline size_t emit_jump(clox_compiler_t *c, clox_op_code_t opcode,
                               const clox_token_t *token) {
  // cast is safe by construction
  emit_byte(c, (clox_byte_t)opcode, token);
  emit_byte(c, UCHAR_MAX, token); // placeholder
  emit_byte(c, UCHAR_MAX, token); // placeholder

  // offset position in the code
  return CHUNK(c).length - 2;
}

_Static_assert(sizeof(size_t) >= 2, "sizeof(size_t) < 2");

#define TWO_BYTE_MAX (((size_t)UCHAR_MAX << CHAR_BIT) | UCHAR_MAX)

static inline void patch_jump(clox_compiler_t *c, size_t jump_pos, const clox_token_t *token) {
  assert(jump_pos + 2 <= CHUNK(c).length);
  // the length of forward jump after reading the whole
  // jump instruction (including the 2 offset bytes)
  size_t offset = CHUNK(c).length - jump_pos - 2;

  if (offset > TWO_BYTE_MAX) {
    error(c, token, "too long forward jump of %zu", offset);
  }

  // the two-byte jump offset encoded in big-endian
  CHUNK(c).code[jump_pos] = (clox_byte_t)(offset >> CHAR_BIT);
  CHUNK(c).code[jump_pos + 1] = (clox_byte_t)offset;
}

static inline void emit_loop(clox_compiler_t *c, size_t loop_start, const clox_token_t *token) {
  emit_byte(c, OP_LOOP, token);

  assert(loop_start <= CHUNK(c).length);
  // the length of backward jump after reading the whole
  // loop instruction (including the 2 offset bytes)
  size_t offset = CHUNK(c).length - loop_start + 2;

  if (offset > TWO_BYTE_MAX) {
    error(c, token, "too long backward jump of %zu", offset);
  }

  // the two-byte jump offset encoded in big-endian
  emit_byte(c, (clox_byte_t)(offset >> CHAR_BIT), token);
  emit_byte(c, (clox_byte_t)offset, token);
}

static inline void emit_pop_n(clox_compiler_t *c, size_t n_to_pop, const clox_token_t *token) {
  if (n_to_pop == 1) {
    // use OP_POP for single pop as more efficient
    emit_byte(c, OP_POP, token);
  } else {
    while (n_to_pop > 0) {
      // cast is safe by construction
      clox_byte_t n_part = (n_to_pop > UCHAR_MAX) ? UCHAR_MAX : (clox_byte_t)n_to_pop;
      emit_byte_op(c, OP_POP_N, n_part, token);
      n_to_pop -= n_part;
    }
  }
}

static inline void scan_to_eof(clox_compiler_t *c) {
  while (c->current.type != TOKEN_EOF) {
    advance(c);
  }
}

static inline bool at_max_parser_depth(clox_compiler_t *c) {
  if (c->parser_depth >= MAX_PARSER_DEPTH) {
    error(c, &c->current, "max parser depth exceeded");
    scan_to_eof(c); // no point in trying to recover
    return true;
  }
  return false;
}

static inline bool at_max_declaration_depth(clox_compiler_t *c) {
  if (c->declaration_depth >= MAX_DECLARATION_DEPTH) {
    error(c, &c->current, "max declaration depth exceeded");
    scan_to_eof(c); // no point in trying to recover
    return true;
  }
  return false;
}

static inline void emit_return(const clox_compiler_t *c, const clox_token_t *token) {
  emit_byte(c, OP_NIL, token); // return nil
  emit_byte(c, OP_RETURN, token);
}

static inline void start_frame(clox_compiler_t *c, clox_compile_frame_t *frame,
                               clox_compile_function_type_t type, const char *fn_name,
                               size_t fn_name_length) {
  assert(fn_name != NULL);
  assert(fn_name_length > 0);

  frame->local_count = 0;
  frame->scope_depth = 0;
  frame->loop = (clox_compile_loop_state_t){0};

  // arity set to zero here may be incremented in parameter parsing
  frame->function =
      clox_new_function(c->allocator, fn_name, fn_name_length, 0, c->file_name, c->source);
  frame->type = type;

  // first local in a frame is reserved for the function object itself
  clox_compile_local_t *func_local = &frame->locals[frame->local_count++];
  func_local->depth = 0;       // at global scope
  func_local->name.start = ""; // not resolvable
  func_local->name.length = 0;
  func_local->initialized = true;
  func_local->is_captured = false;

  frame->enclosing = c->frame;
  c->frame = frame;
}

static inline clox_function_t *end_frame(clox_compiler_t *c) {
  emit_return(c, &c->previous); // at most recent token
  clox_function_t *function = FUNCTION(c);

#if CLOX_DEBUG_COMPILATION
  if (!c->had_error) {
    printf("---- CODE ");
    clox_value_printf(CLOX_OBJECT(function));
    printf("\n");
    for (size_t offset = 0; offset < function->chunk.length;) {
      printf("---- CODE ");
      offset = clox_disassemble_instruction(&function->chunk, offset);
    }
    printf("\n");
  }
#endif

  c->frame = c->frame->enclosing;
  return function;
}

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT, // =
  PREC_OR,         // or
  PREC_AND,        // and
  PREC_EQUALITY,   // == !=
  PREC_COMPARISON, // < > <= >=
  PREC_TERM,       // + -
  PREC_FACTOR,     // * /
  PREC_UNARY,      // ! -
  PREC_CALL,       // . ()
  PREC_PRIMARY,    // literals identifiers
  PREC_COUNT,
} clox_precedence_t;

typedef void clox_parse_fn_t(clox_compiler_t *c, bool can_assign);

typedef struct {
  clox_parse_fn_t *const prefix_fn;
  clox_parse_fn_t *const infix_fn;
  clox_precedence_t infix_prec;
} clox_parse_rule_t;

static void declaration(clox_compiler_t *c);
static void var_declaration(clox_compiler_t *c);
static void statement(clox_compiler_t *c);
static void expression_statement(clox_compiler_t *c);
static clox_parse_rule_t get_rule(clox_token_type_t type);

static void parse(clox_compiler_t *c, clox_precedence_t prec) {
  c->parser_depth++;
  if (at_max_parser_depth(c)) {
    goto ret;
  }

  assert(prec != PREC_NONE && prec < PREC_COUNT);
  advance(c); // scan another token

  // every expression must start with a prefix rule
  clox_parse_fn_t *prefix_fn = get_rule(c->previous.type).prefix_fn;
  if (prefix_fn == NULL) {
    error(c, &c->previous, "expect expression");
    goto ret;
  }

  bool can_assign = (prec <= PREC_ASSIGNMENT);
  prefix_fn(c, can_assign); // parse by prefix rule

  // parse everything with prec or higher infix precedence
  while (prec <= get_rule(c->current.type).infix_prec) {
    advance(c); // current token moves to previous position
    clox_parse_fn_t *infix_fn = get_rule(c->previous.type).infix_fn;
    assert(infix_fn != NULL); // must be defined when != PREC_NONE (0)
    infix_fn(c, can_assign);  // parse by infix rule
  }

  if (can_assign && match(c, TOKEN_EQUAL)) {
    error(c, &c->previous, "invalid assignment target");
  }

ret:
  c->parser_depth--;
}

static void expression(clox_compiler_t *c) {
  parse(c, PREC_ASSIGNMENT);
}

static void print_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  expression(c);
  size_t n_to_print = 1;
  while (match(c, TOKEN_COMMA)) {
    expression(c);
    n_to_print++;
  }
  consume(c, TOKEN_SEMICOLON, "expect ';' after values");

  if (n_to_print > UCHAR_MAX) {
    error(c, &keyword, "max. %d print args allowed", UCHAR_MAX);
  }

  if (n_to_print == 1) {
    emit_byte(c, OP_PRINT, &keyword);
  } else {
    emit_byte(c, OP_PRINT_N, &keyword);
    // cast is safe: the range check error above
    emit_byte(c, (clox_byte_t)n_to_print, &keyword);
  }
}

static inline void begin_scope(clox_compiler_t *c) {
  FRAME(c)->scope_depth++;
}

static inline size_t pop_locals_above(clox_compiler_t *c, size_t depth, const clox_token_t *token) {
  size_t total = 0;
  size_t n_to_pop = 0;

  for (int i = (int)FRAME(c)->local_count - 1; i >= 0; i--) {
    const clox_compile_local_t *local = &FRAME(c)->locals[i];
    if (local->depth <= depth) {
      break;
    }
    if (local->is_captured) {
      // emit POP_N for all before
      emit_pop_n(c, n_to_pop, token);
      emit_byte(c, OP_CLOSE_UPVALUE, token);
      n_to_pop = 0;
    } else {
      n_to_pop++;
    }
    total++;
  }

  // emit POP_N for the rest
  emit_pop_n(c, n_to_pop, token);
  return total;
}

static inline void end_scope(clox_compiler_t *c, const clox_token_t *token) {
  FRAME(c)->scope_depth--;
  size_t n_popped = pop_locals_above(c, FRAME(c)->scope_depth, token);
  assert(n_popped < FRAME(c)->local_count); // local #0 can't be popped
  FRAME(c)->local_count -= n_popped;
}

static inline void block(clox_compiler_t *c) {
  while (!check(c, TOKEN_RIGHT_BRACE) && !check(c, TOKEN_EOF)) {
    declaration(c);
  }

  consume(c, TOKEN_RIGHT_BRACE, "expect '}' after block");
}

static void block_statement(clox_compiler_t *c) {
  begin_scope(c);
  block(c);
  end_scope(c, &c->previous); // at closing brace
}

static void if_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  consume(c, TOKEN_LEFT_PAREN, "expect '(' before if condition");
  expression(c); // condition
  consume(c, TOKEN_RIGHT_PAREN, "expect ')' after if condition");

  size_t after_then_jump = emit_jump(c, OP_JUMP_FALSE_POP, &keyword);
  statement(c); // then block

  size_t after_else_jump = SIZE_MAX;
  if (match(c, TOKEN_ELSE)) {
    after_else_jump = emit_jump(c, OP_JUMP, &keyword);
  }

  patch_jump(c, after_then_jump, &keyword);

  if (after_else_jump != SIZE_MAX) {
    statement(c); // else block
    patch_jump(c, after_else_jump, &keyword);
  }
}

static void for_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  consume(c, TOKEN_LEFT_PAREN, "expect '(' before for clauses");

  begin_scope(c);

  // initializer
  if (!match(c, TOKEN_SEMICOLON)) {
    if (match(c, TOKEN_VAR)) {
      var_declaration(c);
    } else {
      expression_statement(c);
    }
  }

  // return here after increment (or body if none)
  size_t loop_start = CHUNK(c).length;

  // condition
  size_t cond_exit_jump = SIZE_MAX;
  if (!match(c, TOKEN_SEMICOLON)) {
    expression(c); // condition expression
    consume(c, TOKEN_SEMICOLON, "expect ';' after for condition");
    cond_exit_jump = emit_jump(c, OP_JUMP_FALSE_POP, &keyword);
  }

  // increment
  if (!match(c, TOKEN_RIGHT_PAREN)) {
    // skip the increment, jump to body
    size_t body_jump = emit_jump(c, OP_JUMP, &keyword);
    // return here after body
    size_t increment_start = CHUNK(c).length;
    expression(c); // increment expression
    emit_byte(c, OP_POP, &keyword);
    consume(c, TOKEN_RIGHT_PAREN, "expect ')' after for clauses");
    // return to the loop start
    emit_loop(c, loop_start, &keyword);
    loop_start = increment_start;
    patch_jump(c, body_jump, &keyword);
  }

  clox_compile_loop_state_t prev_state = LOOP(c);
  LOOP(c) = (clox_compile_loop_state_t){
      .inside = true,
      .start = loop_start,
      .exit_patch = SIZE_MAX, // sentinel
      .scope = FRAME(c)->scope_depth,
  };
  statement(c); // loop body
  size_t exit_jump = LOOP(c).exit_patch;
  LOOP(c) = prev_state;

  emit_loop(c, loop_start, &keyword);
  if (cond_exit_jump != SIZE_MAX) {
    patch_jump(c, cond_exit_jump, &keyword);
  }
  if (exit_jump != SIZE_MAX) {
    // patch first break's exit jump
    patch_jump(c, exit_jump, &keyword);
  }

  end_scope(c, &keyword);
}

static void while_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  size_t loop_start = CHUNK(c).length;

  consume(c, TOKEN_LEFT_PAREN, "expect '(' before while condition");
  expression(c); // condition
  consume(c, TOKEN_RIGHT_PAREN, "expect ')' after while condition");

  size_t cond_exit_jump = emit_jump(c, OP_JUMP_FALSE_POP, &keyword);

  clox_compile_loop_state_t prev_state = LOOP(c);
  LOOP(c) = (clox_compile_loop_state_t){
      .inside = true,
      .start = loop_start,
      .exit_patch = SIZE_MAX, // sentinel
      .scope = FRAME(c)->scope_depth,
  };
  statement(c); // loop body
  size_t exit_jump = LOOP(c).exit_patch;
  LOOP(c) = prev_state;

  emit_loop(c, loop_start, &keyword);
  patch_jump(c, cond_exit_jump, &keyword);
  if (exit_jump != SIZE_MAX) {
    // patch first break's exit jump
    patch_jump(c, exit_jump, &keyword);
  }
}

static void break_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  consume(c, TOKEN_SEMICOLON, "expect ';' after break");

  if (!LOOP(c).inside) {
    error(c, &keyword, "break allowed only inside loop body");
    return;
  }

  // pop all locals from above the loop's scope
  pop_locals_above(c, LOOP(c).scope, &keyword);

  // exit the loop
  if (LOOP(c).exit_patch == SIZE_MAX) {
    // create new exit patch
    LOOP(c).exit_patch = emit_jump(c, OP_JUMP, &keyword);
  } else {
    // loop back to the existing exit patch
    // the jump opcode is 1 byte behind the patch
    size_t loop_exit = LOOP(c).exit_patch - 1;
    emit_loop(c, loop_exit, &keyword);
  }
}

static void continue_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  consume(c, TOKEN_SEMICOLON, "expect ';' after continue");

  if (!LOOP(c).inside) {
    error(c, &keyword, "continue allowed only inside loop body");
    return;
  }

  // pop all locals from above the loop's scope
  pop_locals_above(c, LOOP(c).scope, &keyword);

  // move to the loop start
  emit_loop(c, LOOP(c).start, &keyword);
}

static void return_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  if (FRAME(c)->type == FUNCTION_SCRIPT) {
    error(c, &keyword, "can't return from top-level code");
  }

  if (match(c, TOKEN_SEMICOLON)) {
    emit_return(c, &keyword);
  } else {
    expression(c);
    consume(c, TOKEN_SEMICOLON, "expect ';' after return value");
    emit_byte(c, OP_RETURN, &keyword);
  }
}

static void expression_statement(clox_compiler_t *c) {
  expression(c);
  consume(c, TOKEN_SEMICOLON, "expect ';' after expression");
  emit_byte(c, OP_POP, &c->previous); // at semicolon token
}

static void statement(clox_compiler_t *c) {
  c->parser_depth++;
  if (at_max_parser_depth(c)) {
    goto ret;
  }

  if (match(c, TOKEN_PRINT)) {
    print_statement(c);
  } else if (match(c, TOKEN_IF)) {
    if_statement(c);
  } else if (match(c, TOKEN_FOR)) {
    for_statement(c);
  } else if (match(c, TOKEN_WHILE)) {
    while_statement(c);
  } else if (match(c, TOKEN_BREAK)) {
    break_statement(c);
  } else if (match(c, TOKEN_CONTINUE)) {
    continue_statement(c);
  } else if (match(c, TOKEN_RETURN)) {
    return_statement(c);
  } else if (match(c, TOKEN_LEFT_BRACE)) {
    block_statement(c);
  } else {
    expression_statement(c);
  }

ret:
  c->parser_depth--;
}

static inline bool names_equal(const clox_token_t *a, const clox_token_t *b) {
  if (a->length != b->length) {
    return false;
  }
  return memcmp(a->start, b->start, a->length) == 0;
}

static inline void add_local(clox_compiler_t *c, clox_compile_frame_t *f,
                             const clox_token_t *name) {
  assert(f->scope_depth > 0);

  if (f->local_count >= CLOX_MAX_LOCALS) {
    error(c, name, "locals limit exceeded");
    return;
  }

  clox_compile_local_t *local = f->locals + f->local_count;
  local->name = *name;
  local->depth = f->scope_depth;
  local->initialized = false;
  local->is_captured = false;
  f->local_count++;
}

static inline size_t resolve_local(clox_compiler_t *c, clox_compile_frame_t *f,
                                   const clox_token_t *name) {
  // cast is safe: MAX_LOCALS <= INT_MAX
  for (int i = (int)f->local_count - 1; i >= 0; i--) {
    clox_compile_local_t *local = f->locals + i;
    if (names_equal(&local->name, name)) {
      if (!local->initialized) {
        error(c, name, "can't read '%.*s' in its own initializer", (int)name->length, name->start);
      }

      // cast is safe: range is non-negative
      return (size_t)i;
    }
  }

  return CLOX_MAX_LOCALS; // sentinel
}

static inline size_t add_upvalue(clox_compiler_t *c, clox_compile_frame_t *f, size_t index,
                                 bool is_local, const clox_token_t *name) {
  assert(f->scope_depth > 0);

  size_t count = f->function->upvalue_count;

  for (size_t i = 0; i < count; i++) {
    if (f->upvalues[i].index == index && f->upvalues[i].is_local == is_local) {
      return i;
    }
  }

  if (count >= CLOX_MAX_UPVALUES) {
    error(c, name, "upvalues limit exceeded");
    return 0; // incorrect but ok, given the error
  }

  f->upvalues[count].index = index;
  f->upvalues[count].is_local = is_local;

  return f->function->upvalue_count++;
}

static inline size_t resolve_upvalue(clox_compiler_t *c, clox_compile_frame_t *f,
                                     const clox_token_t *name) {
  if (f->enclosing == NULL) {
    // no enclosing frame to resolve upvalues in
    return CLOX_MAX_UPVALUES; // sentinel
  }

  size_t local_idx = resolve_local(c, f->enclosing, name);
  if (local_idx < CLOX_MAX_LOCALS) {
    // local resolved in enclosing frame
    f->enclosing->locals[local_idx].is_captured = true;
    return add_upvalue(c, f, local_idx, true, name);
  }

  size_t upvalue_idx = resolve_upvalue(c, f->enclosing, name);
  if (upvalue_idx < CLOX_MAX_UPVALUES) {
    // local resolved in transitively enclosing frame
    return add_upvalue(c, f, upvalue_idx, false, name);
  }

  return CLOX_MAX_UPVALUES; // sentinel
}

_Static_assert(CLOX_MAX_LOCALS <= INT_MAX, "MAX_LOCALS must fit within int");

static inline void declare_variable(clox_compiler_t *c, const clox_token_t *name) {
  assert(FRAME(c)->scope_depth > 0);

  // cast is safe: MAX_LOCALS <= INT_MAX
  for (int i = (int)FRAME(c)->local_count - 1; i >= 0; i--) {
    clox_compile_local_t *local = FRAME(c)->locals + i;
    if (local->initialized && local->depth < FRAME(c)->scope_depth) {
      break;
    }
    if (names_equal(&local->name, name)) {
      error(c, name, "'%.*s' already defined in this scope", (int)name->length, name->start);
      return;
    }
  }

  add_local(c, FRAME(c), name);
}

static inline void mark_initialized(clox_compiler_t *c) {
  FRAME(c)->locals[FRAME(c)->local_count - 1].initialized = true;
}

_Static_assert(CLOX_MAX_UPVALUES <= UCHAR_MAX + 1, "upvalue index must fit in a byte");

static inline void function(clox_compiler_t *c, clox_compile_function_type_t type,
                            const clox_token_t *name) {
  clox_compile_frame_t frame;
  start_frame(c, &frame, type, name->start, name->length);

  begin_scope(c); // function scope

  // function params
  consume(c, TOKEN_LEFT_PAREN, "expect '(' before function parameters");
  if (!check(c, TOKEN_RIGHT_PAREN)) {
    do {
      consume(c, TOKEN_IDENTIFIER, "expect param name");
      clox_token_t param_name = c->previous;
      FUNCTION(c)->arity++;
      if (FUNCTION(c)->arity > CLOX_MAX_ARITY) {
        error(c, &param_name, "max. %d params allowed", CLOX_MAX_ARITY);
      }
      declare_variable(c, &param_name);
      mark_initialized(c);
    } while (match(c, TOKEN_COMMA));
  }
  consume(c, TOKEN_RIGHT_PAREN, "expect ')' after function parameters");

  consume(c, TOKEN_LEFT_BRACE, "expect '{' before function body");
  block(c); // function body (block call consumes closing brace)
  // end_frame always emits return which will rewind the
  // caller's stack, so no need to call end_scope

  // the frame is saved before it's ended
  clox_compile_frame_t *fn_frame = FRAME(c);
  clox_function_t *fn = end_frame(c);
  emit_constant(c, OP_CLOSURE, CLOX_OBJECT(fn), name);
  for (size_t i = 0; i < fn->upvalue_count; i++) {
    emit_byte(c, fn_frame->upvalues[i].is_local ? 1 : 0, name);
    // cast is safe: static assert above
    emit_byte(c, (clox_byte_t)fn_frame->upvalues[i].index, name);
  }
}

static void fun_declaration(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;
  consume(c, TOKEN_IDENTIFIER, "expect function name");
  clox_token_t name = c->previous;

  if (FRAME(c)->scope_depth > 0) { // local scope
    // for recursion: locally declared function name
    // must be visible from within its body (via upvalue)
    declare_variable(c, &name);
    mark_initialized(c);
  }

  function(c, FUNCTION_FUNCTION, &name);

  if (FRAME(c)->scope_depth == 0) { // global scope
    clox_value_t name_str = CLOX_STRING_COPY(c->allocator, name.start, name.length);
    emit_constant(c, OP_DEF_GLOBAL, name_str, &keyword);
  }
}

static void var_declaration(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;
  consume(c, TOKEN_IDENTIFIER, "expect variable name");
  clox_token_t name = c->previous;

  if (FRAME(c)->scope_depth > 0) { // local scope
    declare_variable(c, &name);
  }

  if (match(c, TOKEN_EQUAL)) {
    // initializer
    expression(c);
  } else {
    // initialize to NIL
    emit_byte(c, OP_NIL, &name);
  }

  consume(c, TOKEN_SEMICOLON, "expect ';' after variable declaration");

  if (FRAME(c)->scope_depth > 0) { // local scope
    mark_initialized(c);
  } else { // global scope
    clox_value_t name_str = CLOX_STRING_COPY(c->allocator, name.start, name.length);
    emit_constant(c, OP_DEF_GLOBAL, name_str, &keyword);
  }
}

static void declaration(clox_compiler_t *c) {
  c->declaration_depth++;
  if (at_max_declaration_depth(c)) {
    goto ret;
  }

  if (match(c, TOKEN_FUN)) {
    fun_declaration(c);
  } else if (match(c, TOKEN_VAR)) {
    var_declaration(c);
  } else {
    statement(c);
  }

  if (c->panic_mode) {
    synchronize(c);
  }

ret:
  c->declaration_depth--;
}

static void grouping(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  expression(c);
  consume(c, TOKEN_RIGHT_PAREN, "expect ')' after expression");
}

static void binary(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t op = c->previous;
  clox_parse_rule_t rule = get_rule(op.type);

  // compile right operand (left is already in):
  // everything with higher precedence than op
  assert(rule.infix_prec + 1 < PREC_COUNT);
  parse(c, rule.infix_prec + 1);

  switch (op.type) {
  case TOKEN_PLUS:
    emit_byte(c, OP_ADD, &op);
    break;
  case TOKEN_MINUS:
    emit_byte(c, OP_SUBTRACT, &op);
    break;
  case TOKEN_STAR:
    emit_byte(c, OP_MULTIPLY, &op);
    break;
  case TOKEN_SLASH:
    emit_byte(c, OP_DIVIDE, &op);
    break;
  case TOKEN_EQUAL_EQUAL:
    emit_byte(c, OP_EQUAL, &op);
    break;
  case TOKEN_BANG_EQUAL:
    emit_byte(c, OP_NOT_EQUAL, &op);
    break;
  case TOKEN_GREATER:
    emit_byte(c, OP_GREATER, &op);
    break;
  case TOKEN_GREATER_EQUAL:
    emit_byte(c, OP_GREATER_EQUAL, &op);
    break;
  case TOKEN_LESS:
    emit_byte(c, OP_LESS, &op);
    break;
  case TOKEN_LESS_EQUAL:
    emit_byte(c, OP_LESS_EQUAL, &op);
    break;
  default:
    assert(0 && "unreachable");
  }
}

static void unary(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t op = c->previous;

  // compile the operand
  parse(c, PREC_UNARY);

  switch (op.type) {
  case TOKEN_BANG:
    emit_byte(c, OP_NOT, &op);
    break;
  case TOKEN_MINUS:
    emit_byte(c, OP_NEGATE, &op);
    break;
  default:
    assert(0 && "unreachable");
  }
}

static void number(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t token = c->previous;

  char *term = (char *)token.start + token.length;
  char prev_char = *term;

  errno = 0;
  char *parse_end;
  *term = '\0'; // temp modify
  double val = strtod(token.start, &parse_end);
  *term = prev_char; // restore

  assert(parse_end == term); // must parse the whole string
  if (errno != ERANGE) {
    emit_constant(c, OP_CONSTANT, CLOX_NUMBER(val), &token);
  } else {
    error(c, &token, "number out of range");
  }
}

static void string(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t token = c->previous;

  const char *chars = token.start + 1; // skip leading "
  size_t length = token.length - 2;    // skip both "s

  emit_constant(c, OP_CONSTANT, CLOX_STRING_COPY(c->allocator, chars, length), &token);
}

static void literal(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t token = c->previous;

  switch (token.type) {
  case TOKEN_NIL:
    emit_byte(c, OP_NIL, &token);
    break;
  case TOKEN_TRUE:
    emit_byte(c, OP_TRUE, &token);
    break;
  case TOKEN_FALSE:
    emit_byte(c, OP_FALSE, &token);
    break;
  default:
    assert(0 && "unreachable");
  }
}

_Static_assert(CLOX_MAX_LOCALS - 1 <= UCHAR_MAX, "local index should fit in unsigned char");

static void variable(clox_compiler_t *c, bool can_assign) {
  clox_token_t name = c->previous;

  bool assign = can_assign && match(c, TOKEN_EQUAL);
  if (assign) {
    // evaluate lhs
    expression(c);
  }

  clox_op_code_t local_get_op = OP_CODE_COUNT;
  clox_op_code_t local_set_op = OP_CODE_COUNT;
  size_t local_idx = resolve_local(c, FRAME(c), &name);
  if (local_idx < CLOX_MAX_LOCALS) {
    // local variable
    local_get_op = OP_GET_LOCAL;
    local_set_op = OP_SET_LOCAL;
  } else {
    local_idx = resolve_upvalue(c, FRAME(c), &name);
    if (local_idx < CLOX_MAX_UPVALUES) {
      // non-global variable via upvalue
      local_get_op = OP_GET_UPVALUE;
      local_set_op = OP_SET_UPVALUE;
    }
  }

  if (local_get_op != OP_CODE_COUNT) {
    // cast is safe: range checked above
    clox_byte_t byte_idx = (clox_byte_t)local_idx;
    emit_byte_op(c, assign ? local_set_op : local_get_op, byte_idx, &name);
  } else {
    // global variable
    clox_value_t name_str = CLOX_STRING_COPY(c->allocator, name.start, name.length);
    emit_constant(c, assign ? OP_SET_GLOBAL : OP_GET_GLOBAL, name_str, &name);
  }
}

static void and_(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t keyword = c->previous;

  // if lhs is falsy, leave it on the stack and jump
  size_t end_jump = emit_jump(c, OP_JUMP_FALSE, &keyword);
  emit_byte(c, OP_POP, &keyword); // discard lhs
  parse(c, PREC_AND);             // parse rhs
  patch_jump(c, end_jump, &keyword);
}

static void or_(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t keyword = c->previous;

  // if lhs is truthy, leave it on the stack and jump
  size_t end_jump = emit_jump(c, OP_JUMP_TRUE, &keyword);
  emit_byte(c, OP_POP, &keyword); // discard lhs
  parse(c, PREC_OR);              // parse rhs
  patch_jump(c, end_jump, &keyword);
}

static inline size_t argument_list(clox_compiler_t *c) {
  size_t args_count = 0;
  if (!check(c, TOKEN_RIGHT_PAREN)) {
    do {
      expression(c);
      args_count++;
      if (args_count > CLOX_MAX_ARITY) {
        // error at the most recently matched expression token
        error(c, &c->previous, "max. %d args allowed", CLOX_MAX_ARITY);
      }
    } while (match(c, TOKEN_COMMA));
  }
  consume(c, TOKEN_RIGHT_PAREN, "expect ')' after args");

  return args_count;
}

static void call(clox_compiler_t *c, bool can_assign) {
  (void)can_assign;
  clox_token_t left_paren = c->previous;
  size_t args_count = argument_list(c);
  // cast: range check is done in argument_list
  emit_byte_op(c, OP_CALL, (clox_byte_t)args_count, &left_paren);
}

static const clox_parse_rule_t parse_rules[] = {
#define X(name, prefix_fn, infix_fn, prec) [TOKEN_##name] = {prefix_fn, infix_fn, prec},
#include "tokens.def"
#undef X
};

_Static_assert(CLOX_ARRAY_SIZE(parse_rules) == TOKEN_TYPE_COUNT, "parse rules array size mismatch");

static inline clox_parse_rule_t get_rule(clox_token_type_t type) {
  assert(type < TOKEN_TYPE_COUNT);
  return parse_rules[type];
}

static inline void mark_callback(clox_allocator_t *alloc, void *ctx) {
  clox_compiler_t *compiler = ctx;

  // active functions
  clox_compile_frame_t *frame = compiler->frame;
  while (frame != NULL) {
    clox_mark_object(alloc, (clox_object_t *)frame->function);
    frame = frame->enclosing;
  }
}

void clox_compiler_init(clox_compiler_t *compiler, clox_allocator_t *alloc) {
  compiler->allocator = alloc;
  compiler->frame = NULL;

  clox_compiler_reset_error_handler(compiler);

  compiler->mark_callback_handle =
      clox_register_mark_callback(compiler->allocator, mark_callback, compiler);
}

void clox_compiler_free(clox_compiler_t *compiler) {
  bool unregistered =
      clox_unregister_mark_callback(compiler->allocator, compiler->mark_callback_handle);
  assert(unregistered);
  (void)unregistered;

  compiler->allocator = NULL;

  clox_compiler_reset_error_handler(compiler);
}

void clox_compiler_set_error_handler(clox_compiler_t *compiler, clox_error_handler_t *error_handler,
                                     void *error_ctx) {
  compiler->error_handler = error_handler;
  compiler->error_ctx = error_ctx;
}

void clox_compiler_reset_error_handler(clox_compiler_t *compiler) {
  compiler->error_handler = NULL;
  compiler->error_ctx = NULL;
}

bool clox_compile(clox_compiler_t *compiler, const char *file_name, char *source,
                  clox_function_t **function) {
  // init parser
  clox_scanner_init(&compiler->scanner, source);
  compiler->file_name = file_name;
  compiler->source = source;
  compiler->had_error = false;
  compiler->panic_mode = false;
  compiler->parser_depth = 0;
  compiler->declaration_depth = 0;

  // init frame
  compiler->frame = NULL;

  clox_compile_frame_t script;
  start_frame(compiler, &script, FUNCTION_SCRIPT, CLOX_SCRIPT_NAME, strlen(CLOX_SCRIPT_NAME));
  advance(compiler); // scan the first token
  while (!match(compiler, TOKEN_EOF)) {
    // sequence of declaration statements
    declaration(compiler);
  }
  end_frame(compiler);

  // cleanup
  clox_scanner_free(&compiler->scanner);
  assert(compiler->parser_depth == 0);
  assert(compiler->declaration_depth == 0);
  assert(compiler->frame == NULL);

  if (compiler->had_error) {
    return false;
  }

  *function = script.function;
  return true;
}

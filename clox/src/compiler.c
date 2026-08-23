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
#include "debug.h"
#include "error.h"
#include "memory.h"
#include "object.h"
#include "scanner.h"
#include "value.h"

#define MAX_PARSER_DEPTH 20000

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
        (clox_error_info_t){
            .message = message,
            .pos = token->pos,
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
  clox_chunk_write(c->chunk, byte, token->pos);
}

static inline void emit_byte_op(const clox_compiler_t *c, clox_op_code_t opcode, clox_byte_t byte,
                                const clox_token_t *token) {
  // cast is safe by construction
  clox_chunk_write(c->chunk, (clox_byte_t)opcode, token->pos);
  clox_chunk_write(c->chunk, byte, token->pos);
}

static inline void emit_constant(clox_compiler_t *c, clox_op_code_t opcode, clox_value_t val,
                                 const clox_token_t *token) {
  if (!clox_write_constant(c->chunk, opcode, val, token->pos)) {
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
  return c->chunk->length - 2;
}

_Static_assert(sizeof(size_t) >= 2, "sizeof(size_t) < 2");

#define TWO_BYTE_MAX (((size_t)UCHAR_MAX << CHAR_BIT) | UCHAR_MAX)

static inline void patch_jump(clox_compiler_t *c, size_t jump_pos, const clox_token_t *token) {
  assert(jump_pos + 2 <= c->chunk->length);
  // the length of forward jump after reading the whole
  // jump instruction (including the 2 offset bytes)
  size_t offset = c->chunk->length - jump_pos - 2;

  if (offset > TWO_BYTE_MAX) {
    error(c, token, "too long forward jump of %zu", offset);
  }

  // the two-byte jump offset encoded in big-endian
  c->chunk->code[jump_pos] = (clox_byte_t)(offset >> CHAR_BIT);
  c->chunk->code[jump_pos + 1] = (clox_byte_t)offset;
}

static inline void emit_loop(clox_compiler_t *c, size_t loop_start, const clox_token_t *token) {
  emit_byte(c, OP_LOOP, token);

  assert(loop_start <= c->chunk->length);
  // the length of backward jump after reading the whole
  // loop instruction (including the 2 offset bytes)
  size_t offset = c->chunk->length - loop_start + 2;

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

static inline void end_compiler(const clox_compiler_t *c) {
  // TODO: remove this when compiling statements
  emit_byte(c, OP_RETURN, &c->previous); // EOF

#if CLOX_DEBUG_COMPILATION
  if (!c->had_error) {
    for (size_t offset = 0; offset < c->chunk->length;) {
      printf("---- CODE ");
      offset = clox_disassemble_instruction(c->chunk, offset);
    }
    printf("\n");
  }
#endif
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
  c->scope_depth++;
}

static inline size_t n_locals_above(const clox_compiler_t *c, size_t depth) {
  size_t result = 0;
  for (int i = (int)c->local_count - 1; i >= 0; i--) {
    if (c->locals[i].depth <= depth) {
      break; // in scope
    }
    result++;
  }
  return result;
}

static inline void end_scope(clox_compiler_t *c, const clox_token_t *token) {
  c->scope_depth--;
  size_t n_to_pop = n_locals_above(c, c->scope_depth);
  c->local_count -= n_to_pop;
  emit_pop_n(c, n_to_pop, token);
}

static void block_statement(clox_compiler_t *c) {
  begin_scope(c);

  while (!check(c, TOKEN_RIGHT_BRACE) && !check(c, TOKEN_EOF)) {
    declaration(c);
  }

  consume(c, TOKEN_RIGHT_BRACE, "expect '}' after block");

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
  size_t loop_start = c->chunk->length;

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
    size_t increment_start = c->chunk->length;
    expression(c); // increment expression
    emit_byte(c, OP_POP, &keyword);
    consume(c, TOKEN_RIGHT_PAREN, "expect ')' after for clauses");
    // return to the loop start
    emit_loop(c, loop_start, &keyword);
    loop_start = increment_start;
    patch_jump(c, body_jump, &keyword);
  }

  clox_loop_state_t prev_state = c->loop;
  c->loop = (clox_loop_state_t){
      .inside = true,
      .start = loop_start,
      .exit_patch = SIZE_MAX, // sentinel
      .scope = c->scope_depth,
  };
  statement(c); // loop body
  size_t exit_jump = c->loop.exit_patch;
  c->loop = prev_state;

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

  size_t loop_start = c->chunk->length;

  consume(c, TOKEN_LEFT_PAREN, "expect '(' before while condition");
  expression(c); // condition
  consume(c, TOKEN_RIGHT_PAREN, "expect ')' after while condition");

  size_t cond_exit_jump = emit_jump(c, OP_JUMP_FALSE_POP, &keyword);

  clox_loop_state_t prev_state = c->loop;
  c->loop = (clox_loop_state_t){
      .inside = true,
      .start = loop_start,
      .exit_patch = SIZE_MAX, // sentinel
      .scope = c->scope_depth,
  };
  statement(c); // loop body
  size_t exit_jump = c->loop.exit_patch;
  c->loop = prev_state;

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

  if (!c->loop.inside) {
    error(c, &keyword, "break allowed only inside loop body");
    return;
  }

  // pop all locals from above the loop's scope
  size_t n_to_pop = n_locals_above(c, c->loop.scope);
  emit_pop_n(c, n_to_pop, &keyword);

  // exit the loop
  if (c->loop.exit_patch == SIZE_MAX) {
    // create new exit patch
    c->loop.exit_patch = emit_jump(c, OP_JUMP, &keyword);
  } else {
    // loop back to the existing exit patch
    // the jump opcode is 1 byte behind the patch
    size_t loop_exit = c->loop.exit_patch - 1;
    emit_loop(c, loop_exit, &keyword);
  }
}

static void continue_statement(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;

  consume(c, TOKEN_SEMICOLON, "expect ';' after continue");

  if (!c->loop.inside) {
    error(c, &keyword, "continue allowed only inside loop body");
    return;
  }

  // pop all locals from above the loop's scope
  size_t n_to_pop = n_locals_above(c, c->loop.scope);
  emit_pop_n(c, n_to_pop, &keyword);

  // move to the loop start
  emit_loop(c, c->loop.start, &keyword);
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

static inline void add_local(clox_compiler_t *c, const clox_token_t *name) {
  assert(c->scope_depth > 0);

  if (c->local_count >= CLOX_MAX_LOCALS) {
    error(c, name, "max. %d locals allowed at a time", CLOX_MAX_LOCALS);
    return;
  }

  clox_local_t *local = c->locals + c->local_count;
  local->name = *name;
  local->depth = c->scope_depth;
  local->initialized = false;
  c->local_count++;
}

static inline size_t resolve_local(clox_compiler_t *c, const clox_token_t *name) {
  // cast is safe: MAX_LOCALS <= INT_MAX
  for (int i = (int)c->local_count - 1; i >= 0; i--) {
    clox_local_t *local = c->locals + i;
    if (names_equal(&local->name, name)) {
      if (!local->initialized) {
        error(c, name, "can't read '%.*s' in its own initializer", (int)name->length, name->start);
      }

      // cast is safe: range is non-negative
      return (size_t)i;
    }
  }
  // sentinel value
  return CLOX_MAX_LOCALS;
}

_Static_assert(CLOX_MAX_LOCALS <= INT_MAX, "MAX_LOCALS must fit within int");

static inline void declare_variable(clox_compiler_t *c, const clox_token_t *name) {
  assert(c->scope_depth > 0);

  // cast is safe: MAX_LOCALS <= INT_MAX
  for (int i = (int)c->local_count - 1; i >= 0; i--) {
    clox_local_t *local = c->locals + i;
    if (local->initialized && local->depth < c->scope_depth) {
      break;
    }
    if (names_equal(&local->name, name)) {
      error(c, name, "'%.*s' already defined in this scope", (int)name->length, name->start);
      return;
    }
  }

  add_local(c, name);
}

static void var_declaration(clox_compiler_t *c) {
  clox_token_t keyword = c->previous;
  consume(c, TOKEN_IDENTIFIER, "expect variable name");
  clox_token_t name = c->previous;

  if (c->scope_depth > 0) { // local scope
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

  if (c->scope_depth > 0) { // local scope
    // mark the declared variable initialized
    c->locals[c->local_count - 1].initialized = true;
  } else { // global scope
    clox_value_t name_str = CLOX_STRING_COPY(c->allocator, name.start, name.length);
    emit_constant(c, OP_DEF_GLOBAL, name_str, &keyword);
  }
}

static void declaration(clox_compiler_t *c) {
  if (match(c, TOKEN_VAR)) {
    var_declaration(c);
  } else {
    statement(c);
  }

  if (c->panic_mode) {
    synchronize(c);
  }
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

  bool assignment = can_assign && match(c, TOKEN_EQUAL);
  if (assignment) {
    // evaluate lhs
    expression(c);
  }

  size_t local_idx = resolve_local(c, &name);
  if (local_idx < CLOX_MAX_LOCALS) {
    // local variable
    // cast is safe: range checked above
    clox_byte_t byte_idx = (clox_byte_t)local_idx;
    if (assignment) {
      // set local
      emit_byte_op(c, OP_SET_LOCAL, byte_idx, &name);
    } else {
      // get local
      emit_byte_op(c, OP_GET_LOCAL, byte_idx, &name);
    }
  } else {
    // global variable
    clox_value_t name_str = CLOX_STRING_COPY(c->allocator, name.start, name.length);
    if (assignment) {
      // set global
      emit_constant(c, OP_SET_GLOBAL, name_str, &name);
    } else {
      // get global
      emit_constant(c, OP_GET_GLOBAL, name_str, &name);
    }
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

void clox_compiler_init(clox_compiler_t *compiler, clox_allocator_t *alloc) {
  compiler->allocator = alloc;
  clox_compiler_reset_error_handler(compiler);
}

void clox_compiler_free(clox_compiler_t *compiler) {
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

bool clox_compile(clox_compiler_t *compiler, char *source, clox_chunk_t *chunk) {
  // init
  clox_scanner_init(&compiler->scanner, source);
  compiler->chunk = chunk;
  compiler->had_error = false;
  compiler->panic_mode = false;
  compiler->parser_depth = 0;
  compiler->local_count = 0;
  compiler->scope_depth = 0;
  compiler->loop = (clox_loop_state_t){0};

  advance(compiler); // scan the first token
  while (!match(compiler, TOKEN_EOF)) {
    // sequence of declaration statements
    declaration(compiler);
  }
  end_compiler(compiler);

  assert(compiler->parser_depth == 0);

  // cleanup
  clox_scanner_free(&compiler->scanner);
  compiler->chunk = NULL;

  return !compiler->had_error;
}

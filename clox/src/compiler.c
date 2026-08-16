#include "compiler.h"

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunk.h"
#include "debug.h"
#include "error.h"
#include "memory.h"
#include "scanner.h"
#include "value.h"

#define MAX_PARSER_DEPTH 20000
#define ERROR_MESSAGE_SIZE 512

typedef struct {
  clox_chunk_t *const chunk;
  clox_scanner_t *const scanner;
  clox_token_t previous;
  clox_token_t current;
  bool had_error;
  bool panic_mode;
  clox_error_handler_t *const error_handler;
  void *const error_ctx;
  size_t parser_depth;
} clox_compiler_t;

__attribute__((format(printf, 3, 4))) static inline void
error(clox_compiler_t *c, const clox_token_t *token, const char *fmt, ...) {
  // do nothing in panic mode
  if (!c->panic_mode) {
    c->panic_mode = true;
    c->had_error = true;

    if (c->error_handler != NULL) {
      char message[ERROR_MESSAGE_SIZE];

      va_list ap;
      va_start(ap, fmt);
      (void)vsnprintf(message, sizeof(message), fmt, ap);
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
}

static inline void advance(clox_compiler_t *c) {
  c->previous = c->current;
  while (1) {
    c->current = clox_scan_token(c->scanner);
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

static inline void emit_byte(const clox_compiler_t *c, clox_byte_t byte,
                             const clox_token_t *token) {
  clox_write_chunk(c->chunk, byte, token->pos);
}

static inline void emit_constant(clox_compiler_t *c, clox_value_t value,
                                 const clox_token_t *token) {
  if (!clox_write_constant(c->chunk, value, token->pos)) {
    error(c, token, "constant limit exceeded");
  }
}

static inline void end_compiler(const clox_compiler_t *c) {
  // TODO: remove this when compiling statements
  emit_byte(c, OP_RETURN, &c->previous); // EOF

#if CLOX_DEBUG_COMPILATION
  if (!c->had_error) {
    clox_disassemble_chunk(c->chunk, "code");
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

typedef void clox_parse_fn_t(clox_compiler_t *c);

typedef struct {
  clox_parse_fn_t *const prefix_fn;
  clox_parse_fn_t *const infix_fn;
  clox_precedence_t infix_prec;
} clox_parse_rule_t;

static clox_parse_rule_t get_rule(clox_token_type_t type);

static void parse(clox_compiler_t *c, clox_precedence_t prec) {
  c->parser_depth++;
  if (c->parser_depth >= MAX_PARSER_DEPTH) {
    error(c, &c->current, "max parser depth exceeded");
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
  prefix_fn(c); // parse by prefix rule

  // parse everything with prec or higher infix precedence
  while (prec <= get_rule(c->current.type).infix_prec) {
    advance(c); // current token moves to previous position
    clox_parse_fn_t *infix_fn = get_rule(c->previous.type).infix_fn;
    assert(infix_fn != NULL); // must be defined when != PREC_NONE (0)
    infix_fn(c);              // parse by infix rule
  }

ret:
  c->parser_depth--;
}

static void expression(clox_compiler_t *c) {
  parse(c, PREC_ASSIGNMENT);
}

static void grouping(clox_compiler_t *c) {
  expression(c);
  consume(c, TOKEN_RIGHT_PAREN, "expect ')' after expression");
}

static void binary(clox_compiler_t *c) {
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

static void unary(clox_compiler_t *c) {
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

static void number(clox_compiler_t *c) {
  char *term = (char *)c->previous.start + c->previous.length;
  char prev_char = *term;

  errno = 0;
  char *parse_end;
  *term = '\0'; // temp modify
  double value = strtod(c->previous.start, &parse_end);
  *term = prev_char; // restore

  assert(parse_end == term); // must parse the whole string
  if (errno != ERANGE) {
    emit_constant(c, CLOX_NUMBER(value), &c->previous);
  } else {
    error(c, &c->previous, "number out of range");
  }
}

static void literal(clox_compiler_t *c) {
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

static const clox_parse_rule_t parse_rules[] = {
#define X(name, prefix_fn, infix_fn, prec) [TOKEN_##name] = {prefix_fn, infix_fn, prec},
#include "tokens.def"
#undef X
};

_Static_assert(CLOX_ARRAY_SIZE(parse_rules) == TOKEN_TYPE_COUNT, "parse rules array size mismatch");

static clox_parse_rule_t get_rule(clox_token_type_t type) {
  assert(type < TOKEN_TYPE_COUNT);
  return parse_rules[type];
}

bool clox_compile(char *source, clox_chunk_t *chunk, clox_error_handler_t *error_handler,
                  void *error_ctx) {
  clox_scanner_t scanner;
  clox_init_scanner(&scanner, source);

  clox_compiler_t compiler = {
      .chunk = chunk,
      .scanner = &scanner,
      .had_error = false,
      .panic_mode = false,
      .error_handler = error_handler,
      .error_ctx = error_ctx,
      .parser_depth = 0,
  };

  advance(&compiler);    // scan the first token
  expression(&compiler); // compile single expression
  consume(&compiler, TOKEN_EOF, "expect end of expression");
  end_compiler(&compiler);

  assert(compiler.parser_depth == 0);

  return !compiler.had_error;
}

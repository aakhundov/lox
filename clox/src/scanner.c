#include "scanner.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "common.h"

const char *const clox_token_type_names[] = {
#define X(name, ...) [TOKEN_##name] = "TOKEN_" #name,
#include "tokens.def"
#undef X
};

_Static_assert(CLOX_ARRAY_SIZE(clox_token_type_names) == TOKEN_TYPE_COUNT,
               "token type names array size mismatch");

static inline bool is_digit(char c) {
  return c >= '0' && c <= '9';
}

static inline bool is_alpha(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c == '_');
}

static inline clox_token_type_t check_keyword(const clox_scanner_t *s, size_t prefix_len,
                                              size_t rest_len, const char *rest,
                                              clox_token_type_t type) {
  assert(type < TOKEN_TYPE_COUNT);
  // cast is safe by construction
  if ((size_t)(s->current - s->start) == prefix_len + rest_len && // total length match
      memcmp(s->start + prefix_len, rest, rest_len) == 0) {       // rest content match
    return type;
  }
  return TOKEN_IDENTIFIER;
}

static inline bool is_at_end(const clox_scanner_t *s) {
  return *s->current == '\0';
}

static inline char advance(clox_scanner_t *s) {
  s->current++;
  s->current_pos.col++;
  return s->current[-1];
}

static inline bool match(clox_scanner_t *s, char expected) {
  // never match the NUL
  if (is_at_end(s) || *s->current != expected) {
    return false;
  }
  advance(s);
  return true;
}

static inline char peek(const clox_scanner_t *s) {
  return *s->current;
}

static inline char peek_next(const clox_scanner_t *s) {
  if (is_at_end(s)) {
    return '\0';
  }
  return s->current[1];
}

static inline clox_token_t make_token(const clox_scanner_t *s, clox_token_type_t type) {
  assert(type < TOKEN_TYPE_COUNT);
  return (clox_token_t){
      .type = type,
      .start = s->start,
      // cast is safe by construction
      .length = (size_t)(s->current - s->start),
      .pos = s->start_pos,
  };
}

static inline clox_token_t make_error_token(const clox_scanner_t *s, const char *message) {
  return (clox_token_t){
      .type = TOKEN_ERROR,
      .start = message,
      .length = strlen(message),
      .pos = s->start_pos,
  };
}

static void skip_whitespace(clox_scanner_t *s) {
  while (1) {
    switch (peek(s)) {
    case ' ':
    case '\t':
    case '\v':
    case '\f':
    case '\r':
      advance(s);
      break;
    case '\n':
      s->current_pos.line++;
      s->current_pos.col = 0;
      advance(s);
      break;
    case '/':
      if (peek_next(s) == '/') {
        while (!is_at_end(s) && peek(s) != '\n') {
          advance(s);
        }
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

static clox_token_t string(clox_scanner_t *s) {
  char p = peek(s);
  while (!is_at_end(s) && p != '"') {
    if (p == '\n') {
      s->current_pos.line++;
      s->current_pos.col = 0;
    } else if (p == '\\') {
      advance(s); // eat the '\\'
      if (peek(s) == '\n' || is_at_end(s)) {
        return make_error_token(s, "incomplete escape sequence");
      }
    }
    advance(s);
    p = peek(s);
  }

  if (is_at_end(s)) {
    return make_error_token(s, "unterminated string");
  }

  advance(s); // eat trailing "
  return make_token(s, TOKEN_STRING);
}

static clox_token_t number(clox_scanner_t *s) {
  while (is_digit(peek(s))) {
    advance(s);
  }

  if (peek(s) == '.' && is_digit(peek_next(s))) {
    advance(s); // eat .
    while (is_digit(peek(s))) {
      advance(s);
    }
  }

  return make_token(s, TOKEN_NUMBER);
}

#define REST_ARGS(rest) sizeof(#rest) - 1, #rest

static clox_token_type_t identifier_type(const clox_scanner_t *s) {
  // NOLINTBEGIN(bugprone-switch-missing-default-case)
  switch (s->start[0]) {
  case 'a':
    return check_keyword(s, 1, REST_ARGS(nd), TOKEN_AND);
  case 'b':
    return check_keyword(s, 1, REST_ARGS(reak), TOKEN_BREAK);
  case 'c':
    if (s->current - s->start >= 2) {
      switch (s->start[1]) {
      case 'l':
        return check_keyword(s, 2, REST_ARGS(ass), TOKEN_CLASS);
      case 'o':
        return check_keyword(s, 2, REST_ARGS(ntinue), TOKEN_CONTINUE);
      }
    }
    break;
  case 'e':
    return check_keyword(s, 1, REST_ARGS(lse), TOKEN_ELSE);
  case 'f':
    if (s->current - s->start >= 2) {
      switch (s->start[1]) {
      case 'a':
        return check_keyword(s, 2, REST_ARGS(lse), TOKEN_FALSE);
      case 'o':
        return check_keyword(s, 2, REST_ARGS(r), TOKEN_FOR);
      case 'u':
        return check_keyword(s, 2, REST_ARGS(n), TOKEN_FUN);
      }
    }
    break;
  case 'i':
    return check_keyword(s, 1, REST_ARGS(f), TOKEN_IF);
  case 'n':
    return check_keyword(s, 1, REST_ARGS(il), TOKEN_NIL);
  case 'o':
    return check_keyword(s, 1, REST_ARGS(r), TOKEN_OR);
  case 'p':
    return check_keyword(s, 1, REST_ARGS(rint), TOKEN_PRINT);
  case 'r':
    return check_keyword(s, 1, REST_ARGS(eturn), TOKEN_RETURN);
  case 's':
    return check_keyword(s, 1, REST_ARGS(uper), TOKEN_SUPER);
  case 't':
    if (s->current - s->start >= 2) {
      switch (s->start[1]) {
      case 'h':
        return check_keyword(s, 2, REST_ARGS(is), TOKEN_THIS);
      case 'r':
        return check_keyword(s, 2, REST_ARGS(ue), TOKEN_TRUE);
      }
    }
    break;
  case 'v':
    return check_keyword(s, 1, REST_ARGS(ar), TOKEN_VAR);
  case 'w':
    return check_keyword(s, 1, REST_ARGS(hile), TOKEN_WHILE);
  }
  // NOLINTEND(bugprone-switch-missing-default-case)

  return TOKEN_IDENTIFIER;
}

static clox_token_t identifier(clox_scanner_t *s) {
  while (is_alpha(peek(s)) || is_digit(peek(s))) {
    advance(s);
  }
  return make_token(s, identifier_type(s));
}

void clox_scanner_init(clox_scanner_t *scanner, const char *source) {
  assert(scanner != NULL);
  assert(source != NULL);

  scanner->start = source;
  scanner->current = scanner->start;
  scanner->start_pos = (clox_pos_t){
      .line = 1,
      .col = 1,
  };
  scanner->current_pos = scanner->start_pos;
}

void clox_scanner_free(clox_scanner_t *scanner) {
  assert(scanner != NULL);

  scanner->start = NULL;
  scanner->current = NULL;
}

clox_token_t clox_scan(clox_scanner_t *scanner) {
  assert(scanner != NULL);

  skip_whitespace(scanner);

  scanner->start = scanner->current;
  scanner->start_pos = scanner->current_pos;

  if (is_at_end(scanner)) {
    return make_token(scanner, TOKEN_EOF);
  }

  char c = advance(scanner);

  if (is_alpha(c)) {
    return identifier(scanner);
  }
  if (is_digit(c)) {
    return number(scanner);
  }

  switch (c) {
  case '(':
    return make_token(scanner, TOKEN_LEFT_PAREN);
  case ')':
    return make_token(scanner, TOKEN_RIGHT_PAREN);
  case '{':
    return make_token(scanner, TOKEN_LEFT_BRACE);
  case '}':
    return make_token(scanner, TOKEN_RIGHT_BRACE);
  case ';':
    return make_token(scanner, TOKEN_SEMICOLON);
  case ',':
    return make_token(scanner, TOKEN_COMMA);
  case '.':
    return make_token(scanner, TOKEN_DOT);
  case '-':
    return make_token(scanner, TOKEN_MINUS);
  case '+':
    return make_token(scanner, TOKEN_PLUS);
  case '/':
    return make_token(scanner, TOKEN_SLASH);
  case '*':
    return make_token(scanner, TOKEN_STAR);
  case '!':
    return make_token(scanner, match(scanner, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
  case '=':
    return make_token(scanner, match(scanner, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
  case '<':
    return make_token(scanner, match(scanner, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
  case '>':
    return make_token(scanner, match(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
  case '"':
    return string(scanner);
  default:
    return make_error_token(scanner, "unexpected character");
  }
}

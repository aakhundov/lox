#ifndef CLOX_SCANNER_H
#define CLOX_SCANNER_H

#include <stddef.h>

#include "common.h"

typedef enum {
#define X(name) TOKEN_##name,
#include "tokens.def"
#undef X
  TOKEN_TYPE_COUNT,
} clox_token_type_t;

extern const char *const clox_token_type_names[];

typedef struct {
  clox_token_type_t type;
  const char *start;
  size_t length;
  clox_pos_t pos;
} clox_token_t;

typedef struct {
  const char *start;
  const char *current;
  clox_pos_t start_pos;
  clox_pos_t current_pos;
} clox_scanner_t;

void clox_init_scanner(clox_scanner_t *scanner, const char *source);
clox_token_t clox_scan_token(clox_scanner_t *scanner);

#endif

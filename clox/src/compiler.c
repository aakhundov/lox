#include "compiler.h"

#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "scanner.h"

#define POS_SIZE 11

clox_compile_result_t clox_compile(const char *source, clox_chunk_t *chunk) {
  (void)chunk; // not used yet

  clox_scanner_t scanner;
  clox_init_scanner(&scanner, source);

  while (1) {
    clox_token_t token = clox_scan_token(&scanner);

    char pos_str[POS_SIZE + 1];
    if (snprintf(pos_str, sizeof(pos_str), "%zu:%zu", token.pos.line, token.pos.col) > POS_SIZE) {
      // add trailing ellipsis ... on trimming
      memset(pos_str + (POS_SIZE - 3), '.', 3);
    }
    // for each valid token type, name is available by construction
    printf("%-7s %-20s ", pos_str, clox_token_type_names[token.type]);
    printf("[%.*s]\n", (int)token.length, token.start);

    if (token.type == TOKEN_EOF) {
      break;
    }
  }

  return (clox_compile_result_t){.status = CLOX_COMPILE_OK};
}

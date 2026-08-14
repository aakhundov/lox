#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <isocline.h>

#include "chunk.h"
#include "compiler.h"
#include "error.h"

#define ERROR_BUFFER_SIZE 1024
#define HISTORY_FILE ".clox.history"
#define HISTORY_PATH_SIZE 1024
#define HISTORY_ENTRIES 1000
#define PROMPT_MARKER ">>> "
#define CONTINUATION_MARKER "... "

static void print_error(const char *restrict fmt, ...) {
  char message[ERROR_BUFFER_SIZE];

  va_list ap;
  va_start(ap, fmt);
  (void)vsnprintf(message, ERROR_BUFFER_SIZE, fmt, ap);
  va_end(ap);

  if (isatty(fileno(stderr))) {
    (void)fprintf(stderr, "\033[31m%s\033[0m\n", message);
  } else {
    // no colors in non-tty output
    (void)fprintf(stderr, "%s\n", message);
  }
}

static void print_clox_error(clox_error_info_t e, const char *source) {
  (void)source; // not used yet
  print_error("Error [at %zu:%zu]: %s", e.pos.line, e.pos.col, e.message);
}

static int run_code(const char *source) {
  int ret = EX_OK;

  clox_chunk_t chunk;
  clox_init_chunk(&chunk);

  clox_compile_result_t result = clox_compile(source, &chunk);
  if (result.status != CLOX_COMPILE_OK) {
    print_clox_error(result.error, source);
    ret = EX_DATAERR;
    goto out;
  }

out:
  clox_free_chunk(&chunk);
  return ret;
}

// Decides when the editor should submit rather than open another line. A
// lone enter on an empty prompt starts a multi-line block, which then runs
// on a blank line: two newlines in a row. Single-line input is unaffected.
static bool input_is_complete(const char *input, void *ctx) {
  (void)ctx; // no context argument needed

  size_t len = strlen(input);
  if (len == 0) {
    return false; // empty prompt: open a multi-line block
  }
  if (strchr(input, '\n') == NULL) {
    return true; // still on the first line: enter submits
  }
  return len >= 2 && input[len - 1] == '\n' && input[len - 2] == '\n';
}

static void setup_ic_history(void) {
  const char *home = getenv("HOME");
  if (home == NULL) {
    ic_set_history(NULL, -1); // no home: keep history for this session only
    return;
  }

  char path[HISTORY_PATH_SIZE];
  int written = snprintf(path, sizeof(path), "%s/%s", home, HISTORY_FILE);
  if (written < 0 || (size_t)written >= sizeof(path)) {
    ic_set_history(NULL, -1); // path too long: same fallback
    return;
  }

  ic_set_history(path, HISTORY_ENTRIES);
}

static void run_repl(void) {
  ic_set_prompt_marker(PROMPT_MARKER, CONTINUATION_MARKER);
  ic_enable_multiline(true); // the completeness hook is ignored without it
  ic_set_is_complete(input_is_complete, NULL);
  setup_ic_history();

  while (1) {
    char *text = ic_readline(NULL); // alloc
    if (text == NULL) {
      break; // Ctrl-D, Ctrl-C or read error: exit REPL
    }

    // drop the blank lines that opened and terminated a multi-line block,
    // so the first line the user typed is line 1 of the program
    char *source = text;
    while (*source == '\n') {
      source++;
    }
    size_t len = strlen(source);
    while (len > 0 && source[len - 1] == '\n') {
      source[--len] = '\0';
    }

    run_code(source);
    ic_free(text); // free
  }
}

// ftell returns long, need one more byte for NUL in file buffer
_Static_assert(LONG_MAX < SIZE_MAX, "long + 1 doesn't fit into size_t");

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    print_error("Error opening file: %s", path);
    exit(EX_IOERR);
  }

  (void)fseek(file, 0L, SEEK_END);
  long ret_size = ftell(file);
  if (ret_size < 0) {
    print_error("Error reading file size: %s", path);
    exit(EX_IOERR);
  }
  size_t file_size = (size_t)ret_size;
  (void)fseek(file, 0L, SEEK_SET); // rewind

  // allocate size for file plus NUL
  char *buffer = malloc(file_size + 1);
  if (buffer == NULL) {
    print_error("Not enough memory to read file: %s", path);
    exit(EX_IOERR);
  }
  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
  if (bytes_read < file_size) {
    print_error("Error reading file: %s", path);
    free(buffer);
    exit(EX_IOERR);
  }
  // the write is safe: buffer is file_size + 1 bytes large
  buffer[bytes_read] = '\0'; // NOLINT(clang-analyzer-security.ArrayBound)

  (void)fclose(file);
  return buffer;
}

static void run_file(const char *path) {
  char *source = read_file(path); // alloc
  int result = run_code(source);
  free(source); // free

  if (result != EX_OK) {
    exit(result);
  }
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    run_repl();
  } else if (argc == 2) {
    run_file(argv[1]);
  } else {
    (void)fprintf(stderr, "Usage: clox [path]\n");
    exit(EX_USAGE);
  }

  return EX_OK;
}

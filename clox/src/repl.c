#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <isocline.h>

#include "compiler.h"
#include "error.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

#define MAX_ERROR_MSG_LEN 1024
#define HISTORY_FILE ".clox.history"
#define HISTORY_PATH_SIZE 1024
#define PROMPT_MARKER ">>> "
#define CONTINUATION_MARKER "... "
#define REPL_QUIT_COMMAND "q"

#define DASHES /* 256 dashes */                                                                    \
  "----------------------------------------------------------------"                               \
  "----------------------------------------------------------------"                               \
  "----------------------------------------------------------------"                               \
  "----------------------------------------------------------------"

typedef struct {
  const char *domain;
  char *source;
} clox_error_ctx_t;

typedef struct {
  clox_allocator_t allocator;
  clox_compiler_t compiler;
  clox_vm_t vm;
} clox_harness_t;

__attribute__((format(printf, 1, 2))) static int print_error(const char *restrict fmt, ...) {
  char message[MAX_ERROR_MSG_LEN + 1];

  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(message, sizeof(message), fmt, ap);
  if (len > MAX_ERROR_MSG_LEN) {
    // truncate incomplete error message with with ...
    memset(message + (MAX_ERROR_MSG_LEN - 3), '.', 3);
    len = MAX_ERROR_MSG_LEN;
  }
  va_end(ap);

  int result;
  if (isatty(fileno(stderr))) {
    result = fprintf(stderr, "\033[31m%s\033[0m\n", message);
  } else {
    // no colors in non-tty output
    result = fprintf(stderr, "%s\n", message);
  }

  return (result >= 0) ? len : -1;
}

static void print_clox_error(clox_error_info_t e, void *ctx) {
  clox_error_ctx_t *error_ctx = ctx;
  assert(e.pos.line > 0);
  assert(e.pos.col > 0);

  int printed =
      print_error("%s error [at %zu:%zu]: %s", error_ctx->domain, e.pos.line, e.pos.col, e.message);

  if (printed <= 0) {
    return;
  }

  // pos line / col are 1-based
  size_t line = e.pos.line - 1;
  size_t col = e.pos.col - 1;

  // assume source is NUL-terminated
  char *line_start = error_ctx->source;
  size_t current_line = 0;
  while (current_line < line) {
    if (*line_start++ == '\n') {
      current_line++;
    }
  }
  char *line_end = line_start;
  while (*line_end != '\n' && *line_end != '\0') {
    line_end++;
  }
  char prev_line_end = *line_end;

  print_error("%.*s", printed, DASHES); // underline
  *line_end = '\0';                     // temp modify
  print_error("%s", line_start);        // error line
  *line_end = prev_line_end;            // restore
  print_error("%*s^", (int)col, "");    // spaces + caret
}

static void clox_harness_setup(clox_harness_t *harness) {
  clox_allocator_init(&harness->allocator);
  clox_compiler_init(&harness->compiler, &harness->allocator);
  clox_vm_init(&harness->vm, &harness->allocator);
}

static void clox_harness_teardown(clox_harness_t *harness) {
  clox_vm_free(&harness->vm);
  clox_compiler_free(&harness->compiler);
  clox_allocator_free(&harness->allocator);
}

static clox_exit_code_t run_code(clox_harness_t *h, char *source) {
  clox_exit_code_t ret = CLOX_EX_OK;

  clox_function_t *script;

  clox_error_ctx_t compile_ctx = {.domain = "Compilation", .source = source};
  clox_compiler_set_error_handler(&h->compiler, print_clox_error, &compile_ctx);
  if (!clox_compile(&h->compiler, source, &script)) {
    ret = CLOX_EX_DATAERR;
    goto out;
  }

  clox_error_ctx_t interpret_ctx = {.domain = "Runtime", .source = source};
  clox_vm_set_error_handler(&h->vm, print_clox_error, &interpret_ctx);
  if (!clox_interpret(&h->vm, script)) {
    ret = CLOX_EX_SOFTWARE;
    goto out;
  }

out:
  clox_compiler_reset_error_handler(&h->compiler);
  clox_vm_reset_error_handler(&h->vm);
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

  ic_set_history(path, -1);
}

static void handle_repl_command(clox_harness_t *h, const char *command) {
  if (strcmp(command, "env") == 0) {
    const clox_table_entry_t *running = NULL;
    while ((running = clox_table_next(&h->vm.globals, running))) {
      printf("%s = [", CLOX_AS_CSTRING(CLOX_OBJECT(running->key)));
      clox_value_repr_printf(running->value);
      printf("]\n");
    }
  } else {
    print_error("Unrecognized command: \"%s\"", command);
  }
}

static void run_repl(void) {
  ic_set_prompt_marker(PROMPT_MARKER, CONTINUATION_MARKER);
  ic_enable_multiline(true); // the completeness hook is ignored without it
  ic_set_is_complete(input_is_complete, NULL);
  ic_enable_history_short_entries(true);
  setup_ic_history();

  clox_harness_t harness;
  clox_harness_setup(&harness);

  while (1) {
    char *text = ic_readline(NULL); // alloc
    if (text == NULL) {
      break; // Ctrl-D, Ctrl-C or read error: exit REPL
    }

    // drop the blank line that opened and lines that terminated
    // a multi-line block, so the first line after multi-line break
    // is line 1 of the program
    char *source = text;
    if (*source == '\n') {
      source++;
    }
    size_t len = strlen(source);
    while (len > 0 && source[len - 1] == '\n') {
      source[--len] = '\0';
    }

    // leading whitespace makes it a non-command
    if (len > 0 && source[0] == ':') {
      // commands are not saved in history
      ic_history_remove_last();
      ic_history_save();

      // bare : is silently ignored
      if (len > 1) {
        char *command = source + 1; // skip :
        if (strcmp(command, REPL_QUIT_COMMAND) == 0) {
          ic_free(text); // free before break
          break;
        }
        handle_repl_command(&harness, command);
      }
    } else {
      run_code(&harness, source);
    }

    ic_free(text); // free
  }

  clox_harness_teardown(&harness);
}

// ftell returns long, need one more byte for NUL in file buffer
_Static_assert(LONG_MAX < SIZE_MAX, "long + 1 doesn't fit into size_t");

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    print_error("Error opening file: %s", path);
    exit(CLOX_EX_NOINPUT);
  }

  (void)fseek(file, 0L, SEEK_END);
  long ret_size = ftell(file);
  if (ret_size < 0) {
    print_error("Error reading file size: %s", path);
    exit(CLOX_EX_IOERR);
  }
  size_t file_size = (size_t)ret_size;
  (void)fseek(file, 0L, SEEK_SET); // rewind

  // allocate size for file plus NUL
  char *buffer = malloc(file_size + 1);
  if (buffer == NULL) {
    print_error("Not enough memory to read file: %s", path);
    exit(CLOX_EX_OSERR);
  }
  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
  if (bytes_read < file_size) {
    print_error("Error reading file: %s", path);
    free(buffer);
    exit(CLOX_EX_IOERR);
  }
  // the write is safe: buffer is file_size + 1 bytes large
  buffer[bytes_read] = '\0'; // NOLINT(clang-analyzer-security.ArrayBound)

  (void)fclose(file);
  return buffer;
}

static void run_file(const char *path) {
  char *source = read_file(path); // alloc

  clox_harness_t harness;
  clox_harness_setup(&harness);
  clox_exit_code_t result = run_code(&harness, source);
  clox_harness_teardown(&harness);

  free(source); // free

  if (result != CLOX_EX_OK) {
    exit((int)result);
  }
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    run_repl();
  } else if (argc == 2) {
    run_file(argv[1]);
  } else {
    (void)fprintf(stderr, "Usage: clox [path]\n");
    exit(CLOX_EX_USAGE);
  }

  return CLOX_EX_OK;
}

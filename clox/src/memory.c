#include "memory.h"

#include <stdlib.h>

#include "error.h"

void *clox_reallocate(void *pointer, size_t old_bytes, size_t new_bytes) {
  (void)old_bytes; // not used yet

  if (new_bytes == 0) {
    free(pointer);
    return NULL;
  }

  void *result = realloc(pointer, new_bytes);
  if (result == NULL) {
    CLOX_FATAL_ERROR("memory allocation failed", CLOX_EX_OSERR);
  }
  return result;
}

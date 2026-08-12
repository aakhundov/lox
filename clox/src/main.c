#include <assert.h>

#include "chunk.h"
#include "vm.h"

int main(void) {
  clox_vm_t vm;
  clox_chunk_t chunk;

  clox_init_vm(&vm);
  clox_init_chunk(&chunk);

  // NOLINTBEGIN(readability-magic-numbers)
  clox_write_constant(&chunk, 1.2, (clox_pos_t){1, 2});
  clox_write_constant(&chunk, 3.4, (clox_pos_t){1, 5});
  clox_write_chunk(&chunk, OP_ADD, (clox_pos_t){1, 3});
  clox_write_constant(&chunk, 5.6, (clox_pos_t){2, 3});
  clox_write_chunk(&chunk, OP_DIVIDE, (clox_pos_t){2, 6});
  clox_write_chunk(&chunk, OP_NEGATE, (clox_pos_t){3, 1});
  clox_write_chunk(&chunk, OP_RETURN, (clox_pos_t){3, 5});
  // NOLINTEND(readability-magic-numbers)

  assert(clox_interpret(&vm, &chunk) == CLOX_INTERPRET_OK);

  clox_free_chunk(&chunk);
  clox_free_vm(&vm);

  return 0;
}

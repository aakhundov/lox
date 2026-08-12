#include <stddef.h>

#include "chunk.h"
#include "debug.h"

int main(void) {
  clox_chunk_t chunk;

  clox_init_chunk(&chunk);

  // NOLINTBEGIN(readability-magic-numbers)
  clox_write_constant(&chunk, 3.14, (clox_pos_t){1, 2}); // short
  // fill the constants to exceed short
  for (size_t i = 0; i < 300; i++) {
    clox_add_constant(&chunk, 1.0);
  }
  clox_write_constant(&chunk, 2.17, (clox_pos_t){2, 3}); // long
  clox_write_chunk(&chunk, OP_RETURN, (clox_pos_t){2, 5});
  // NOLINTEND(readability-magic-numbers)

  clox_disassemble_chunk(&chunk, "test chunk");

  clox_free_chunk(&chunk);

  return 0;
}

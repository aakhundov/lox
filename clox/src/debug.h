#ifndef CLOX_DEBUG_H
#define CLOX_DEBUG_H

#include <stddef.h>

#include "chunk.h"

#ifndef CLOX_DEBUG_TRACE
#define CLOX_DEBUG_TRACE 1
#endif

void clox_disassemble_chunk(const clox_chunk_t *chunk, const char *name);
size_t clox_disassemble_instruction(const clox_chunk_t *chunk, size_t offset);

#endif

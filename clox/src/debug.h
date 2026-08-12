#ifndef CLOX_DEBUG_H
#define CLOX_DEBUG_H

#include "chunk.h"

void clox_disassemble_chunk(const clox_chunk_t *chunk, const char *name);

#endif

#ifndef CLOX_DEBUG_H
#define CLOX_DEBUG_H

#include <stddef.h>
#include <stdio.h>

#include "chunk.h"

#ifndef CLOX_DEBUG_COMPILATION
#define CLOX_DEBUG_COMPILATION 0
#endif
#ifndef CLOX_DEBUG_EXECUTION
#define CLOX_DEBUG_EXECUTION 0
#endif
#ifndef CLOX_DEBUG_ALLOCATION
#define CLOX_DEBUG_ALLOCATION 0
#endif
#ifndef CLOX_DEBUG_MEMORY
#define CLOX_DEBUG_MEMORY 0
#endif
#ifndef CLOX_DEBUG_GC
#define CLOX_DEBUG_GC 0
#endif
#ifndef CLOX_STRESS_GC
#define CLOX_STRESS_GC 1
#endif

void clox_disassemble_chunk_fprintf(FILE *stream, const clox_chunk_t *chunk, const char *name);
void clox_disassemble_chunk(const clox_chunk_t *chunk, const char *name);
size_t clox_disassemble_instruction_fprintf(FILE *stream, const clox_chunk_t *chunk, size_t offset);
size_t clox_disassemble_instruction(const clox_chunk_t *chunk, size_t offset);

#endif

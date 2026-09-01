#include "debug.h"

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "common.h"
#include "object.h"
#include "value.h"

#define PAD_SIZE 40
#define POS_SIZE 11

static size_t const_instruction(FILE *stream, const clox_chunk_t *chunk, clox_op_code_t opcode,
                                clox_value_t *value, size_t offset) {
  const clox_byte_t *ip = chunk->code + offset + 1; // skip the opcode
  clox_value_t val = clox_read_constant(chunk, opcode, &ip);
  assert(ip > chunk->code + offset + 1); // ip has moved

  clox_value_repr_fprintf(stream, val);

  if (value != NULL) {
    *value = val;
  }

  // cast is safe: assert above
  return (size_t)(ip - chunk->code);
}

static size_t byte_instruction(FILE *stream, const clox_chunk_t *chunk, size_t offset) {
  clox_byte_t byte = chunk->code[offset + 1];
  (void)fprintf(stream, "0x%02x", byte);
  return offset + 2; // opcode + byte
}

static size_t jump_instruction(FILE *stream, const clox_chunk_t *chunk, int sign, size_t offset) {
  assert(sign == 1 || sign == -1);

  size_t jump = ((size_t)chunk->code[offset + 1] << CHAR_BIT);
  jump |= chunk->code[offset + 2];

  ptrdiff_t destination = (ptrdiff_t)offset + 3 + (sign * (ptrdiff_t)jump);
  (void)fprintf(stream, "%04td", destination);

  return offset + 3; // opcode + 2 bytes
}

static size_t append_upvalues(FILE *stream, const clox_chunk_t *chunk, size_t count,
                              size_t offset) {
  for (size_t i = 0; i < count; i++) {
    bool is_local = (chunk->code[offset++] == 1);
    size_t index = chunk->code[offset++];
    (void)fprintf(stream, " %c%zu", is_local ? 'L' : 'U', index);
  }

  return offset; // incremented above
}

size_t clox_disassemble_instruction_fprintf(FILE *stream, const clox_chunk_t *chunk,
                                            size_t offset) {
  assert(stream != NULL);
  assert(chunk != NULL);

  assert(offset < chunk->length);
  clox_byte_t byte = chunk->code[offset];
  clox_pos_t pos = chunk->positions[offset];

  char pos_str[POS_SIZE + 1];
  if (snprintf(pos_str, sizeof(pos_str), "%zu:%zu", pos.line, pos.col) > POS_SIZE) {
    // add trailing ellipsis ... on trimming
    memset(pos_str + (POS_SIZE - 3), '.', 3);
  }
  (void)fprintf(stream, "%04zu %-7s ", offset, pos_str);

  if (byte < OP_CODE_COUNT) {
    // safe to cast: range check above
    clox_op_code_t opcode = (clox_op_code_t)byte;
    // legal opcode: name is available by construction
    (void)fprintf(stream, "%-18s ", clox_op_code_names[opcode]);

    switch (opcode) {
    case OP_NIL:
    case OP_TRUE:
    case OP_FALSE:
    case OP_POP:
    case OP_CLOSE_UPVALUE:
    case OP_EQUAL:
    case OP_NOT_EQUAL:
    case OP_GREATER:
    case OP_GREATER_EQUAL:
    case OP_LESS:
    case OP_LESS_EQUAL:
    case OP_ADD:
    case OP_SUBTRACT:
    case OP_MULTIPLY:
    case OP_DIVIDE:
    case OP_NOT:
    case OP_NEGATE:
    case OP_PRINT:
    case OP_RETURN:
      offset++; // just opcode
      break;
    case OP_CONSTANT:
    case OP_CONSTANT_LONG:
    case OP_DEF_GLOBAL:
    case OP_DEF_GLOBAL_LONG:
    case OP_GET_GLOBAL:
    case OP_GET_GLOBAL_LONG:
    case OP_SET_GLOBAL:
    case OP_SET_GLOBAL_LONG:
      offset = const_instruction(stream, chunk, opcode, NULL, offset);
      break;
    case OP_CLOSURE:
    case OP_CLOSURE_LONG: {
      clox_value_t val;
      offset = const_instruction(stream, chunk, opcode, &val, offset);
      size_t count = CLOX_AS_FUNCTION(val)->upvalue_count;
      offset = append_upvalues(stream, chunk, count, offset);
      break;
    }
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_GET_UPVALUE:
    case OP_SET_UPVALUE:
    case OP_POP_N:
    case OP_PRINT_N:
    case OP_CALL:
      offset = byte_instruction(stream, chunk, offset);
      break;
    case OP_JUMP_TRUE:
    case OP_JUMP_FALSE:
    case OP_JUMP_FALSE_POP:
    case OP_JUMP:
      offset = jump_instruction(stream, chunk, +1, offset);
      break;
    case OP_LOOP:
      offset = jump_instruction(stream, chunk, -1, offset);
      break;
    case OP_CODE_COUNT:
      assert(0 && "unreachable");
    }
  } else {
    (void)fprintf(stream, "Unknown opcode: 0x%02x", byte);
    offset++; // skip this byte
  }

  (void)fprintf(stream, "\n");
  return offset;
}

size_t clox_disassemble_instruction(const clox_chunk_t *chunk, size_t offset) {
  assert(chunk != NULL);

  return clox_disassemble_instruction_fprintf(stdout, chunk, offset);
}

void clox_disassemble_chunk_fprintf(FILE *stream, const clox_chunk_t *chunk, const char *name) {
  assert(stream != NULL);
  assert(chunk != NULL);
  assert(name != NULL);

  char pad[PAD_SIZE + 1];
  size_t name_len = strlen(name) + 2; // two spaces on the sides
  size_t pad_len = (name_len > PAD_SIZE ? 0 : PAD_SIZE - name_len) / 2;
  memset(pad, '-', pad_len);
  pad[pad_len] = '\0';

  int printed = fprintf(stream, "%s %s %s\n", pad, name, pad);

  for (size_t offset = 0; offset < chunk->length;) {
    // offset is incremented by the call below
    offset = clox_disassemble_instruction_fprintf(stream, chunk, offset);
  }

  // skip the new line char from the printed char count
  size_t bottom_pad_len = printed > 0 ? (size_t)(printed - 1) : 0;
  if (bottom_pad_len > PAD_SIZE) {
    bottom_pad_len = PAD_SIZE;
  }
  memset(pad, '-', bottom_pad_len);
  pad[bottom_pad_len] = '\0';

  (void)fprintf(stream, "%s\n", pad);
}

void clox_disassemble_chunk(const clox_chunk_t *chunk, const char *name) {
  assert(chunk != NULL);
  assert(name != NULL);

  clox_disassemble_chunk_fprintf(stdout, chunk, name);
}

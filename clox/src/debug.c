#include "debug.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "chunk.h"
#include "value.h"

#define PAD_SIZE 40
#define POS_SIZE 11

static size_t const_instruction(const clox_chunk_t *chunk, clox_op_code_t opcode, size_t offset) {
  const clox_byte_t *ip = chunk->code + offset + 1; // skip the opcode
  clox_value_t value = clox_read_constant(chunk, opcode, &ip);
  assert(ip > chunk->code + offset + 1); // ip has moved

  printf("[");
  clox_print_value(value);
  printf("]");

  // cast is safe: assert above
  return (size_t)(ip - chunk->code);
}

size_t clox_disassemble_instruction(const clox_chunk_t *chunk, size_t offset) {
  assert(offset < chunk->length);
  clox_byte_t byte = chunk->code[offset];
  clox_pos_t pos = chunk->positions[offset];

  char pos_str[POS_SIZE + 1];
  if (snprintf(pos_str, sizeof(pos_str), "%u:%u", pos.line, pos.column) > POS_SIZE) {
    // add trailing ellipsis ... on trimming
    memset(pos_str + (POS_SIZE - 3), '.', 3);
  }
  printf("%04zu %-7s ", offset, pos_str);

  if (byte < OP_CODE_COUNT) {
    // safe to cast: range check above
    clox_op_code_t opcode = (clox_op_code_t)byte;
    // legal opcode: name is available by construction
    printf("%-18s ", clox_op_code_names[opcode]);

    switch (opcode) {
    case OP_ADD:
    case OP_SUBTRACT:
    case OP_MULTIPLY:
    case OP_DIVIDE:
    case OP_NEGATE:
    case OP_RETURN:
      offset++; // just opcode
      break;
    case OP_CONSTANT:
    case OP_CONSTANT_LONG:
      offset = const_instruction(chunk, opcode, offset);
      break;
    case OP_CODE_COUNT:
      assert(0 && "unreachable");
    }
  } else {
    printf("Unknown opcode: %#04x", byte);
    offset++; // skip this byte
  }

  printf("\n");
  return offset;
}

void clox_disassemble_chunk(const clox_chunk_t *chunk, const char *name) {
  char pad[PAD_SIZE + 1];
  size_t name_len = strlen(name) + 2; // two spaces on the sides
  size_t pad_len = (name_len > PAD_SIZE ? 0 : PAD_SIZE - name_len) / 2;
  memset(pad, '=', pad_len);
  pad[pad_len] = '\0';

  int printed = printf("%s %s %s\n", pad, name, pad);

  for (size_t offset = 0; offset < chunk->length;) {
    // offset is incremented by the call below
    offset = clox_disassemble_instruction(chunk, offset);
  }

  // skip the new line char from the printed char count
  size_t bottom_pad_len = printed > 0 ? (size_t)(printed - 1) : 0;
  if (bottom_pad_len > PAD_SIZE) {
    bottom_pad_len = PAD_SIZE;
  }
  memset(pad, '=', bottom_pad_len);
  pad[bottom_pad_len] = '\0';

  printf("%s\n", pad);
}

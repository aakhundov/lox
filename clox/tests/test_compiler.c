#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <utest.h>

#include "chunk.h"
#include "compiler.h"
#include "memory.h"
#include "object.h"
#include "value.h"

#include "support/harness.h"

// The name a compilation is opened under. It is stamped on every function the
// compiler builds and reported alongside a position, and never opened as a file.
#define SOURCE_NAME "test.lox"
// room for a declaration per local slot, several times over
#define SOURCE_SIZE 8192
// more digits than a double can hold, and fewer than the source buffer
#define OVERSIZED_DIGITS 400
// The first slot of a frame goes to the function being compiled, so a
// declaration can reach every slot but that one. What is left is exactly what
// one byte counts, which is what the test below rests on: should the slots
// ever outgrow that, a scope's pop needs more than one counted group again.
#define LOCAL_SLOTS (CLOX_MAX_LOCALS - 1)
_Static_assert(LOCAL_SLOTS == UCHAR_MAX, "a full scope's pop no longer fits one byte");
// one more local than there are slots to hold them
#define OVER_LOCAL_SLOTS (LOCAL_SLOTS + 1)
// A capture index is a byte like a slot index, but no slot is reserved among
// them: every one of them can be reached.
#define UPVALUE_SLOTS CLOX_MAX_UPVALUES
_Static_assert(UPVALUE_SLOTS == UCHAR_MAX + 1, "a capture index no longer fits one byte");
// one more capture than a closure has room for
#define OVER_UPVALUE_SLOTS (UPVALUE_SLOTS + 1)
// A frame declaring a function beside its locals spends a slot on that
// function too, so this is as many locals as it can leave for it to capture.
#define CAPTURABLE_LOCALS (LOCAL_SLOTS - 1)
// the widest offset two bytes can carry
#define TWO_BYTE_MAX ((UCHAR_MAX << CHAR_BIT) | UCHAR_MAX)
// the statement long bodies are built from, two bytes of code each
#define FILLER_STATEMENT "true;"
#define FILLER_STATEMENT_BYTES 2
// statements enough for a body outgrowing a one-byte jump offset
#define OVER_BYTE_JUMP ((UCHAR_MAX / FILLER_STATEMENT_BYTES) + 1)
// statements enough for a body outgrowing a two-byte jump offset
#define OVER_TWO_BYTE_JUMP ((TWO_BYTE_MAX / FILLER_STATEMENT_BYTES) + 1)
// one more value than a one-byte print operand can count
#define OVER_PRINT_ARGS (UCHAR_MAX + 1)
// one more constant than a single-byte index can address
#define OVER_BYTE_INDEX (UCHAR_MAX + 1)
// Mirrors MAX_DECLARATION_DEPTH in compiler.c, which is private to it: the
// depth at which a declaration is refused. One level short of it must compile.
#define DECLARATION_DEPTH_LIMIT 200

// Compares the whole emitted code array, so a stray or missing byte shows up.
#define EXPECT_CODE(chunk, ...)                                                                    \
  do {                                                                                             \
    const clox_byte_t clox_test_expected[] = {__VA_ARGS__};                                        \
    const size_t clox_test_count = sizeof(clox_test_expected) / sizeof(*clox_test_expected);       \
    ASSERT_EQ(clox_test_count, (chunk)->length);                                                   \
    for (size_t i = 0; i < clox_test_count; i++) {                                                 \
      ASSERT_EQ(clox_test_expected[i], (chunk)->code[i]);                                          \
    }                                                                                              \
  } while (0)

struct compiler {
  clox_allocator_t alloc;
  clox_compiler_t compiler;
  clox_function_t *function;
  clox_test_errors_t errors;
  char source[SOURCE_SIZE]; // clox_compile needs a buffer it may modify
};

UTEST_F_SETUP(compiler) {
  clox_allocator_init(&utest_fixture->alloc);
  clox_compiler_init(&utest_fixture->compiler, &utest_fixture->alloc);
  utest_fixture->function = NULL;
  utest_fixture->errors = (clox_test_errors_t){0};
  clox_compiler_set_error_handler(&utest_fixture->compiler, clox_test_error_handler,
                                  &utest_fixture->errors);
}

UTEST_F_TEARDOWN(compiler) {
  clox_compiler_reset_error_handler(&utest_fixture->compiler);
  clox_compiler_free(&utest_fixture->compiler);
  clox_allocator_free(&utest_fixture->alloc);
}

static bool compile(struct compiler *fixture, const char *source) {
  (void)snprintf(fixture->source, SOURCE_SIZE, "%s", source);

  return clox_compile(&fixture->compiler, SOURCE_NAME, fixture->source, &fixture->function);
}

// Renders a block declaring count locals -- "{var v0;var v1;...}" -- into
// buffer. SOURCE_SIZE holds several times the longest block a caller can ask
// for, so nothing here truncates.
static const char *block_of_locals(char (*buffer)[SOURCE_SIZE], size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "{");
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "var v%zu;", i);
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, "}");

  return *buffer;
}

// Renders head followed by a block of count filler statements -- "if (false)
// {true;true;...}" -- into buffer, for the tests that need a body of a known
// size in bytes. The caller keeps count small enough for SOURCE_SIZE.
static const char *long_body(char (*buffer)[SOURCE_SIZE], const char *head, size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "%s{", head);
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, FILLER_STATEMENT);
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, "}");

  return *buffer;
}

// The same body on the heap, for the counts that outgrow SOURCE_SIZE several
// times over. Returns NULL if the text cannot be allocated; the caller owns
// what it gets back, and clox_compile may modify it in place.
static char *long_body_alloc(const char *head, size_t count) {
  size_t size = strlen(head) + (count * strlen(FILLER_STATEMENT)) + 3; // "{", "}", NUL
  char *source = malloc(size);
  if (source == NULL) {
    return NULL;
  }

  size_t written = (size_t)snprintf(source, size, "%s{", head);
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(source + written, size - written, FILLER_STATEMENT);
  }
  (void)snprintf(source + written, size - written, "}");

  return source;
}

// Renders "print 1, 1, ...;" over count values into buffer.
static const char *print_of(char (*buffer)[SOURCE_SIZE], size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "print ");
  for (size_t i = 0; i < count; i++) {
    written +=
        (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "%s1", (i > 0) ? ", " : "");
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, ";");

  return *buffer;
}

// Renders "0;1;...;" over OVER_BYTE_INDEX distinct number literals, then the
// statement handed in. Numbers are not interned the way names are, so each one
// takes a constant slot of its own and what follows is named by an index no
// single byte can carry.
static const char *past_a_byte_of_constants(char (*buffer)[SOURCE_SIZE], const char *statement) {
  size_t written = 0;
  for (size_t i = 0; i < OVER_BYTE_INDEX; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "%zu;", i);
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, "%s", statement);

  return *buffer;
}

// Renders "fun f(p0,p1,...){}" over count parameters into buffer.
static const char *function_of_params(char (*buffer)[SOURCE_SIZE], size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "fun f(");
  for (size_t i = 0; i < count; i++) {
    written +=
        (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "%sp%zu", (i > 0) ? "," : "", i);
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, "){}");

  return *buffer;
}

// Renders "f(1,1,...);" over count arguments into buffer.
static const char *call_of_args(char (*buffer)[SOURCE_SIZE], size_t count) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "f(");
  for (size_t i = 0; i < count; i++) {
    written +=
        (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "%s1", (i > 0) ? "," : "");
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, ");");

  return *buffer;
}

// Renders count nested function declarations -- "fun f0(){fun f1(){...}}" --
// into buffer, for the tests measuring how deep declarations may nest.
static const char *nested_functions(char (*buffer)[SOURCE_SIZE], size_t count) {
  size_t written = 0;
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "fun f%zu(){", i);
  }
  for (size_t i = 0; i < count; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "}");
  }

  return *buffer;
}

// Renders three nested functions, the outer two declaring locals of their own
// and the innermost naming every one of them:
//
//   fun f0(){var a0;...fun f1(){var b0;...fun f2(){a0;...b0;...}}}
//
// The innermost frame therefore needs outer + middle captures, and the middle
// one needs outer of them to pass through. Neither count may pass what a frame
// has slots for; the sum is what the captures are measured against.
static const char *functions_capturing(char (*buffer)[SOURCE_SIZE], size_t outer, size_t middle) {
  size_t written = (size_t)snprintf(*buffer, SOURCE_SIZE, "fun f0(){");
  for (size_t i = 0; i < outer; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "var a%zu;", i);
  }
  written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "fun f1(){");
  for (size_t i = 0; i < middle; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "var b%zu;", i);
  }
  written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "fun f2(){");
  for (size_t i = 0; i < outer; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "a%zu;", i);
  }
  for (size_t i = 0; i < middle; i++) {
    written += (size_t)snprintf(*buffer + written, SOURCE_SIZE - written, "b%zu;", i);
  }
  (void)snprintf(*buffer + written, SOURCE_SIZE - written, "}}}");

  return *buffer;
}

// The function object standing at index among the chunk's constants.
static clox_function_t *function_constant(const clox_chunk_t *chunk, size_t index) {
  if (index >= chunk->constants.length || !CLOX_IS_FUNCTION(chunk->constants.values[index])) {
    return NULL;
  }

  return CLOX_AS_FUNCTION(chunk->constants.values[index]);
}

// The two-byte big-endian jump operand standing at pos.
static size_t jump_offset(const clox_chunk_t *chunk, size_t pos) {
  return ((size_t)chunk->code[pos] << CHAR_BIT) | chunk->code[pos + 1];
}

UTEST_F(compiler, a_number_becomes_a_constant) {
  ASSERT_TRUE(compile(utest_fixture, "42;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN_NIL);
  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(42.0), utest_fixture->function->chunk.constants.values[0]);
}

UTEST_F(compiler, a_fractional_number_keeps_its_value) {
  ASSERT_TRUE(compile(utest_fixture, "1.5;"));

  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.5), utest_fixture->function->chunk.constants.values[0]);
}

UTEST_F(compiler, true_has_its_own_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "true;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, false_has_its_own_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "false;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_FALSE, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, nil_has_its_own_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "nil;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_NIL, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_string_becomes_a_constant_holding_its_text) {
  ASSERT_TRUE(compile(utest_fixture, "\"text\";"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN_NIL);
  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);

  clox_value_t constant = utest_fixture->function->chunk.constants.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(constant));
  EXPECT_STREQ("text", CLOX_AS_CSTRING(constant));
}

UTEST_F(compiler, an_empty_string_becomes_an_empty_constant) {
  ASSERT_TRUE(compile(utest_fixture, "\"\";"));

  clox_value_t constant = utest_fixture->function->chunk.constants.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(constant));
  EXPECT_EQ((size_t)0, CLOX_AS_STRING(constant)->length);
}

UTEST_F(compiler, an_escape_sequence_becomes_the_character_it_stands_for) {
  ASSERT_TRUE(compile(utest_fixture, "\"a\\nb\";"));

  clox_value_t constant = utest_fixture->function->chunk.constants.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(constant));
  EXPECT_EQ((size_t)3, CLOX_AS_STRING(constant)->length);
  EXPECT_STREQ("a\nb", CLOX_AS_CSTRING(constant));
}

UTEST_F(compiler, an_escaped_quote_and_an_escaped_backslash_are_one_character_each) {
  ASSERT_TRUE(compile(utest_fixture, "\"a\\\"b\\\\c\";"));

  clox_value_t constant = utest_fixture->function->chunk.constants.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(constant));
  EXPECT_EQ((size_t)5, CLOX_AS_STRING(constant)->length);
  EXPECT_STREQ("a\"b\\c", CLOX_AS_CSTRING(constant));
}

UTEST_F(compiler, a_string_of_nothing_but_escape_sequences_keeps_every_character) {
  ASSERT_TRUE(compile(utest_fixture, "\"\\n\\\\\\\"\";"));

  clox_value_t constant = utest_fixture->function->chunk.constants.values[0];
  ASSERT_TRUE(CLOX_IS_STRING(constant));
  EXPECT_EQ((size_t)3, CLOX_AS_STRING(constant)->length);
  EXPECT_STREQ("\n\\\"", CLOX_AS_CSTRING(constant));
}

UTEST_F(compiler, an_escape_sequence_and_the_character_it_stands_for_are_one_constant) {
  // the rewritten text is interned like any other, so the two spellings of the
  // same string meet in the string table instead of becoming two constants
  ASSERT_TRUE(compile(utest_fixture, "\"a\\nb\"; \"a\nb\";"));

  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
  EXPECT_STREQ("a\nb", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[0]));
}

UTEST_F(compiler, an_unsupported_escape_sequence_is_reported_at_its_string) {
  EXPECT_FALSE(compile(utest_fixture, "print \"a\\tb\";"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "escape sequence") != NULL);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)7, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, an_escape_sequence_broken_across_two_lines_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "\"a\\\nb\";"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "escape sequence") != NULL);
}

UTEST_F(compiler, a_backslash_with_nothing_after_it_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "\"a\\"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "escape sequence") != NULL);
}

UTEST_F(compiler, a_broken_escape_sequence_does_not_shift_the_lines_after_it) {
  EXPECT_FALSE(compile(utest_fixture, "\"a\\\nb\" \";\nvar 3;"));

  ASSERT_TRUE(utest_fixture->errors.count > 1);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[1][0].pos.line);
  EXPECT_EQ((size_t)5, utest_fixture->errors.stacks[1][0].pos.col);
}

UTEST_F(compiler, negation_follows_its_operand) {
  ASSERT_TRUE(compile(utest_fixture, "-1;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_NEGATE, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, not_follows_its_operand) {
  ASSERT_TRUE(compile(utest_fixture, "!true;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_NOT, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, unary_operators_stack_up) {
  ASSERT_TRUE(compile(utest_fixture, "--1;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_NEGATE, OP_NEGATE, OP_POP,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_binary_operator_follows_both_operands) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_POP,
              OP_RETURN_NIL);
  ASSERT_EQ((size_t)2, utest_fixture->function->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->function->chunk.constants.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->function->chunk.constants.values[1]);
}

UTEST_F(compiler, each_arithmetic_operator_has_its_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2;"));
  EXPECT_EQ(OP_ADD, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 - 2;"));
  EXPECT_EQ(OP_SUBTRACT, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 * 2;"));
  EXPECT_EQ(OP_MULTIPLY, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 / 2;"));
  EXPECT_EQ(OP_DIVIDE, utest_fixture->function->chunk.code[4]);
}

UTEST_F(compiler, each_comparison_operator_has_its_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "1 == 2;"));
  EXPECT_EQ(OP_EQUAL, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 != 2;"));
  EXPECT_EQ(OP_NOT_EQUAL, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 > 2;"));
  EXPECT_EQ(OP_GREATER, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 >= 2;"));
  EXPECT_EQ(OP_GREATER_EQUAL, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 < 2;"));
  EXPECT_EQ(OP_LESS, utest_fixture->function->chunk.code[4]);

  ASSERT_TRUE(compile(utest_fixture, "1 <= 2;"));
  EXPECT_EQ(OP_LESS_EQUAL, utest_fixture->function->chunk.code[4]);
}

UTEST_F(compiler, multiplication_binds_tighter_than_addition) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2 * 3;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
              OP_MULTIPLY, OP_ADD, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, grouping_overrides_precedence) {
  ASSERT_TRUE(compile(utest_fixture, "(1 + 2) * 3;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
              2, OP_MULTIPLY, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, equal_precedence_associates_to_the_left) {
  ASSERT_TRUE(compile(utest_fixture, "1 - 2 - 3;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SUBTRACT,
              OP_CONSTANT, 2, OP_SUBTRACT, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, comparison_binds_looser_than_arithmetic) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 2 < 3;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_CONSTANT,
              2, OP_LESS, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, the_same_literal_twice_is_stored_twice) {
  ASSERT_TRUE(compile(utest_fixture, "1 + 1;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_POP,
              OP_RETURN_NIL);
  EXPECT_EQ((size_t)2, utest_fixture->function->chunk.constants.length);
}

UTEST_F(compiler, a_print_statement_emits_print_after_its_expression) {
  ASSERT_TRUE(compile(utest_fixture, "print 1;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_PRINT, OP_RETURN_NIL);
}

UTEST_F(compiler, statements_are_emitted_one_after_another) {
  ASSERT_TRUE(compile(utest_fixture, "1; print 2;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_POP, OP_CONSTANT, 1, OP_PRINT,
              OP_RETURN_NIL);
  ASSERT_EQ((size_t)2, utest_fixture->function->chunk.constants.length);
}

UTEST_F(compiler, an_empty_source_compiles_to_a_return_of_nil) {
  ASSERT_TRUE(compile(utest_fixture, ""));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_RETURN_NIL);
}

UTEST_F(compiler, a_variable_declaration_defines_a_global_from_its_initializer) {
  ASSERT_TRUE(compile(utest_fixture, "var a = 1;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_DEF_GLOBAL, 1, OP_RETURN_NIL);
  ASSERT_EQ((size_t)2, utest_fixture->function->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->function->chunk.constants.values[0]);

  clox_value_t name = utest_fixture->function->chunk.constants.values[1];
  ASSERT_TRUE(CLOX_IS_STRING(name));
  EXPECT_STREQ("a", CLOX_AS_CSTRING(name));
}

UTEST_F(compiler, a_variable_declaration_without_an_initializer_defines_it_as_nil) {
  ASSERT_TRUE(compile(utest_fixture, "var a;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_NIL, OP_DEF_GLOBAL, 0, OP_RETURN_NIL);
  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[0]));
}

UTEST_F(compiler, reading_a_variable_emits_a_global_get) {
  ASSERT_TRUE(compile(utest_fixture, "a;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_POP, OP_RETURN_NIL);
  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[0]));
}

UTEST_F(compiler, assigning_to_a_variable_emits_a_global_set) {
  ASSERT_TRUE(compile(utest_fixture, "a = 1;"));

  // the set stands alone as a statement, so it takes the discarding form
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_SET_GLOBAL_POP, 1, OP_RETURN_NIL);
  ASSERT_EQ((size_t)2, utest_fixture->function->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->function->chunk.constants.values[0]);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[1]));
}

UTEST_F(compiler, assignment_associates_to_the_right) {
  ASSERT_TRUE(compile(utest_fixture, "a = b = 1;"));

  // The innermost assignment runs first, and its value flows outward: only the
  // outermost one is the statement's own, so only it discards.
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_SET_GLOBAL, 1, OP_SET_GLOBAL_POP,
              2, OP_RETURN_NIL);
  ASSERT_EQ((size_t)3, utest_fixture->function->chunk.constants.length);
  EXPECT_STREQ("b", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[1]));
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[2]));
}

UTEST_F(compiler, a_variable_reads_itself_inside_its_own_initializer) {
  ASSERT_TRUE(compile(utest_fixture, "var a = a;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_DEF_GLOBAL, 0, OP_RETURN_NIL);
}

UTEST_F(compiler, the_same_string_literal_becomes_one_constant) {
  ASSERT_TRUE(compile(utest_fixture, "\"x\" + \"x\";"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 0, OP_ADD, OP_POP,
              OP_RETURN_NIL);
  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
  EXPECT_STREQ("x", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[0]));
}

UTEST_F(compiler, a_name_read_and_assigned_shares_one_constant) {
  ASSERT_TRUE(compile(utest_fixture, "a = a;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_SET_GLOBAL_POP, 0,
              OP_RETURN_NIL);
  ASSERT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
}

UTEST_F(compiler, a_global_set_named_by_a_long_index_discards_in_its_long_form) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, past_a_byte_of_constants(&source, "a = 1;")));

  // Which of the pair the fold picks is what decides how the index behind it
  // is read: the short form would take one byte of a three-byte index and run
  // on into the middle of it.
  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  ASSERT_TRUE(chunk->length >= 5);
  // the set, its three index bytes, and the script's own return
  EXPECT_EQ(OP_SET_GLOBAL_POP_LONG, chunk->code[chunk->length - 5]);
  EXPECT_EQ(OP_RETURN_NIL, chunk->code[chunk->length - 1]);
}

UTEST_F(compiler, a_call_standing_as_a_statement_keeps_its_pop) {
  ASSERT_TRUE(compile(utest_fixture, "f();"));

  // a call leaves its result behind only after jumping into the callee's own
  // code, so there is no instruction here for the discard to fold into
  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_CALL, 0, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_set_inside_a_branch_still_discards) {
  ASSERT_TRUE(compile(utest_fixture, "if (a) b = 1;"));

  // the branch is jumped over whole, value and set together, so the set is
  // still reached by every path that reaches the value it takes
  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_JUMP_FALSE_POP, 0, 4,
              OP_CONSTANT, 1, OP_SET_GLOBAL_POP, 2, OP_RETURN_NIL);
}

UTEST_F(compiler, a_variable_takes_part_in_expressions) {
  ASSERT_TRUE(compile(utest_fixture, "print a + 1;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_CONSTANT, 1, OP_ADD, OP_PRINT,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_block_keeps_its_variable_out_of_the_globals) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; }"));

  // one local leaves by the plain OP_POP, not a counted one
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN_NIL);
  // the initializer is the only constant: a local is never named
  EXPECT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
}

UTEST_F(compiler, a_local_is_read_by_its_slot) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; a; }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_POP, OP_POP,
              OP_RETURN_NIL);
  EXPECT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
}

UTEST_F(compiler, a_local_is_assigned_by_its_slot) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; a = 2; }"));

  // the set discards its own value; the pop left is the block dropping a
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SET_LOCAL_POP, 1,
              OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, locals_take_their_slots_in_declaration_order) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; var b = 2; a; b; }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_LOCAL, 1,
              OP_POP, OP_GET_LOCAL, 2, OP_POP, OP_POP_N, 2, OP_RETURN_NIL);
}

UTEST_F(compiler, a_local_shadows_an_enclosing_one_of_the_same_name) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; { var a = 2; a; } }"));

  // the inner slot wins, and each block pops only what it declared
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_GET_LOCAL, 2,
              OP_POP, OP_POP, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, an_inner_block_reaches_the_enclosing_local) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; { a; } }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_POP, OP_POP,
              OP_RETURN_NIL);
}

UTEST_F(compiler, an_inner_block_assigns_to_the_enclosing_local) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; { a = 2; } }"));

  // the inner block declares nothing, so the only pop left is the outer one
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_SET_LOCAL_POP, 1,
              OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_block_declaring_nothing_pops_nothing) {
  ASSERT_TRUE(compile(utest_fixture, "{ 1; }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, leaving_a_block_pops_its_locals_in_one_instruction) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a; var b; var c; }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_NIL, OP_NIL, OP_NIL, OP_POP_N, 3, OP_RETURN_NIL);
}

UTEST_F(compiler, the_counted_pop_starts_at_the_second_local) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a; }"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_NIL, OP_POP, OP_RETURN_NIL);

  ASSERT_TRUE(compile(utest_fixture, "{ var a; var b; }"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_NIL, OP_NIL, OP_POP_N, 2, OP_RETURN_NIL);
}

UTEST_F(compiler, a_declaration_outside_every_block_is_still_a_global) {
  ASSERT_TRUE(compile(utest_fixture, "var a = 1; { var b = 2; }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_DEF_GLOBAL, 1, OP_CONSTANT, 2,
              OP_POP, OP_RETURN_NIL);
  // only the global is named
  ASSERT_EQ((size_t)3, utest_fixture->function->chunk.constants.length);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[1]));
}

UTEST_F(compiler, filling_every_local_slot_pops_them_in_one_counted_group) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, block_of_locals(&source, LOCAL_SLOTS)));

  // one OP_NIL per local, then the widest pop a single byte can count
  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  ASSERT_EQ((size_t)LOCAL_SLOTS + 3, chunk->length);
  EXPECT_EQ(OP_NIL, chunk->code[LOCAL_SLOTS - 1]);
  EXPECT_EQ(OP_POP_N, chunk->code[LOCAL_SLOTS]);
  EXPECT_EQ(UCHAR_MAX, chunk->code[LOCAL_SLOTS + 1]);
  EXPECT_EQ(OP_RETURN_NIL, chunk->code[LOCAL_SLOTS + 2]);
}

UTEST_F(compiler, a_print_of_several_values_counts_them_in_its_operand) {
  ASSERT_TRUE(compile(utest_fixture, "print 1, 2;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_PRINT_N, 2,
              OP_RETURN_NIL);
  ASSERT_EQ((size_t)2, utest_fixture->function->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->function->chunk.constants.values[0]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->function->chunk.constants.values[1]);
}

UTEST_F(compiler, the_values_of_a_print_are_pushed_left_to_right) {
  ASSERT_TRUE(compile(utest_fixture, "print a, b, c;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_GET_GLOBAL, 1, OP_GET_GLOBAL, 2,
              OP_PRINT_N, 3, OP_RETURN_NIL);
  EXPECT_STREQ("a", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[0]));
  EXPECT_STREQ("c", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[2]));
}

UTEST_F(compiler, a_print_of_one_value_stays_on_the_uncounted_opcode) {
  ASSERT_TRUE(compile(utest_fixture, "print 1;"));
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_PRINT, OP_RETURN_NIL);
}

UTEST_F(compiler, the_widest_print_the_operand_can_count_is_accepted) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, print_of(&source, UCHAR_MAX)));

  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  // one two-byte constant per value, then the counted print and the return
  size_t print_at = (size_t)UCHAR_MAX * 2;
  ASSERT_EQ(print_at + 3, chunk->length);
  EXPECT_EQ(OP_PRINT_N, chunk->code[print_at]);
  EXPECT_EQ(UCHAR_MAX, chunk->code[print_at + 1]);
}

UTEST_F(compiler, one_print_value_more_than_the_operand_can_count_is_reported) {
  char source[SOURCE_SIZE];
  EXPECT_FALSE(compile(utest_fixture, print_of(&source, OVER_PRINT_ARGS)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "print args") != NULL);
}

UTEST_F(compiler, a_print_of_several_values_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "print 1, 2"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after values") != NULL);
}

UTEST_F(compiler, an_if_jumps_over_its_branch_when_the_condition_fails) {
  ASSERT_TRUE(compile(utest_fixture, "if (true) print 1;"));

  // the conditional jump drops the condition and skips the branch; with no
  // else to skip in turn, the branch closes on nothing
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 3, OP_CONSTANT, 0,
              OP_PRINT, OP_RETURN_NIL);
}

UTEST_F(compiler, an_else_branch_is_jumped_over_by_the_then_branch) {
  ASSERT_TRUE(compile(utest_fixture, "if (true) print 1; else print 2;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_CONSTANT, 0,
              OP_PRINT, OP_JUMP, 0, 3, OP_CONSTANT, 1, OP_PRINT, OP_RETURN_NIL);
}

UTEST_F(compiler, an_else_belongs_to_the_nearest_if) {
  ASSERT_TRUE(compile(utest_fixture, "if (true) if (false) print 1; else print 2;"));

  // the inner if owns the else, so only the inner then branch closes with a
  // jump; the outer one has no else to jump over
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 13, OP_FALSE,
              OP_JUMP_FALSE_POP, 0, 6, OP_CONSTANT, 0, OP_PRINT, OP_JUMP, 0, 3, OP_CONSTANT, 1,
              OP_PRINT, OP_RETURN_NIL);
}

UTEST_F(compiler, a_while_loop_jumps_back_to_its_condition) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) print 1;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_CONSTANT, 0,
              OP_PRINT, OP_LOOP, 0, 10, OP_RETURN_NIL);
}

UTEST_F(compiler, a_for_loop_runs_its_increment_between_the_body_and_the_condition) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0; i < 3; i = i + 1) print i;"));

  // the increment is compiled before the body but jumped over on the way in,
  // so the body's back jump reaches the increment and the increment's the
  // condition; leaving the loop pops the variable it declared
  EXPECT_CODE(&utest_fixture->function->chunk,                              //
              OP_CONSTANT, 0,                                               // var i = 0
              OP_GET_LOCAL, 1, OP_CONSTANT, 1, OP_LESS,                     // i < 3
              OP_JUMP_FALSE_POP, 0, 19,                                     // exit the loop
              OP_JUMP, 0, 10,                                               // skip the increment
              OP_GET_LOCAL, 1, OP_CONSTANT, 2, OP_ADD, OP_SET_LOCAL_POP, 1, // i = i + 1
              OP_LOOP, 0, 21,                                               // back to i < 3
              OP_GET_LOCAL, 1, OP_PRINT,                                    // print i
              OP_LOOP, 0, 16,                                               // back to i = i + 1
              OP_POP,                                                       // drop i
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_for_loop_without_clauses_only_jumps_back) {
  ASSERT_TRUE(compile(utest_fixture, "for (;;) 1;"));

  // no condition means no way out, and no initializer means nothing to pop
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_POP, OP_LOOP, 0, 6,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_for_loop_keeps_its_variable_in_a_scope_of_its_own) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0;;) i;"));

  // i is a local, never named in the constants, and popped on the way out
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_POP, OP_LOOP, 0,
              6, OP_POP, OP_RETURN_NIL);
  EXPECT_EQ((size_t)1, utest_fixture->function->chunk.constants.length);
}

UTEST_F(compiler, a_for_initializer_may_be_an_expression_instead_of_a_declaration) {
  ASSERT_TRUE(compile(utest_fixture, "for (a = 0;;) 1;"));

  // an expression initializer runs once and drops its value
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_SET_GLOBAL_POP, 1, OP_CONSTANT, 2,
              OP_POP, OP_LOOP, 0, 6, OP_RETURN_NIL);
}

UTEST_F(compiler, a_loop_carrying_no_break_reserves_nothing_for_one) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) 1;"));

  // the way out of a loop is emitted by the first break that needs it, so a
  // body without one costs the same as before there were breaks at all
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_CONSTANT, 0,
              OP_POP, OP_LOOP, 0, 10, OP_RETURN_NIL);
}

UTEST_F(compiler, a_break_jumps_past_the_end_of_its_loop) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { break; }"));

  // the break's jump lands after the loop's backward one, which is where the
  // failing condition lands too
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_JUMP, 0, 3,
              OP_LOOP, 0, 10, OP_RETURN_NIL);
}

UTEST_F(compiler, a_second_break_reaches_the_jump_the_first_one_left) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { break; break; }"));

  // only the first break emits a forward jump; every later one is behind it
  // and reaches it backwards, so no list of sites has to be kept
  EXPECT_CODE(&utest_fixture->function->chunk, //
              OP_TRUE,                         // condition
              OP_JUMP_FALSE_POP, 0, 9,         // exit the loop
              OP_JUMP, 0, 6,                   // first break, out of the loop
              OP_LOOP, 0, 6,                   // second break, back to the first
              OP_LOOP, 0, 13,                  // back to the condition
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_break_pops_the_locals_of_every_block_it_leaves) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { var a; { var b; break; } }"));

  // both locals go in one counted pop before the jump; the pops the blocks
  // themselves emit stay behind for the turn that reaches their closing brace
  EXPECT_CODE(&utest_fixture->function->chunk, //
              OP_TRUE,                         // condition
              OP_JUMP_FALSE_POP, 0, 12,        // exit the loop
              OP_NIL, OP_NIL,                  // var a, var b
              OP_POP_N, 2,                     // the break drops both
              OP_JUMP, 0, 5,                   // out of the loop
              OP_POP, OP_POP,                  // the blocks ending in order
              OP_LOOP, 0, 16,                  // back to the condition
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_break_leaves_the_for_loop_variable_to_the_loop_itself) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0;;) break;"));

  // the variable belongs to the loop's own scope, not to the body, so the
  // break jumps to the pop that drops it rather than emitting one
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_JUMP, 0, 3, OP_LOOP, 0, 6, OP_POP,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_break_in_a_nested_loop_leaves_only_that_loop) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { while (true) break; }"));

  // the inner break lands on the outer loop's backward jump, so the outer
  // loop keeps running and never reserves a way out of its own
  EXPECT_CODE(&utest_fixture->function->chunk, //
              OP_TRUE,                         // outer condition
              OP_JUMP_FALSE_POP, 0, 13,        // exit the outer loop
              OP_TRUE,                         // inner condition
              OP_JUMP_FALSE_POP, 0, 6,         // exit the inner loop
              OP_JUMP, 0, 3,                   // break, out of the inner loop
              OP_LOOP, 0, 10,                  // back to the inner condition
              OP_LOOP, 0, 17,                  // back to the outer condition
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_continue_jumps_back_to_the_condition_of_a_while) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { continue; }"));

  // the condition is already behind the body, so continue needs no patching
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 6, OP_LOOP, 0, 7,
              OP_LOOP, 0, 10, OP_RETURN_NIL);
}

UTEST_F(compiler, a_continue_jumps_back_to_the_increment_of_a_for) {
  ASSERT_TRUE(compile(utest_fixture, "for (var i = 0;; i = 1) continue;"));

  // the increment has to run before the next turn, so continue goes there
  // and not to the condition, exactly where the body's own back jump goes
  EXPECT_CODE(&utest_fixture->function->chunk,     //
              OP_CONSTANT, 0,                      // var i = 0
              OP_JUMP, 0, 7,                       // skip the increment
              OP_CONSTANT, 1, OP_SET_LOCAL_POP, 1, // i = 1
              OP_LOOP, 0, 10,                      // back to the condition
              OP_LOOP, 0, 10,                      // continue, to the increment
              OP_LOOP, 0, 13,                      // body's own back jump
              OP_POP,                              // drop i
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_continue_pops_the_locals_of_every_block_it_leaves) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { var a; continue; }"));

  // the continue drops the local before jumping back, and the pop the block
  // emits at its closing brace stays for the turn that reaches it
  EXPECT_CODE(&utest_fixture->function->chunk, OP_TRUE, OP_JUMP_FALSE_POP, 0, 9, OP_NIL, OP_POP,
              OP_LOOP, 0, 9, OP_POP, OP_LOOP, 0, 13, OP_RETURN_NIL);
}

UTEST_F(compiler, and_jumps_past_its_right_operand_when_the_left_is_falsy) {
  ASSERT_TRUE(compile(utest_fixture, "1 and 2;"));

  // the jump keeps the left operand, which becomes the value of the whole
  // expression; the pop only runs when the right operand replaces it
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_JUMP_FALSE, 0, 3, OP_POP,
              OP_CONSTANT, 1, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, or_jumps_past_its_right_operand_when_the_left_is_truthy) {
  ASSERT_TRUE(compile(utest_fixture, "1 or 2;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_JUMP_TRUE, 0, 3, OP_POP,
              OP_CONSTANT, 1, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, and_binds_tighter_than_or) {
  ASSERT_TRUE(compile(utest_fixture, "1 and 2 or 3;"));

  // the and is complete before the or begins, so its jump lands on the or
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_JUMP_FALSE, 0, 3, OP_POP,
              OP_CONSTANT, 1, OP_JUMP_TRUE, 0, 3, OP_POP, OP_CONSTANT, 2, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, and_binds_looser_than_comparison) {
  ASSERT_TRUE(compile(utest_fixture, "1 < 2 and 3;"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_LESS,
              OP_JUMP_FALSE, 0, 3, OP_POP, OP_CONSTANT, 2, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_set_an_and_can_jump_over_keeps_its_pop) {
  ASSERT_TRUE(compile(utest_fixture, "a and (b = 1);"));

  // The and's jump lands after the set, so a falsy left operand skips it and
  // leaves its own value behind instead. Folding the discard into an
  // instruction that path never reaches would leak that value.
  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_JUMP_FALSE, 0, 5, OP_POP,
              OP_CONSTANT, 1, OP_SET_GLOBAL, 2, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_set_an_or_can_jump_over_keeps_its_pop) {
  ASSERT_TRUE(compile(utest_fixture, "a or (b = 1);"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_JUMP_TRUE, 0, 5, OP_POP,
              OP_CONSTANT, 1, OP_SET_GLOBAL, 2, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_set_taking_a_short_circuit_value_still_discards) {
  ASSERT_TRUE(compile(utest_fixture, "a = b and c;"));

  // here the jump lands before the set rather than after it, so both paths
  // arrive at the set and the fold holds
  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_JUMP_FALSE, 0, 3, OP_POP,
              OP_GET_GLOBAL, 1, OP_SET_GLOBAL_POP, 2, OP_RETURN_NIL);
}

UTEST_F(compiler, a_forward_jump_further_than_a_byte_keeps_its_high_byte) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, long_body(&source, "if (false) ", OVER_BYTE_JUMP)));

  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  // the condition, then the conditional jump: its operand stands at 2
  ASSERT_EQ(OP_JUMP_FALSE_POP, chunk->code[1]);
  EXPECT_TRUE(chunk->code[2] > 0); // the offset outgrew a single byte
  // it skips the body and the jump closing it, landing on the final return
  EXPECT_EQ(chunk->length - 1, 2 + 2 + jump_offset(chunk, 2));
  EXPECT_EQ(OP_RETURN_NIL, chunk->code[chunk->length - 1]);
}

UTEST_F(compiler, a_backward_jump_further_than_a_byte_keeps_its_high_byte) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, long_body(&source, "while (false) ", OVER_BYTE_JUMP)));

  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  // the loop instruction closes the body: three bytes, then the return
  size_t loop = chunk->length - 4;
  ASSERT_EQ(OP_LOOP, chunk->code[loop]);
  EXPECT_TRUE(chunk->code[loop + 1] > 0); // the offset outgrew a single byte
  // it returns to the condition, which opens the chunk
  EXPECT_EQ((size_t)0, loop + 3 - jump_offset(chunk, loop + 1));
}

UTEST_F(compiler, a_forward_jump_too_long_to_encode_is_reported) {
  char *source = long_body_alloc("if (false) ", OVER_TWO_BYTE_JUMP);
  ASSERT_TRUE(source != NULL);

  bool compiled =
      clox_compile(&utest_fixture->compiler, SOURCE_NAME, source, &utest_fixture->function);
  free(source);

  EXPECT_FALSE(compiled);
  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "forward jump") != NULL);
}

UTEST_F(compiler, a_backward_jump_too_long_to_encode_is_reported) {
  char *source = long_body_alloc("while (false) ", OVER_TWO_BYTE_JUMP);
  ASSERT_TRUE(source != NULL);

  bool compiled =
      clox_compile(&utest_fixture->compiler, SOURCE_NAME, source, &utest_fixture->function);
  free(source);

  EXPECT_FALSE(compiled);
  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "backward jump") != NULL);
}

UTEST_F(compiler, an_if_without_parentheses_around_its_condition_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "if true) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "if condition") != NULL);
}

UTEST_F(compiler, a_while_without_parentheses_around_its_condition_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "while true) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "while condition") != NULL);
}

UTEST_F(compiler, a_for_without_parentheses_around_its_clauses_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "for ;;) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "for clauses") != NULL);
}

UTEST_F(compiler, a_for_condition_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "for (; true) print 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "for condition") != NULL);
}

UTEST_F(compiler, a_declaration_is_not_a_body_a_loop_may_carry) {
  // only a block opens a scope a declaration can live in
  EXPECT_FALSE(compile(utest_fixture, "while (true) var a = 1;"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, a_break_outside_every_loop_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "break;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "break") != NULL);
}

UTEST_F(compiler, a_continue_outside_every_loop_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "continue;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "continue") != NULL);
}

UTEST_F(compiler, a_break_after_the_loop_it_followed_is_reported) {
  // the loop's body is over, so its break is no longer inside anything
  EXPECT_FALSE(compile(utest_fixture, "while (true) 1; break;"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, a_break_inside_a_block_outside_a_loop_is_reported) {
  // a block is not a loop body, however deep the break sits in it
  EXPECT_FALSE(compile(utest_fixture, "{ var a; { break; } }"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, a_break_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "while (true) { break }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "';'") != NULL);
}

UTEST_F(compiler, a_continue_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "while (true) { continue }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "';'") != NULL);
}

UTEST_F(compiler, break_and_continue_are_not_names_a_declaration_may_take) {
  EXPECT_FALSE(compile(utest_fixture, "var break = 1;"));
  EXPECT_FALSE(compile(utest_fixture, "var continue = 1;"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, one_local_more_than_there_are_slots_is_reported) {
  char source[SOURCE_SIZE];
  EXPECT_FALSE(compile(utest_fixture, block_of_locals(&source, OVER_LOCAL_SLOTS)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "locals") != NULL);
}

UTEST_F(compiler, declaring_a_local_twice_in_one_block_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "{ var a; var a; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "already defined") != NULL);
}

UTEST_F(compiler, a_local_may_repeat_a_name_from_an_enclosing_block) {
  EXPECT_TRUE(compile(utest_fixture, "{ var a; { var a; } }"));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(compiler, a_local_read_inside_its_own_initializer_is_reported) {
  // the global form of this compiles and fails at run time instead
  EXPECT_FALSE(compile(utest_fixture, "{ var a = a; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "own initializer") != NULL);
}

UTEST_F(compiler, a_block_without_its_closing_brace_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "{ 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after block") != NULL);
}

UTEST_F(compiler, a_number_too_large_to_represent_is_reported) {
  char digits[OVERSIZED_DIGITS + 1];
  memset(digits, '9', OVERSIZED_DIGITS);
  digits[OVERSIZED_DIGITS] = '\0';

  EXPECT_FALSE(compile(utest_fixture, digits));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, an_unfinished_expression_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "1 +"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strlen(utest_fixture->errors.messages[0]) > 0);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.line);
}

UTEST_F(compiler, an_unknown_character_is_reported_where_it_stands) {
  EXPECT_FALSE(compile(utest_fixture, "1 + @"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)5, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, an_unclosed_group_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "(1 + 2"));

  EXPECT_TRUE(utest_fixture->errors.count > 0);
}

UTEST_F(compiler, trailing_input_after_an_expression_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "1 2"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, an_error_on_a_later_line_carries_that_line) {
  EXPECT_FALSE(compile(utest_fixture, "1 +\n\n@"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[0][0].pos.line);
}

UTEST_F(compiler, a_statement_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "print 1"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after value") != NULL);
}

UTEST_F(compiler, a_declaration_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "var a = 1"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after variable declaration") != NULL);
}

UTEST_F(compiler, a_declaration_without_a_name_is_reported_where_the_name_should_stand) {
  EXPECT_FALSE(compile(utest_fixture, "var 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "variable name") != NULL);
  EXPECT_EQ((size_t)5, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, assigning_to_something_that_is_not_a_variable_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "1 = 2;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "assignment target") != NULL);
  EXPECT_EQ((size_t)3, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, assignment_reaching_past_a_binary_operator_is_reported) {
  // '=' binds loosest, so it cannot be the right operand of '+'
  EXPECT_FALSE(compile(utest_fixture, "a + b = 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "assignment target") != NULL);
  EXPECT_EQ((size_t)7, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, an_error_in_a_later_statement_is_reported_too) {
  // the first error silences the rest of its statement, not the next one
  EXPECT_FALSE(compile(utest_fixture, "1 + ;\n2 + ;"));

  ASSERT_EQ((size_t)2, utest_fixture->errors.count);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.line);
  EXPECT_EQ((size_t)2, utest_fixture->errors.stacks[1][0].pos.line);
}

UTEST_F(compiler, one_broken_statement_reports_one_error) {
  EXPECT_FALSE(compile(utest_fixture, "print 1 + ;"));

  EXPECT_EQ((size_t)1, utest_fixture->errors.count);
}

UTEST_F(compiler, a_reported_error_carries_the_name_the_source_was_compiled_under) {
  EXPECT_FALSE(compile(utest_fixture, "1 + @"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_STREQ(SOURCE_NAME, utest_fixture->errors.stacks[0][0].file_name);
}

UTEST_F(compiler, a_reported_error_points_back_at_the_source_it_was_found_in) {
  // the buffer handed to the compiler is the one a reporter reads the
  // offending line out of, so the frame carries that buffer itself
  EXPECT_FALSE(compile(utest_fixture, "1 + @"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((const char *)utest_fixture->source, utest_fixture->errors.stacks[0][0].source);
}

UTEST_F(compiler, every_function_a_run_builds_is_stamped_with_the_name_and_the_source) {
  // a nested function reports against the same file and the same text as the
  // script it was declared in, so a trace out through it names one source
  // throughout
  ASSERT_TRUE(compile(utest_fixture, "fun f() { return 1; }"));

  clox_function_t *script = utest_fixture->function;
  ASSERT_TRUE(script != NULL);
  EXPECT_STREQ(SOURCE_NAME, script->file_name);
  EXPECT_EQ((const char *)utest_fixture->source, script->source);

  clox_function_t *declared = function_constant(&script->chunk, 0);
  ASSERT_TRUE(declared != NULL);
  EXPECT_STREQ(SOURCE_NAME, declared->file_name);
  EXPECT_EQ((const char *)utest_fixture->source, declared->source);
}

UTEST_F(compiler, an_error_at_the_top_level_stands_in_the_script) {
  EXPECT_FALSE(compile(utest_fixture, "1 + @"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_STREQ(CLOX_SCRIPT_NAME, utest_fixture->errors.stacks[0][0].fn_name);
}

UTEST_F(compiler, an_error_inside_a_function_stands_in_that_function) {
  EXPECT_FALSE(compile(utest_fixture, "fun f() { 1 + ; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_STREQ("f", utest_fixture->errors.stacks[0][0].fn_name);
}

UTEST_F(compiler, an_error_inside_a_nested_function_stands_in_the_innermost_one) {
  EXPECT_FALSE(compile(utest_fixture, "fun outer() { fun inner() { 1 + ; } }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_STREQ("inner", utest_fixture->errors.stacks[0][0].fn_name);
}

UTEST_F(compiler, a_compile_error_reports_the_one_place_it_was_found) {
  // nothing is running yet, so there is no call stack to trace an error out
  // through: a compile error stands where the parser stood and nowhere else
  EXPECT_FALSE(compile(utest_fixture, "fun outer() { fun inner() { 1 + ; } }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stack_sizes[0]);
}

UTEST_F(compiler, a_compiler_without_a_handler_still_reports_failure) {
  clox_compiler_reset_error_handler(&utest_fixture->compiler);

  EXPECT_FALSE(compile(utest_fixture, "1 +"));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(compiler, a_function_declaration_defines_a_global_holding_the_function) {
  ASSERT_TRUE(compile(utest_fixture, "fun f() {}"));

  // the compiled function is pushed as the constant it is, then bound to its
  // name: capturing nothing, it needs nothing wrapped around it
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_DEF_GLOBAL, 1, OP_RETURN_NIL);
  ASSERT_EQ((size_t)2, utest_fixture->function->chunk.constants.length);
  ASSERT_TRUE(function_constant(&utest_fixture->function->chunk, 0) != NULL);
  EXPECT_STREQ("f", CLOX_AS_CSTRING(utest_fixture->function->chunk.constants.values[1]));
}

UTEST_F(compiler, a_compiled_function_carries_its_name_and_arity) {
  ASSERT_TRUE(compile(utest_fixture, "fun f(a, b, c) {}"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_STREQ("f", compiled->name);
  EXPECT_EQ((size_t)3, compiled->arity);
}

UTEST_F(compiler, the_script_itself_is_named_as_a_script) {
  ASSERT_TRUE(compile(utest_fixture, "1;"));

  // the top-level frame has no declared name, and is given the standing one
  // rather than none: an error reported in it has a function to name
  EXPECT_STREQ(CLOX_SCRIPT_NAME, utest_fixture->function->name);
}

UTEST_F(compiler, an_empty_function_body_returns_nil) {
  ASSERT_TRUE(compile(utest_fixture, "fun f() {}"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_RETURN_NIL);
}

UTEST_F(compiler, a_function_body_compiles_into_the_functions_own_chunk) {
  ASSERT_TRUE(compile(utest_fixture, "fun f() { print 1; }"));

  // the body is not in the script's code
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_DEF_GLOBAL, 1, OP_RETURN_NIL);

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_CONSTANT, 0, OP_PRINT, OP_RETURN_NIL);
}

UTEST_F(compiler, a_return_with_a_value_returns_what_it_evaluates) {
  ASSERT_TRUE(compile(utest_fixture, "fun f() { return 1; }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  // the trailing return is emitted whether or not the body can reach it
  EXPECT_CODE(&compiled->chunk, OP_CONSTANT, 0, OP_RETURN, OP_RETURN_NIL);
}

UTEST_F(compiler, a_bare_return_returns_nil) {
  ASSERT_TRUE(compile(utest_fixture, "fun f() { return; }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_RETURN_NIL, OP_RETURN_NIL);
}

UTEST_F(compiler, a_return_out_of_a_scope_does_not_pop_its_locals) {
  // the return rewinds the whole frame, so the pops a scope would emit on the
  // way out are not needed on this path
  ASSERT_TRUE(compile(utest_fixture, "fun f() { { var a = 1; return a; } }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_RETURN, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, parameters_take_the_slots_after_the_reserved_one) {
  // slot 0 of a frame belongs to the function itself
  ASSERT_TRUE(compile(utest_fixture, "fun f(a, b) { print a; print b; }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_GET_LOCAL, 1, OP_PRINT, OP_GET_LOCAL, 2, OP_PRINT,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_body_local_follows_the_parameters) {
  ASSERT_TRUE(compile(utest_fixture, "fun f(a) { var b = 1; print b; }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 2, OP_PRINT, OP_RETURN_NIL);
}

UTEST_F(compiler, a_parameter_can_be_assigned_like_any_local) {
  ASSERT_TRUE(compile(utest_fixture, "fun f(a) { a = 1; }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_CONSTANT, 0, OP_SET_LOCAL_POP, 1, OP_RETURN_NIL);
}

UTEST_F(compiler, a_frame_reaches_a_local_around_it_through_an_upvalue) {
  // the enclosing local is not in the function's frame, so the name is
  // resolved as a capture of it rather than as a slot or a global
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; fun f() { print a; } }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 1);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_GET_UPVALUE, 0, OP_PRINT, OP_RETURN_NIL);
  EXPECT_EQ((size_t)1, compiled->upvalue_count);
}

UTEST_F(compiler, a_name_no_enclosing_frame_declares_is_still_a_global) {
  // the block has closed before the function is declared, so there is no
  // enclosing local left of that name to capture
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; } fun f() { print a; }"));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 1);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_CODE(&compiled->chunk, OP_GET_GLOBAL, 0, OP_PRINT, OP_RETURN_NIL);
  EXPECT_EQ((size_t)0, compiled->upvalue_count);
}

UTEST_F(compiler, a_function_capturing_nothing_is_emitted_as_a_plain_constant) {
  ASSERT_TRUE(compile(utest_fixture, "fun f() {}"));

  // there is nothing for a closure to hold, so none is made: the function
  // reaches the stack the way any other constant does
  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_EQ((size_t)0, compiled->upvalue_count);
  EXPECT_EQ((clox_byte_t)OP_CONSTANT, utest_fixture->function->chunk.code[0]);
}

UTEST_F(compiler, a_function_capturing_nothing_at_a_long_index_takes_the_long_constant) {
  char source[SOURCE_SIZE];
  ASSERT_TRUE(compile(utest_fixture, past_a_byte_of_constants(&source, "fun f() {}")));

  // the choice of instruction is made before the index is written, so the
  // plain-constant form has to reach its long variant like any other constant
  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  ASSERT_TRUE(chunk->length >= 9);
  // the function's long constant, the long define of its name, and the return
  EXPECT_EQ(OP_CONSTANT_LONG, chunk->code[chunk->length - 9]);
  EXPECT_EQ(OP_DEF_GLOBAL_LONG, chunk->code[chunk->length - 5]);
  EXPECT_EQ(OP_RETURN_NIL, chunk->code[chunk->length - 1]);
}

UTEST_F(compiler, whether_a_function_is_wrapped_is_decided_one_function_at_a_time) {
  // two declarations side by side in one scope, one naming the local around
  // them and one naming nothing: only the one that captures is wrapped
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; fun plain() {} fun capturing() { print a; } }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CLOSURE, 2, 1, 1,
              OP_POP_N, 2, OP_CLOSE_UPVALUE, OP_RETURN_NIL);
}

UTEST_F(compiler, a_function_capturing_nothing_inside_one_that_does_is_still_a_plain_constant) {
  // the middle function is wrapped, and the innermost one is not: what decides
  // the form is the function's own captures, not those of the frame around it
  ASSERT_TRUE(compile(utest_fixture, "fun o() { var a = 1; fun m() { print a; fun i() {} } }"));

  clox_function_t *outer = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(outer != NULL);
  EXPECT_CODE(&outer->chunk, OP_CONSTANT, 0, OP_CLOSURE, 1, 1, 1, OP_RETURN_NIL);

  clox_function_t *middle = function_constant(&outer->chunk, 1);
  ASSERT_TRUE(middle != NULL);
  EXPECT_EQ((size_t)1, middle->upvalue_count);
  // the innermost function stays in the slot it was made into; the return
  // rewinds the frame, so the scope emits no pop for it
  EXPECT_CODE(&middle->chunk, OP_GET_UPVALUE, 0, OP_PRINT, OP_CONSTANT, 0, OP_RETURN_NIL);
}

UTEST_F(compiler, a_capture_says_it_is_a_local_and_names_the_slot_it_is_taken_from) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; fun f() { print a; } }"));

  // the two bytes after the constant index: taken from a local of this frame,
  // standing in slot 1
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CLOSURE, 1, 1, 1, OP_POP,
              OP_CLOSE_UPVALUE, OP_RETURN_NIL);
}

UTEST_F(compiler, a_capture_from_further_out_is_taken_from_the_enclosing_closure) {
  // two frames up: the middle function does not name a itself, but has to
  // capture it so the innermost one can be handed it in turn
  ASSERT_TRUE(compile(utest_fixture, "fun o() { var a = 1; fun m() { fun i() { print a; } } }"));

  clox_function_t *outer = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(outer != NULL);
  EXPECT_CODE(&outer->chunk, OP_CONSTANT, 0, OP_CLOSURE, 1, 1, 1, OP_RETURN_NIL);

  clox_function_t *middle = function_constant(&outer->chunk, 1);
  ASSERT_TRUE(middle != NULL);
  EXPECT_EQ((size_t)1, middle->upvalue_count);
  // not a local of the middle frame, and taken at capture index 0 rather than
  // at a slot
  EXPECT_CODE(&middle->chunk, OP_CLOSURE, 0, 0, 0, OP_RETURN_NIL);
}

UTEST_F(compiler, one_name_used_twice_is_captured_once) {
  ASSERT_TRUE(compile(utest_fixture, "fun o() { var a = 1; fun i() { print a; print a; } }"));

  clox_function_t *outer = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(outer != NULL);
  clox_function_t *inner = function_constant(&outer->chunk, 1);
  ASSERT_TRUE(inner != NULL);

  // both reads go through the one capture, so the closure carries a single
  // upvalue and both instructions name index 0
  EXPECT_EQ((size_t)1, inner->upvalue_count);
  EXPECT_CODE(&inner->chunk, OP_GET_UPVALUE, 0, OP_PRINT, OP_GET_UPVALUE, 0, OP_PRINT,
              OP_RETURN_NIL);
}

UTEST_F(compiler, assigning_to_a_captured_name_writes_through_the_upvalue) {
  ASSERT_TRUE(compile(utest_fixture, "fun o() { var a = 1; fun i() { a = 2; } }"));

  clox_function_t *outer = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(outer != NULL);
  clox_function_t *inner = function_constant(&outer->chunk, 1);
  ASSERT_TRUE(inner != NULL);
  EXPECT_CODE(&inner->chunk, OP_CONSTANT, 0, OP_SET_UPVALUE_POP, 0, OP_RETURN_NIL);
}

UTEST_F(compiler, a_parameter_is_captured_like_any_other_local) {
  ASSERT_TRUE(compile(utest_fixture, "fun o(p) { fun i() { print p; } }"));

  clox_function_t *outer = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(outer != NULL);
  // the parameter stands in slot 1, and is captured from there
  EXPECT_CODE(&outer->chunk, OP_CLOSURE, 0, 1, 1, OP_RETURN_NIL);
}

UTEST_F(compiler, a_local_function_reaches_itself_through_an_upvalue) {
  // the name is declared and marked initialized before the body is compiled,
  // so a call to it from inside resolves to the slot it is about to fill
  ASSERT_TRUE(compile(utest_fixture, "fun o() { fun i(n) { return i(n); } }"));

  clox_function_t *outer = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(outer != NULL);
  clox_function_t *inner = function_constant(&outer->chunk, 0);
  ASSERT_TRUE(inner != NULL);

  EXPECT_EQ((size_t)1, inner->upvalue_count);
  EXPECT_CODE(&inner->chunk, OP_GET_UPVALUE, 0, OP_GET_LOCAL, 1, OP_CALL, 1, OP_RETURN,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_captured_local_is_closed_and_not_popped_when_its_scope_ends) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; fun f() { print a; } }"));

  // the closure outlives the slot, so the value has to be moved off the stack
  // rather than dropped: the function beside it is popped as usual
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CLOSURE, 1, 1, 1, OP_POP,
              OP_CLOSE_UPVALUE, OP_RETURN_NIL);
}

UTEST_F(compiler, the_locals_above_a_captured_one_are_still_popped_in_a_group) {
  ASSERT_TRUE(compile(utest_fixture, "{ var a = 1; var b = 2; fun f() { print a; } }"));

  // b and f come off together, and only the capture below them is closed
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_CLOSURE, 2, 1, 1,
              OP_POP_N, 2, OP_CLOSE_UPVALUE, OP_RETURN_NIL);
}

UTEST_F(compiler, break_closes_a_captured_local_on_its_way_out_of_the_loop) {
  // break leaves the scope without running the pops its end emits, so it
  // carries a close of its own for anything captured under it
  ASSERT_TRUE(compile(utest_fixture, "while (true) { var a = 1; fun f() { print a; } break; }"));

  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  ASSERT_TRUE(chunk->length > 12);
  EXPECT_EQ((clox_byte_t)OP_POP, chunk->code[10]);
  EXPECT_EQ((clox_byte_t)OP_CLOSE_UPVALUE, chunk->code[11]);
  EXPECT_EQ((clox_byte_t)OP_JUMP, chunk->code[12]);
}

UTEST_F(compiler, continue_closes_a_captured_local_on_its_way_round) {
  ASSERT_TRUE(compile(utest_fixture, "while (true) { var a = 1; fun f() { print a; } continue; }"));

  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  ASSERT_TRUE(chunk->length > 12);
  EXPECT_EQ((clox_byte_t)OP_POP, chunk->code[10]);
  EXPECT_EQ((clox_byte_t)OP_CLOSE_UPVALUE, chunk->code[11]);
  EXPECT_EQ((clox_byte_t)OP_LOOP, chunk->code[12]);
}

UTEST_F(compiler, capturing_as_many_names_as_the_limit_allows_is_accepted) {
  char source[SOURCE_SIZE];
  // one frame holds the slots, the next holds one more, and the innermost
  // names every one of them: exactly what a byte of capture index can count
  ASSERT_TRUE(compile(utest_fixture, functions_capturing(&source, CAPTURABLE_LOCALS,
                                                         UPVALUE_SLOTS - CAPTURABLE_LOCALS)));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(compiler, one_capture_too_many_is_reported) {
  char source[SOURCE_SIZE];
  EXPECT_FALSE(compile(utest_fixture, functions_capturing(&source, CAPTURABLE_LOCALS,
                                                          OVER_UPVALUE_SLOTS - CAPTURABLE_LOCALS)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "upvalues limit") != NULL);
}

UTEST_F(compiler, a_function_declared_in_a_block_is_a_local) {
  ASSERT_TRUE(compile(utest_fixture, "{ fun f() {} }"));

  // no OP_DEF_GLOBAL: the function stays in the slot it was pushed into, and
  // the scope pops it on the way out
  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_local_function_can_be_called_through_its_slot) {
  ASSERT_TRUE(compile(utest_fixture, "{ fun f() {} f(); }"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_GET_LOCAL, 1, OP_CALL, 0, OP_POP,
              OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_nested_function_is_a_constant_of_the_one_around_it) {
  ASSERT_TRUE(compile(utest_fixture, "fun outer() { fun inner() {} }"));

  clox_function_t *outer = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(outer != NULL);
  EXPECT_STREQ("outer", outer->name);

  clox_function_t *inner = function_constant(&outer->chunk, 0);
  ASSERT_TRUE(inner != NULL);
  EXPECT_STREQ("inner", inner->name);
}

UTEST_F(compiler, a_call_emits_the_number_of_arguments_it_carries) {
  ASSERT_TRUE(compile(utest_fixture, "f(1, 2);"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_CONSTANT, 1, OP_CONSTANT, 2,
              OP_CALL, 2, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_call_without_arguments_counts_none) {
  ASSERT_TRUE(compile(utest_fixture, "f();"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_CALL, 0, OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, arguments_are_evaluated_left_to_right) {
  ASSERT_TRUE(compile(utest_fixture, "f(1, 2);"));

  ASSERT_EQ((size_t)3, utest_fixture->function->chunk.constants.length);
  EXPECT_VALUE_EQ(CLOX_NUMBER(1.0), utest_fixture->function->chunk.constants.values[1]);
  EXPECT_VALUE_EQ(CLOX_NUMBER(2.0), utest_fixture->function->chunk.constants.values[2]);
}

UTEST_F(compiler, calls_chain_left_to_right) {
  ASSERT_TRUE(compile(utest_fixture, "f()();"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_CALL, 0, OP_CALL, 0, OP_POP,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_call_binds_tighter_than_a_unary_operator) {
  ASSERT_TRUE(compile(utest_fixture, "-f();"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_GET_GLOBAL, 0, OP_CALL, 0, OP_NEGATE, OP_POP,
              OP_RETURN_NIL);
}

UTEST_F(compiler, a_call_binds_tighter_than_a_binary_operator) {
  ASSERT_TRUE(compile(utest_fixture, "1 + f();"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_GET_GLOBAL, 1, OP_CALL, 0, OP_ADD,
              OP_POP, OP_RETURN_NIL);
}

UTEST_F(compiler, a_parenthesised_expression_is_still_a_grouping) {
  // the same token opens a call and a grouping; only an operand before it
  // makes it a call
  ASSERT_TRUE(compile(utest_fixture, "(1 + 2);"));

  EXPECT_CODE(&utest_fixture->function->chunk, OP_CONSTANT, 0, OP_CONSTANT, 1, OP_ADD, OP_POP,
              OP_RETURN_NIL);
}

UTEST_F(compiler, the_widest_parameter_list_is_accepted) {
  char source[SOURCE_SIZE];

  ASSERT_TRUE(compile(utest_fixture, function_of_params(&source, CLOX_MAX_ARITY)));

  clox_function_t *compiled = function_constant(&utest_fixture->function->chunk, 0);
  ASSERT_TRUE(compiled != NULL);
  EXPECT_EQ((size_t)CLOX_MAX_ARITY, compiled->arity);
}

UTEST_F(compiler, one_parameter_too_many_is_reported) {
  char source[SOURCE_SIZE];

  EXPECT_FALSE(compile(utest_fixture, function_of_params(&source, CLOX_MAX_ARITY + 1)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "params") != NULL);
}

UTEST_F(compiler, the_widest_argument_list_is_accepted) {
  // a call must reach as wide as a declaration, or the widest function that
  // can be declared could never be called
  char source[SOURCE_SIZE];

  ASSERT_TRUE(compile(utest_fixture, call_of_args(&source, CLOX_MAX_ARITY)));

  const clox_chunk_t *chunk = &utest_fixture->function->chunk;
  // the tail is OP_CALL, its count, then the pop and the script's own return
  ASSERT_TRUE(chunk->length >= 4);
  EXPECT_EQ((clox_byte_t)OP_CALL, chunk->code[chunk->length - 4]);
  EXPECT_EQ((clox_byte_t)CLOX_MAX_ARITY, chunk->code[chunk->length - 3]);
}

UTEST_F(compiler, one_argument_too_many_is_reported) {
  char source[SOURCE_SIZE];

  EXPECT_FALSE(compile(utest_fixture, call_of_args(&source, CLOX_MAX_ARITY + 1)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "args") != NULL);
}

UTEST_F(compiler, nesting_declarations_as_deep_as_the_limit_allows_is_accepted) {
  char source[SOURCE_SIZE];

  EXPECT_TRUE(compile(utest_fixture, nested_functions(&source, DECLARATION_DEPTH_LIMIT - 1)));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(compiler, nesting_declarations_past_the_limit_is_reported) {
  char source[SOURCE_SIZE];

  EXPECT_FALSE(compile(utest_fixture, nested_functions(&source, DECLARATION_DEPTH_LIMIT)));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "declaration depth") != NULL);
}

UTEST_F(compiler, a_return_at_the_top_level_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "return 1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "top-level") != NULL);
  EXPECT_EQ((size_t)1, utest_fixture->errors.stacks[0][0].pos.col);
}

UTEST_F(compiler, a_return_in_a_top_level_block_is_reported_too) {
  // a block is not a frame: the script is still what encloses this
  EXPECT_FALSE(compile(utest_fixture, "{ return; }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "top-level") != NULL);
}

UTEST_F(compiler, a_return_without_its_semicolon_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "fun f() { return 1 }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after return value") != NULL);
}

UTEST_F(compiler, a_function_without_a_name_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "fun () {}"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "function name") != NULL);
}

UTEST_F(compiler, a_function_without_its_parameter_list_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "fun f {}"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "function parameters") != NULL);
}

UTEST_F(compiler, a_function_without_a_body_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "fun f();"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "function body") != NULL);
}

UTEST_F(compiler, a_parameter_that_is_not_a_name_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "fun f(1) {}"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "param name") != NULL);
}

UTEST_F(compiler, a_repeated_parameter_name_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "fun f(a, a) {}"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "already defined") != NULL);
}

UTEST_F(compiler, a_parameter_may_be_shadowed_inside_the_body) {
  // the body opens a scope of its own, so this is not a redefinition
  EXPECT_TRUE(compile(utest_fixture, "fun f(a) { { var a = 1; } }"));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

UTEST_F(compiler, a_call_without_its_closing_paren_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "f(1;"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "after args") != NULL);
}

UTEST_F(compiler, break_inside_a_function_in_a_loop_is_reported) {
  // the loop is the caller's, not this frame's: a break here has nothing to
  // leave
  EXPECT_FALSE(compile(utest_fixture, "while (true) { fun f() { break; } }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "inside loop body") != NULL);
}

UTEST_F(compiler, continue_inside_a_function_in_a_loop_is_reported) {
  EXPECT_FALSE(compile(utest_fixture, "while (true) { fun f() { continue; } }"));

  ASSERT_TRUE(utest_fixture->errors.count > 0);
  EXPECT_TRUE(strstr(utest_fixture->errors.messages[0], "inside loop body") != NULL);
}

UTEST_F(compiler, a_loop_inside_a_function_carries_its_own_break) {
  EXPECT_TRUE(compile(utest_fixture, "while (true) { fun f() { while (true) { break; } } }"));
  EXPECT_EQ((size_t)0, utest_fixture->errors.count);
}

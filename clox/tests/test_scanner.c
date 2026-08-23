#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <utest.h>

#include "common.h"
#include "scanner.h"

// An enumerator constant has type int while the enum type itself is unsigned,
// so the expected side is cast to keep the comparison sign-clean.
#define EXPECT_TOKEN_TYPE(expected, actual) EXPECT_EQ((clox_token_type_t)(expected), (actual))
#define ASSERT_TOKEN_TYPE(expected, actual) ASSERT_EQ((clox_token_type_t)(expected), (actual))

// Scans source and returns its first token.
static clox_token_t scan_first(const char *source) {
  clox_scanner_t scanner;
  clox_scanner_init(&scanner, source);
  clox_token_t token = clox_scan(&scanner);
  clox_scanner_free(&scanner);

  return token;
}

// True when the token's lexeme is exactly text.
static bool lexeme_is(clox_token_t token, const char *text) {
  return token.length == strlen(text) && memcmp(token.start, text, token.length) == 0;
}

UTEST(scanner, an_empty_source_scans_to_end_of_file) {
  EXPECT_TOKEN_TYPE(TOKEN_EOF, scan_first("").type);
}

UTEST(scanner, each_single_character_token_scans_to_its_type) {
  EXPECT_TOKEN_TYPE(TOKEN_LEFT_PAREN, scan_first("(").type);
  EXPECT_TOKEN_TYPE(TOKEN_RIGHT_PAREN, scan_first(")").type);
  EXPECT_TOKEN_TYPE(TOKEN_LEFT_BRACE, scan_first("{").type);
  EXPECT_TOKEN_TYPE(TOKEN_RIGHT_BRACE, scan_first("}").type);
  EXPECT_TOKEN_TYPE(TOKEN_COMMA, scan_first(",").type);
  EXPECT_TOKEN_TYPE(TOKEN_DOT, scan_first(".").type);
  EXPECT_TOKEN_TYPE(TOKEN_MINUS, scan_first("-").type);
  EXPECT_TOKEN_TYPE(TOKEN_PLUS, scan_first("+").type);
  EXPECT_TOKEN_TYPE(TOKEN_SEMICOLON, scan_first(";").type);
  EXPECT_TOKEN_TYPE(TOKEN_SLASH, scan_first("/").type);
  EXPECT_TOKEN_TYPE(TOKEN_STAR, scan_first("*").type);
}

UTEST(scanner, each_operator_scans_to_its_one_or_two_character_form) {
  EXPECT_TOKEN_TYPE(TOKEN_BANG, scan_first("!").type);
  EXPECT_TOKEN_TYPE(TOKEN_BANG_EQUAL, scan_first("!=").type);
  EXPECT_TOKEN_TYPE(TOKEN_EQUAL, scan_first("=").type);
  EXPECT_TOKEN_TYPE(TOKEN_EQUAL_EQUAL, scan_first("==").type);
  EXPECT_TOKEN_TYPE(TOKEN_GREATER, scan_first(">").type);
  EXPECT_TOKEN_TYPE(TOKEN_GREATER_EQUAL, scan_first(">=").type);
  EXPECT_TOKEN_TYPE(TOKEN_LESS, scan_first("<").type);
  EXPECT_TOKEN_TYPE(TOKEN_LESS_EQUAL, scan_first("<=").type);
}

UTEST(scanner, each_keyword_scans_to_its_type) {
  // a table rather than a line apiece: every comparison the macro expands to
  // counts towards one function's size, and this list only ever grows
  const struct {
    const char *text;
    clox_token_type_t type;
  } keywords[] = {
      {"and", TOKEN_AND},           {"break", TOKEN_BREAK}, {"class", TOKEN_CLASS},
      {"continue", TOKEN_CONTINUE}, {"else", TOKEN_ELSE},   {"false", TOKEN_FALSE},
      {"for", TOKEN_FOR},           {"fun", TOKEN_FUN},     {"if", TOKEN_IF},
      {"nil", TOKEN_NIL},           {"or", TOKEN_OR},       {"print", TOKEN_PRINT},
      {"return", TOKEN_RETURN},     {"super", TOKEN_SUPER}, {"this", TOKEN_THIS},
      {"true", TOKEN_TRUE},         {"var", TOKEN_VAR},     {"while", TOKEN_WHILE},
  };

  for (size_t i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
    EXPECT_EQ_MSG(keywords[i].type, scan_first(keywords[i].text).type, keywords[i].text);
  }
}

UTEST(scanner, a_word_that_only_starts_like_a_keyword_is_an_identifier) {
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("orchid").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("iffy").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("classy").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("breaking").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("continued").type);
}

UTEST(scanner, a_word_stopping_short_of_a_keyword_is_an_identifier) {
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("brea").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("continu").type);
}

UTEST(scanner, a_word_sharing_a_first_letter_with_a_keyword_is_an_identifier) {
  // c, f and t each begin more than one keyword, so a word starting with one
  // of them is told apart by its second letter, and a word of one letter has
  // none to be told apart by
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("cat").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("comb").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("c").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("f").type);
  EXPECT_TOKEN_TYPE(TOKEN_IDENTIFIER, scan_first("t").type);
}

UTEST(scanner, a_keyword_carries_its_whole_lexeme) {
  clox_token_t token = scan_first("continue;");

  ASSERT_TOKEN_TYPE(TOKEN_CONTINUE, token.type);
  EXPECT_TRUE(lexeme_is(token, "continue"));
}

UTEST(scanner, identifiers_carry_their_whole_lexeme) {
  clox_token_t token = scan_first("_name123 rest");

  ASSERT_TOKEN_TYPE(TOKEN_IDENTIFIER, token.type);
  EXPECT_TRUE(lexeme_is(token, "_name123"));
}

UTEST(scanner, numbers_carry_their_whole_lexeme) {
  clox_token_t whole = scan_first("123 rest");
  ASSERT_TOKEN_TYPE(TOKEN_NUMBER, whole.type);
  EXPECT_TRUE(lexeme_is(whole, "123"));

  clox_token_t fraction = scan_first("1.5 rest");
  ASSERT_TOKEN_TYPE(TOKEN_NUMBER, fraction.type);
  EXPECT_TRUE(lexeme_is(fraction, "1.5"));
}

UTEST(scanner, a_trailing_dot_is_not_part_of_a_number) {
  clox_scanner_t scanner;
  clox_scanner_init(&scanner, "1.");

  clox_token_t number = clox_scan(&scanner);
  ASSERT_TOKEN_TYPE(TOKEN_NUMBER, number.type);
  EXPECT_TRUE(lexeme_is(number, "1"));
  EXPECT_TOKEN_TYPE(TOKEN_DOT, clox_scan(&scanner).type);

  clox_scanner_free(&scanner);
}

UTEST(scanner, a_string_literal_is_one_token) {
  EXPECT_TOKEN_TYPE(TOKEN_STRING, scan_first("\"text\"").type);
  EXPECT_TOKEN_TYPE(TOKEN_STRING, scan_first("\"\"").type);
  EXPECT_TOKEN_TYPE(TOKEN_STRING, scan_first("\"with spaces and 123\"").type);
}

UTEST(scanner, whitespace_and_comments_are_skipped) {
  EXPECT_TOKEN_TYPE(TOKEN_PLUS, scan_first("   \t\r\n  +").type);
  EXPECT_TOKEN_TYPE(TOKEN_PLUS, scan_first("// a comment\n+").type);
  EXPECT_TOKEN_TYPE(TOKEN_EOF, scan_first("// only a comment").type);
}

UTEST(scanner, tokens_come_in_source_order) {
  clox_scanner_t scanner;
  clox_scanner_init(&scanner, "1 + 2 * (3 - 4)");

  const clox_token_type_t expected[] = {
      TOKEN_NUMBER, TOKEN_PLUS,  TOKEN_NUMBER, TOKEN_STAR,        TOKEN_LEFT_PAREN,
      TOKEN_NUMBER, TOKEN_MINUS, TOKEN_NUMBER, TOKEN_RIGHT_PAREN, TOKEN_EOF,
  };
  for (size_t i = 0; i < sizeof(expected) / sizeof(*expected); i++) {
    ASSERT_EQ(expected[i], clox_scan(&scanner).type);
  }

  clox_scanner_free(&scanner);
}

UTEST(scanner, positions_are_one_based_and_start_at_the_lexeme) {
  clox_scanner_t scanner;
  clox_scanner_init(&scanner, "1 + 22");

  clox_token_t first = clox_scan(&scanner);
  EXPECT_EQ((size_t)1, first.pos.line);
  EXPECT_EQ((size_t)1, first.pos.col);

  clox_token_t plus = clox_scan(&scanner);
  EXPECT_EQ((size_t)1, plus.pos.line);
  EXPECT_EQ((size_t)3, plus.pos.col);

  clox_token_t second = clox_scan(&scanner);
  EXPECT_EQ((size_t)1, second.pos.line);
  EXPECT_EQ((size_t)5, second.pos.col);

  clox_scanner_free(&scanner);
}

UTEST(scanner, a_newline_starts_a_line_and_resets_the_column) {
  clox_scanner_t scanner;
  clox_scanner_init(&scanner, "1\n  22\n\n333");

  EXPECT_EQ((size_t)1, clox_scan(&scanner).pos.line);

  clox_token_t second = clox_scan(&scanner);
  EXPECT_EQ((size_t)2, second.pos.line);
  EXPECT_EQ((size_t)3, second.pos.col);

  clox_token_t third = clox_scan(&scanner);
  EXPECT_EQ((size_t)4, third.pos.line);
  EXPECT_EQ((size_t)1, third.pos.col);

  clox_scanner_free(&scanner);
}

UTEST(scanner, an_unknown_character_is_an_error_token_carrying_a_message) {
  clox_token_t token = scan_first("@");

  ASSERT_TOKEN_TYPE(TOKEN_ERROR, token.type);
  EXPECT_TRUE(token.length > 0);
  EXPECT_EQ((size_t)1, token.pos.line);
  EXPECT_EQ((size_t)1, token.pos.col);
}

UTEST(scanner, an_unterminated_string_is_an_error_token) {
  clox_token_t token = scan_first("\"no end");

  ASSERT_TOKEN_TYPE(TOKEN_ERROR, token.type);
  EXPECT_TRUE(token.length > 0);
}

UTEST(scanner, scanning_past_the_end_keeps_returning_end_of_file) {
  clox_scanner_t scanner;
  clox_scanner_init(&scanner, "1");

  ASSERT_TOKEN_TYPE(TOKEN_NUMBER, clox_scan(&scanner).type);
  EXPECT_TOKEN_TYPE(TOKEN_EOF, clox_scan(&scanner).type);
  EXPECT_TOKEN_TYPE(TOKEN_EOF, clox_scan(&scanner).type);
  EXPECT_TOKEN_TYPE(TOKEN_EOF, clox_scan(&scanner).type);

  clox_scanner_free(&scanner);
}

UTEST(token_types, every_token_type_has_a_name) {
  for (size_t type = 0; type < TOKEN_TYPE_COUNT; type++) {
    ASSERT_TRUE(clox_token_type_names[type] != NULL);
    ASSERT_TRUE(strncmp(clox_token_type_names[type], "TOKEN_", 6) == 0);
    ASSERT_TRUE(strlen(clox_token_type_names[type]) > 6);
  }
}

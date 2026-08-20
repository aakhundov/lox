#include <stdarg.h>
#include <string.h>

#include <utest.h>

#include "error.h"

#define OVER_LIMIT ((size_t)MAX_ERROR_LENGTH * 2)

// clox_format_error takes a va_list, so a test needs a variadic caller.
__attribute__((format(printf, 2, 3))) static int format(char (*buffer)[MAX_ERROR_LENGTH + 1],
                                                        const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int length = clox_format_error(buffer, fmt, ap);
  va_end(ap);

  return length;
}

UTEST(error, a_message_is_formatted_and_its_length_returned) {
  char buffer[MAX_ERROR_LENGTH + 1];

  EXPECT_EQ(15, format(&buffer, "%s got %d", "expected", 42));
  EXPECT_STREQ("expected got 42", buffer + 0);
}

UTEST(error, an_empty_message_stays_empty) {
  char buffer[MAX_ERROR_LENGTH + 1];

  EXPECT_EQ(0, format(&buffer, "%s", ""));
  EXPECT_STREQ("", buffer + 0);
}

UTEST(error, a_message_of_exactly_the_limit_is_kept_whole) {
  char buffer[MAX_ERROR_LENGTH + 1];
  char message[MAX_ERROR_LENGTH + 1];
  memset(message, 'x', MAX_ERROR_LENGTH);
  message[MAX_ERROR_LENGTH] = '\0';

  EXPECT_EQ(MAX_ERROR_LENGTH, format(&buffer, "%s", message));
  EXPECT_EQ((size_t)MAX_ERROR_LENGTH, strlen(buffer));
  EXPECT_STREQ(message, buffer + 0);
}

UTEST(error, a_longer_message_is_cut_to_the_limit_and_marked) {
  char buffer[MAX_ERROR_LENGTH + 1];
  char message[OVER_LIMIT + 1];
  memset(message, 'x', OVER_LIMIT);
  message[OVER_LIMIT] = '\0';

  EXPECT_EQ(MAX_ERROR_LENGTH, format(&buffer, "%s", message));
  ASSERT_EQ((size_t)MAX_ERROR_LENGTH, strlen(buffer));
  EXPECT_STREQ("...", buffer + MAX_ERROR_LENGTH - 3);
  EXPECT_EQ('x', buffer[MAX_ERROR_LENGTH - 4]);
}

UTEST(error, a_message_one_character_over_the_limit_is_cut_to_it) {
  char buffer[MAX_ERROR_LENGTH + 1];
  char message[MAX_ERROR_LENGTH + 2];
  memset(message, 'x', MAX_ERROR_LENGTH + 1);
  message[MAX_ERROR_LENGTH + 1] = '\0';

  // one character over the limit
  EXPECT_EQ(MAX_ERROR_LENGTH, format(&buffer, "%s", message));
  EXPECT_EQ((size_t)MAX_ERROR_LENGTH, strlen(buffer));
}

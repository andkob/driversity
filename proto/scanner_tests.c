#include "scanner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static void fail(const char *test_name, const char *message) {
  fprintf(stderr, "%s: %s\n", test_name, message);
  exit(1);
}

static void expect_int(const char *test_name, ssize_t actual, ssize_t expected) {
  if (actual != expected) {
    fprintf(stderr, "%s: expected %zd, got %zd\n", test_name, expected, actual);
    exit(1);
  }
}

static void expect_bytes(const char *test_name,
                         const unsigned char *actual,
                         const unsigned char *expected,
                         size_t len) {
  if (memcmp(actual, expected, len) != 0) {
    fail(test_name, "byte sequence mismatch");
  }
}

static void test_basic_colon_split(void) {
  const char *name = "basic_colon_split";
  Scanner scanner;
  unsigned char buf[8];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ":", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "alpha:beta", 10) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 5);
  expect_bytes(name, buf, (const unsigned char *)"alpha", 5);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 4);
  expect_bytes(name, buf, (const unsigned char *)"beta", 4);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_long_token_multiple_reads(void) {
  const char *name = "long_token_multiple_reads";
  Scanner scanner;
  unsigned char buf[4];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ":", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "alphabet", 8) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 4);
  expect_bytes(name, buf, (const unsigned char *)"alph", 4);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 4);
  expect_bytes(name, buf, (const unsigned char *)"abet", 4);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_exact_buffer_length_token(void) {
  const char *name = "exact_buffer_length_token";
  Scanner scanner;
  unsigned char buf[5];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ",", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "abcde", 5) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 5);
  expect_bytes(name, buf, (const unsigned char *)"abcde", 5);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_default_whitespace_separators(void) {
  const char *name = "default_whitespace_separators";
  Scanner scanner;
  unsigned char buf[8];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_data(&scanner, "alpha beta\tgamma", 16) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 5);
  expect_bytes(name, buf, (const unsigned char *)"alpha", 5);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 4);
  expect_bytes(name, buf, (const unsigned char *)"beta", 4);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 5);
  expect_bytes(name, buf, (const unsigned char *)"gamma", 5);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_leading_trailing_and_repeated_separators(void) {
  const char *name = "leading_trailing_and_repeated_separators";
  Scanner scanner;
  unsigned char buf[8];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ":", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "::alpha:::beta::", 16) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 5);
  expect_bytes(name, buf, (const unsigned char *)"alpha", 5);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 4);
  expect_bytes(name, buf, (const unsigned char *)"beta", 4);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_all_separators_means_no_tokens(void) {
  const char *name = "all_separators_means_no_tokens";
  Scanner scanner;
  unsigned char buf[4];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ":", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "::::", 4) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);
  scanner_destroy(&scanner);
}

static void test_empty_input_means_end_of_data(void) {
  const char *name = "empty_input_means_end_of_data";
  Scanner scanner;
  unsigned char buf[4];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_data(&scanner, "", 0) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);
  scanner_destroy(&scanner);
}

static void test_empty_separator_set_keeps_whole_input(void) {
  const char *name = "empty_separator_set_keeps_whole_input";
  Scanner scanner;
  unsigned char buf[32];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, "", 0) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "a b:c", 5) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 5);
  expect_bytes(name, buf, (const unsigned char *)"a b:c", 5);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_embedded_nul_in_input(void) {
  const char *name = "embedded_nul_in_input";
  Scanner scanner;
  unsigned char buf[8];
  const unsigned char data[] = { 'a', 0x00, 'b', ':', 'c' };

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ":", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, data, sizeof(data)) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 3);
  expect_bytes(name, buf, data, 3);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 1);
  expect_bytes(name, buf, (const unsigned char *)"c", 1);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_embedded_nul_in_separator_set(void) {
  const char *name = "embedded_nul_in_separator_set";
  Scanner scanner;
  unsigned char buf[8];
  const unsigned char separators[] = { 0x00 };
  const unsigned char data[] = { 'a', 0x00, 'b' };

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, separators, sizeof(separators)) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, data, sizeof(data)) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 1);
  expect_bytes(name, buf, (const unsigned char *)"a", 1);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 1);
  expect_bytes(name, buf, (const unsigned char *)"b", 1);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_repeated_data_load_resets_state(void) {
  const char *name = "repeated_data_load_resets_state";
  Scanner scanner;
  unsigned char buf[8];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ":", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "alpha:beta", 10) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, 3), 3);
  expect_bytes(name, buf, (const unsigned char *)"alp", 3);

  if (scanner_set_data(&scanner, "z", 1) < 0) {
    fail(name, "scanner_set_data reset failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 1);
  expect_bytes(name, buf, (const unsigned char *)"z", 1);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_multiple_instances_are_independent(void) {
  const char *name = "multiple_instances_are_independent";
  Scanner left;
  Scanner right;
  unsigned char left_buf[8];
  unsigned char right_buf[8];

  if (scanner_init(&left) < 0 || scanner_init(&right) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&left, ":", 1) < 0 ||
      scanner_set_separators(&right, ",", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&left, "a:b", 3) < 0 ||
      scanner_set_data(&right, "x,y", 3) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&left, left_buf, sizeof(left_buf)), 1);
  expect_bytes(name, left_buf, (const unsigned char *)"a", 1);
  expect_int(name, scanner_read_token_chunk(&right, right_buf, sizeof(right_buf)), 1);
  expect_bytes(name, right_buf, (const unsigned char *)"x", 1);
  expect_int(name, scanner_read_token_chunk(&left, left_buf, sizeof(left_buf)), 0);
  expect_int(name, scanner_read_token_chunk(&right, right_buf, sizeof(right_buf)), 0);
  expect_int(name, scanner_read_token_chunk(&left, left_buf, sizeof(left_buf)), 1);
  expect_bytes(name, left_buf, (const unsigned char *)"b", 1);
  expect_int(name, scanner_read_token_chunk(&right, right_buf, sizeof(right_buf)), 1);
  expect_bytes(name, right_buf, (const unsigned char *)"y", 1);

  scanner_destroy(&left);
  scanner_destroy(&right);
}

static void test_zero_length_read_returns_zero_for_active_token(void) {
  const char *name = "zero_length_read_returns_zero_for_active_token";
  Scanner scanner;
  unsigned char buf[4];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }
  if (scanner_set_separators(&scanner, ":", 1) < 0) {
    fail(name, "scanner_set_separators failed");
  }
  if (scanner_set_data(&scanner, "abc", 3) < 0) {
    fail(name, "scanner_set_data failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, 0), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 3);
  expect_bytes(name, buf, (const unsigned char *)"abc", 3);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), 0);
  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);

  scanner_destroy(&scanner);
}

static void test_read_before_data_load_returns_end_of_data(void) {
  const char *name = "read_before_data_load_returns_end_of_data";
  Scanner scanner;
  unsigned char buf[4];

  if (scanner_init(&scanner) < 0) {
    fail(name, "scanner_init failed");
  }

  expect_int(name, scanner_read_token_chunk(&scanner, buf, sizeof(buf)), -1);
  scanner_destroy(&scanner);
}

int main(void) {
  void (*tests[])(void) = {
    test_basic_colon_split,
    test_long_token_multiple_reads,
    test_exact_buffer_length_token,
    test_default_whitespace_separators,
    test_leading_trailing_and_repeated_separators,
    test_all_separators_means_no_tokens,
    test_empty_input_means_end_of_data,
    test_empty_separator_set_keeps_whole_input,
    test_embedded_nul_in_input,
    test_embedded_nul_in_separator_set,
    test_repeated_data_load_resets_state,
    test_multiple_instances_are_independent,
    test_zero_length_read_returns_zero_for_active_token,
    test_read_before_data_load_returns_end_of_data
  };

  for (size_t i = 0; i < ARRAY_LEN(tests); ++i) {
    tests[i]();
  }

  puts("scanner_tests: all tests passed");
  return 0;
}

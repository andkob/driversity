#include "scanner.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char default_separators[] = { ' ', '\t', '\n', '\r', '\f', '\v' };

static void reset_separator_map(Scanner *scanner) {
  memset(scanner->separator_map, 0, sizeof(scanner->separator_map));
  for (size_t i = 0; i < scanner->separators_len; ++i) {
    scanner->separator_map[scanner->separators[i]] = 1;
  }
}

static int replace_buffer(unsigned char **dst, size_t *dst_len, const void *src, size_t len) {
  unsigned char *copy = NULL;

  if (len > 0) {
    copy = malloc(len);
    if (!copy) {
      return -ENOMEM;
    }
    memcpy(copy, src, len);
  }

  free(*dst);
  *dst = copy;
  *dst_len = len;
  return 0;
}

static void reset_scan_state(Scanner *scanner) {
  scanner->cursor = 0;
  scanner->token_start = 0;
  scanner->token_end = 0;
  scanner->token_cursor = 0;
  scanner->token_active = 0;
  scanner->pending_token_end = 0;
}

static void start_next_token(Scanner *scanner) {
  /* Skip leading separators; they never produce empty tokens. */
  while (scanner->cursor < scanner->data_len &&
         scanner->separator_map[scanner->data[scanner->cursor]]) {
    scanner->cursor++;
  }

  if (scanner->cursor >= scanner->data_len) {
    scanner->token_active = 0;
    return;
  }

  scanner->token_start = scanner->cursor;
  /* Extend until the next separator or end-of-data. */
  while (scanner->cursor < scanner->data_len &&
         !scanner->separator_map[scanner->data[scanner->cursor]]) {
    scanner->cursor++;
  }

  scanner->token_end = scanner->cursor;
  scanner->token_cursor = scanner->token_start;
  scanner->token_active = 1;
}

int scanner_init(Scanner *scanner) {
  memset(scanner, 0, sizeof(*scanner));
  return scanner_set_separators(scanner, default_separators, sizeof(default_separators));
}

int scanner_set_separators(Scanner *scanner, const void *buf, size_t len) {
  if (len > 0 && !buf) {
    return -EINVAL;
  }

  int rc = replace_buffer(&scanner->separators, &scanner->separators_len, buf, len);
  if (rc < 0) {
    return rc;
  }

  reset_separator_map(scanner);
  return 0;
}

int scanner_set_data(Scanner *scanner, const void *buf, size_t len) {
  if (len > 0 && !buf) {
    return -EINVAL;
  }

  int rc = replace_buffer(&scanner->data, &scanner->data_len, buf, len);
  if (rc < 0) {
    return rc;
  }

  reset_scan_state(scanner);
  return 0;
}

ssize_t scanner_read_token_chunk(Scanner *scanner, void *buf, size_t count) {
  unsigned char *out = buf;

  if (!buf && count > 0) {
    errno = EINVAL;
    return -1;
  }

  if (count == 0) {
    return 0;
  }

  /* After the final token chunk, the assignment wants one explicit 0 read. */
  if (scanner->pending_token_end) {
    scanner->pending_token_end = 0;
    return 0;
  }

  if (!scanner->token_active) {
    start_next_token(scanner);
    if (!scanner->token_active) {
      return -1;
    }
  }

  size_t remaining = scanner->token_end - scanner->token_cursor;
  size_t chunk = remaining < count ? remaining : count;

  if (chunk > 0) {
    memcpy(out, scanner->data + scanner->token_cursor, chunk);
    scanner->token_cursor += chunk;
    if (scanner->token_cursor == scanner->token_end) {
      /* The next call should observe end-of-token before advancing again. */
      scanner->token_active = 0;
      scanner->pending_token_end = 1;
    }
    return (ssize_t)chunk;
  }

  scanner->token_active = 0;
  scanner->pending_token_end = 1;
  return 0;
}

void scanner_destroy(Scanner *scanner) {
  free(scanner->separators);
  free(scanner->data);
  memset(scanner, 0, sizeof(*scanner));
}

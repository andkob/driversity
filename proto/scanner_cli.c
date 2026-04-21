#define _POSIX_C_SOURCE 200809L

#include "scanner.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) {
  fprintf(stderr, "%s\n", message);
  exit(1);
}

static void read_text_mode(Scanner *scanner, size_t chunk_size) {
  unsigned char *buf = malloc(chunk_size + 1);
  if (!buf) {
    fail("malloc() failed");
  }

  for (;;) {
    ssize_t len = scanner_read_token_chunk(scanner, buf, chunk_size);
    if (len < 0) {
      break;
    }
    if (len == 0) {
      putchar('\n');
      continue;
    }
    buf[len] = '\0';
    fputs((const char *)buf, stdout);
  }

  free(buf);
}

static int hex_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return 10 + (c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return 10 + (c - 'A');
  }
  return -1;
}

static size_t parse_hex(const char *text, unsigned char **out) {
  size_t len = strlen(text);
  if ((len % 2) != 0) {
    fail("hex input must have an even number of digits");
  }

  unsigned char *buf = malloc(len / 2);
  if (!buf && len != 0) {
    fail("malloc() failed");
  }

  for (size_t i = 0; i < len; i += 2) {
    int hi = hex_value(text[i]);
    int lo = hex_value(text[i + 1]);
    if (hi < 0 || lo < 0) {
      free(buf);
      fail("invalid hex digit");
    }
    buf[i / 2] = (unsigned char)((hi << 4) | lo);
  }

  *out = buf;
  return len / 2;
}

static void print_hex_line(const unsigned char *buf, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    printf("%02x", buf[i]);
  }
  putchar('\n');
}

static void read_hex_mode(Scanner *scanner, size_t chunk_size) {
  unsigned char *buf = malloc(chunk_size == 0 ? 1 : chunk_size);
  if (!buf) {
    fail("malloc() failed");
  }

  for (;;) {
    ssize_t len = scanner_read_token_chunk(scanner, buf, chunk_size);
    if (len < 0) {
      puts("EOF");
      break;
    }
    if (len == 0) {
      puts("EOT");
      continue;
    }
    print_hex_line(buf, (size_t)len);
  }

  free(buf);
}

int main(int argc, char **argv) {
  const char *separator_text = NULL;
  const char *data_hex = NULL;
  const char *separator_hex = NULL;
  size_t chunk_size = 8;
  int hex_mode = 0;

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--separators") == 0 && i + 1 < argc) {
      separator_text = argv[++i];
    } else if (strcmp(argv[i], "--separator-hex") == 0 && i + 1 < argc) {
      separator_hex = argv[++i];
    } else if (strcmp(argv[i], "--data-hex") == 0 && i + 1 < argc) {
      data_hex = argv[++i];
    } else if (strcmp(argv[i], "--chunk-size") == 0 && i + 1 < argc) {
      char *end = NULL;
      unsigned long parsed = strtoul(argv[++i], &end, 10);
      if (!end || *end != '\0') {
        fail("invalid chunk size");
      }
      chunk_size = (size_t)parsed;
    } else if (strcmp(argv[i], "--hex-output") == 0) {
      hex_mode = 1;
    } else {
      fprintf(stderr,
              "usage: %s [--separators TEXT | --separator-hex HEX] "
              "[--data-hex HEX] [--chunk-size N] [--hex-output]\n",
              argv[0]);
      return 1;
    }
  }

  Scanner scanner;
  if (scanner_init(&scanner) < 0) {
    fail("scanner_init() failed");
  }

  if (separator_text && separator_hex) {
    scanner_destroy(&scanner);
    fail("choose either --separators or --separator-hex");
  }

  if (separator_text) {
    if (scanner_set_separators(&scanner, separator_text, strlen(separator_text)) < 0) {
      scanner_destroy(&scanner);
      fail("scanner_set_separators() failed");
    }
  } else if (separator_hex) {
    unsigned char *separator_buf = NULL;
    size_t separator_len = parse_hex(separator_hex, &separator_buf);
    if (scanner_set_separators(&scanner, separator_buf, separator_len) < 0) {
      free(separator_buf);
      scanner_destroy(&scanner);
      fail("scanner_set_separators() failed");
    }
    free(separator_buf);
  }

  if (data_hex) {
    unsigned char *data_buf = NULL;
    size_t data_len = parse_hex(data_hex, &data_buf);
    if (scanner_set_data(&scanner, data_buf, data_len) < 0) {
      free(data_buf);
      scanner_destroy(&scanner);
      fail("scanner_set_data() failed");
    }
    free(data_buf);
    read_hex_mode(&scanner, chunk_size);
  } else {
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;

    while ((len = getline(&line, &cap, stdin)) >= 0) {
      if (len > 0 && line[len - 1] == '\n') {
        len--;
      }
      if (scanner_set_data(&scanner, line, (size_t)len) < 0) {
        free(line);
        scanner_destroy(&scanner);
        fail("scanner_set_data() failed");
      }
      if (hex_mode) {
        read_hex_mode(&scanner, chunk_size);
      } else {
        read_text_mode(&scanner, chunk_size);
      }
    }

    free(line);
  }

  scanner_destroy(&scanner);
  return 0;
}

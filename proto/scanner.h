#ifndef SCANNER_H
#define SCANNER_H

#include <stddef.h>
#include <sys/types.h>

/**
 * Stateful byte-oriented scanner prototype that mirrors the intended driver
 * behavior in user space.
 *
 * All separator and input handling is length-based. No byte value, including
 * NUL, is treated specially unless it appears in the separator set.
 */
typedef struct {
  unsigned char separator_map[256];
  unsigned char *separators;
  size_t separators_len;
  unsigned char *data;
  size_t data_len;
  size_t cursor;
  size_t token_start;
  size_t token_end;
  size_t token_cursor;
  int token_active;
  int pending_token_end;
} Scanner;

/**
 * Initialize a scanner instance with the default separator set.
 *
 * The default set is conventional ASCII whitespace:
 * space, tab, newline, carriage return, form feed, and vertical tab.
 *
 * @param scanner Scanner instance to initialize.
 * @return 0 on success, or a negative errno-style value on failure.
 */
int scanner_init(Scanner *scanner);

/**
 * Replace the scanner's active separator set.
 *
 * This updates the byte membership table used during tokenization but does not
 * alter the current input buffer or current scan position.
 *
 * @param scanner Scanner instance to update.
 * @param buf Pointer to the new separator bytes. May be NULL only when len is 0.
 * @param len Number of separator bytes in buf.
 * @return 0 on success, or a negative errno-style value on failure.
 */
int scanner_set_separators(Scanner *scanner, const void *buf, size_t len);

/**
 * Replace the scanner's current input sequence.
 *
 * Input writes are not cumulative. Each call discards the previous input and
 * resets tokenization state so scanning starts from the beginning of the new
 * byte sequence.
 *
 * @param scanner Scanner instance to update.
 * @param buf Pointer to the new input bytes. May be NULL only when len is 0.
 * @param len Number of input bytes in buf.
 * @return 0 on success, or a negative errno-style value on failure.
 */
int scanner_set_data(Scanner *scanner, const void *buf, size_t len);

/**
 * Read the next chunk of token data from the current input sequence.
 *
 * Return semantics intentionally match the assignment:
 * positive value = token bytes copied into buf
 * zero = end of the current token
 * -1 = end of data
 *
 * If the caller buffer is smaller than the token, repeated calls continue
 * returning more bytes from the same token until the token is exhausted.
 *
 * @param scanner Scanner instance to read from.
 * @param buf Destination buffer for token bytes. May be NULL only when count is 0.
 * @param count Maximum number of bytes to copy into buf.
 * @return Positive byte count, 0 for end-of-token, or -1 for end-of-data.
 */
ssize_t scanner_read_token_chunk(Scanner *scanner, void *buf, size_t count);

/**
 * Release all heap-owned scanner state and zero the structure.
 *
 * @param scanner Scanner instance to destroy.
 */
void scanner_destroy(Scanner *scanner);

#endif

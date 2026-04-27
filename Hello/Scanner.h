#ifndef SCANNER_H
#define SCANNER_H

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/types.h>

/**
 * Default separator bytes used for a newly opened scanner device.
 *
 * The initial separator set matches ordinary whitespace so that a new open
 * behaves like a basic token scanner until user space replaces the separators
 * with ioctl(fd, 0, 0) followed by write().
 */
extern const unsigned char default_separators[];

/**
 * Per-module device state.
 *
 * There is exactly one Device instance for this kernel module. It owns the
 * registered device number and the embedded cdev used by the VFS layer.
 */
typedef struct
{
    /** Kernel-assigned major/minor number pair. */
    dev_t devno;
    /** Character-device object registered with the kernel. */
    struct cdev cdev;
} Device;

/**
 * Per-open scanner state.
 *
 * Each open file descriptor gets its own File object so separators, buffered
 * input data, and tokenization progress stay independent across opens.
 */
typedef struct
{
    /** O(1) membership table for separator bytes. */
    unsigned char separator_map[256];
    /** Current separator set for this open file. */
    unsigned char *separators;
    /** Number of bytes stored in separators. */
    size_t separators_len;
    /** Current input buffer waiting to be tokenized. */
    unsigned char *data;
    /** Number of bytes stored in data. */
    size_t data_len;
    /** Scan cursor used to locate the next token. */
    size_t cursor;
    /** Inclusive start offset of the active token. */
    size_t token_start;
    /** Exclusive end offset of the active token. */
    size_t token_end;
    /** Current output position within the active token. */
    size_t token_cursor;
    /** Nonzero while a token is currently active. */
    int token_active;
    /** Nonzero when the next read must return end-of-token (0). */
    int pending_token_end;
    /** Nonzero when ioctl() has marked the next write as a separator update. */
    int next_write_sets_separators;
} File;

/** Rebuilds separator_map from the current separators buffer. */
static void reset_separator_map(File *file);

/** Replaces a heap-owned byte buffer with a newly copied buffer. */
static int replace_buffer(unsigned char **dst,
                          size_t *dst_len,
                          const void *src,
                          size_t len);

/** Clears tokenization progress so scanning restarts from the new data. */
static void reset_scan_state(File *file);

/** Installs a new separator buffer and refreshes the separator lookup table. */
static int set_separators(File *file,
                          const void *buf,
                          size_t len);

/** Installs a new data buffer and resets tokenization state. */
static int set_data(File *file,
                    const void *buf,
                    size_t len);

/** Advances the scanner to the next token boundaries, if one exists. */
static void start_next_token(File *file);

/**
 * Produces the next scanner result.
 *
 * Returns a positive byte count for token data, 0 for end-of-token, and -1
 * for end-of-data to match the assignment contract.
 */
static ssize_t read_token_chunk(File *file,
                                void *buf,
                                size_t count);

/** Releases any dynamic storage owned by a File object. */
static void destroy_file_state(File *file);

/** open() handler for /dev/Scanner. */
static int open(struct inode *inode, struct file *filp);

/** release() handler for /dev/Scanner. */
static int release(struct inode *inode, struct file *filp);

/** read() handler for /dev/Scanner. */
static ssize_t read(struct file *filp,
                    char *buf,
                    size_t count,
                    loff_t *f_pos);

/** write() handler for /dev/Scanner. */
static ssize_t write(struct file *filp,
                     const char *buf,
                     size_t count,
                     loff_t *f_pos);

/** ioctl() handler for /dev/Scanner. */
static long ioctl(struct file *filp,
                  unsigned int cmd,
                  unsigned long arg);

/** Module load entry point. */
static int __init my_init(void);

/** Module unload entry point. */
static void __exit my_exit(void);

#endif

/*
 * Core module macros and metadata support:
 * - MODULE_LICENSE / MODULE_DESCRIPTION / MODULE_AUTHOR
 * - module_init / module_exit
 * - THIS_MODULE
 */
#include <linux/module.h>
/*
 * Kernel logging support, including printk() and log-level tags such as
 * KERN_ERR and KERN_INFO.
 */
#include <linux/kernel.h>
/*
 * __init and __exit annotations. These tell the kernel that init code is only
 * needed while the module is loading, and exit code is only needed while the
 * module is unloading.
 */
#include <linux/init.h>
/*
 * Kernel heap allocation:
 * - kmalloc() allocates memory from kernel space
 * - kfree() frees it
 * This header is the rough kernel-space equivalent of using malloc/free from
 * stdlib in a normal C program.
 */
#include <linux/slab.h>
/*
 * File and device infrastructure:
 * - dev_t device numbers
 * - struct file / struct inode
 * - alloc_chrdev_region() / unregister_chrdev_region()
 * - struct file_operations
 */
#include <linux/fs.h>
/*
 * Safe copying between kernel memory and user memory:
 * - copy_to_user()
 * - copy_from_user()
 * User pointers cannot be dereferenced directly inside the kernel.
 */
#include <linux/uaccess.h>
/*
 * Character-device registration helpers:
 * - struct cdev
 * - cdev_init()
 * - cdev_add()
 * - cdev_del()
 */
#include <linux/cdev.h>
#include "Scanner.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BSU CS 452 HW5");
MODULE_AUTHOR("<buff@cs.boisestate.edu>");

const unsigned char default_separators[]={
  ' ','\t','\n','\r','\f','\v'
};

/* Single global device instance for this module. */
static Device device;

static void reset_separator_map(File *file) {
  size_t i;

  memset(file->separator_map,0,sizeof(file->separator_map));
  // Mark each active separator byte for O(1) membership checks during scans.
  for (i=0; i<file->separators_len; i++)
    file->separator_map[file->separators[i]]=1;
}

static int replace_buffer(unsigned char **dst,
    size_t *dst_len,
    const void *src,
    size_t len) {
  unsigned char *copy=NULL;

  if (len>0) {
    copy=(unsigned char *)kmalloc(len,GFP_KERNEL);
    if (!copy)
      return -ENOMEM;
    // Keep kernel-owned copies so later reads do not depend on user memory.
    memcpy(copy,src,len);
  }

  kfree(*dst);
  *dst=copy;
  *dst_len=len;
  return 0;
}

static void reset_scan_state(File *file) {
  // A fresh data write always restarts tokenization from the beginning.
  file->cursor=0;
  file->token_start=0;
  file->token_end=0;
  file->token_cursor=0;
  file->token_active=0;
  file->pending_token_end=0;
}

static int set_separators(File *file,
    const void *buf,
    size_t len) {
  int err;

  if (len>0 && !buf)
    return -EINVAL;

  err=replace_buffer(&file->separators,&file->separators_len,buf,len);
  if (err)
    return err;

  reset_separator_map(file);
  return 0;
}

static int set_data(File *file,
    const void *buf,
    size_t len) {
  int err;

  if (len>0 && !buf)
    return -EINVAL;

  err=replace_buffer(&file->data,&file->data_len,buf,len);
  if (err)
    return err;

  reset_scan_state(file);
  return 0;
}

static void start_next_token(File *file) {
  // Skip leading separators until we either find token data or hit end-of-data.
  while (file->cursor<file->data_len &&
      file->separator_map[file->data[file->cursor]])
    file->cursor++;

  if (file->cursor>=file->data_len) {
    file->token_active=0;
    return;
  }

  file->token_start=file->cursor;
  // Advance until the next separator to capture one whole token span.
  while (file->cursor<file->data_len &&
      !file->separator_map[file->data[file->cursor]])
    file->cursor++;

  file->token_end=file->cursor;
  file->token_cursor=file->token_start;
  file->token_active=1;
}

static ssize_t read_token_chunk(File *file,
    void *buf,
    size_t count) {
  unsigned char *out=(unsigned char *)buf;
  size_t remaining;
  size_t chunk;

  if (count==0)
    return 0;

  // The scanner contract is: >0 for token bytes, 0 for end-of-token, -1 for end-of-data.
  if (file->pending_token_end) {
    // Emit the delayed end-of-token marker immediately after a token finishes.
    file->pending_token_end=0;
    return 0;
  }

  if (!file->token_active) {
    // No token is active, so scan forward to the next token boundary pair.
    start_next_token(file);
    if (!file->token_active)
      return -1;
  }

  remaining=file->token_end-file->token_cursor;
  chunk=(remaining<count ? remaining : count);

  if (chunk>0) {
    // Copy out the next contiguous slice of the current token.
    memcpy(out,file->data+file->token_cursor,chunk);
    file->token_cursor+=chunk;
    if (file->token_cursor==file->token_end) {
      // The next read after the last byte must report end-of-token.
      file->token_active=0;
      file->pending_token_end=1;
    }
    return chunk;
  }

  // A zero-sized remainder still means we owe the caller an end-of-token.
  file->token_active=0;
  file->pending_token_end=1;
  return 0;
}

static void destroy_file_state(File *file) {
  kfree(file->separators);
  kfree(file->data);
  memset(file,0,sizeof(*file));
}

static int open(struct inode *inode, struct file *filp) {
  /*
   * open() runs when a user-space program opens /dev/Scanner.
   *
   * The kernel passes us a struct file representing this particular open file
   * descriptor. We create a File object so this open instance has private
   * state that can be kept separate from other opens.
   */
  File *file=(File *)kmalloc(sizeof(*file),GFP_KERNEL);
  if (!file) {
    printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
    return -ENOMEM;
  }
  memset(file,0,sizeof(*file));

  /*
   * Each open file gets its own separator set and scan state.
   * Start with the default whitespace separators and no loaded data.
   */
  if (set_separators(file,default_separators,sizeof(default_separators))) {
    printk(KERN_ERR "%s: set_separators() failed\n",DEVNAME);
    kfree(file);
    return -ENOMEM;
  }
  if (set_data(file,NULL,0)) {
    printk(KERN_ERR "%s: set_data() failed\n",DEVNAME);
    destroy_file_state(file);
    kfree(file);
    return -ENOMEM;
  }

  /*
   * private_data is the standard place to hang driver-specific state off of a
   * particular open file. read()/write()/ioctl()/release() can recover it
   * later through filp->private_data.
   */
  filp->private_data=file;
  printk(KERN_INFO
      "%s: open data_len=%zu separators_len=%zu cursor=%zu token_active=%d\n",
      DEVNAME,
      file->data_len,
      file->separators_len,
      file->cursor,
      file->token_active);
  return 0;
}

static int release(struct inode *inode, struct file *filp) {
  /*
  * release() runs when the file descriptor is closed.
  * Free anything allocated in open() for this file instance.
  */
  File *file=filp->private_data;
  printk(KERN_INFO
      "%s: release data_len=%zu separators_len=%zu cursor=%zu token_active=%d pending_token_end=%d\n",
      DEVNAME,
      file->data_len,
      file->separators_len,
      file->cursor,
      file->token_active,
      file->pending_token_end);
  destroy_file_state(file);
  kfree(file);
  return 0;
}

static ssize_t read(struct file *filp,
    char *buf,
    size_t count,
    loff_t *f_pos) { 
  /*
   * read() is invoked when user space calls read(fd, ...).
   *
   * buf is a USER-SPACE pointer, so kernel code must not write to it directly.
   * Instead we use copy_to_user(), declared in <linux/uaccess.h>.
   */
  File *file=filp->private_data;
  unsigned char *tmp;
  ssize_t n;
  (void)f_pos;

  if (count==0)
    return 0;

  printk(KERN_INFO
      "%s: read enter count=%zu data_len=%zu cursor=%zu token_active=%d token_cursor=%zu token_end=%zu pending_token_end=%d\n",
      DEVNAME,
      count,
      file->data_len,
      file->cursor,
      file->token_active,
      file->token_cursor,
      file->token_end,
      file->pending_token_end);

  tmp=(unsigned char *)kmalloc(count,GFP_KERNEL);
  if (!tmp) {
    printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
    return -ENOMEM;
  }

  n=read_token_chunk(file,tmp,count);
  if (n<=0) {
    printk(KERN_INFO
        "%s: read exit count=%zu result=%zd data_len=%zu cursor=%zu token_active=%d token_cursor=%zu token_end=%zu pending_token_end=%d\n",
        DEVNAME,
        count,
        n,
        file->data_len,
        file->cursor,
        file->token_active,
        file->token_cursor,
        file->token_end,
        file->pending_token_end);
    kfree(tmp);
    return n;
  }

  if (copy_to_user(buf,tmp,n)) {
    kfree(tmp);
    printk(KERN_ERR "%s: copy_to_user() failed\n",DEVNAME);
    return -EFAULT;
  }
  kfree(tmp);
  printk(KERN_INFO
      "%s: read exit count=%zu result=%zd data_len=%zu cursor=%zu token_active=%d token_cursor=%zu token_end=%zu pending_token_end=%d\n",
      DEVNAME,
      count,
      n,
      file->data_len,
      file->cursor,
      file->token_active,
      file->token_cursor,
      file->token_end,
      file->pending_token_end);

  /* read_token_chunk() owns the token/end-of-token/end-of-data contract. */
  return n;
}

static ssize_t write(struct file *filp,
    const char *buf,
    size_t count,
    loff_t *f_pos) {
  File *file=filp->private_data;
  unsigned char *tmp=NULL;
  int err;
  (void)f_pos;

  printk(KERN_INFO
      "%s: write enter count=%zu mode=%s data_len=%zu separators_len=%zu cursor=%zu token_active=%d\n",
      DEVNAME,
      count,
      (file->next_write_sets_separators ? "separators" : "data"),
      file->data_len,
      file->separators_len,
      file->cursor,
      file->token_active);

  if (count>0) {
    tmp=(unsigned char *)kmalloc(count,GFP_KERNEL);
    if (!tmp) {
      printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
      return -ENOMEM;
    }
    if (copy_from_user(tmp,buf,count)) {
      kfree(tmp);
      printk(KERN_ERR "%s: copy_from_user() failed\n",DEVNAME);
      return -EFAULT;
    }
  }

  if (file->next_write_sets_separators) {
    // ioctl(cmd=0,arg=0) makes exactly one subsequent write replace separators.
    err=set_separators(file,tmp,count);
    file->next_write_sets_separators=0;
    if (err) {
      kfree(tmp);
      printk(KERN_ERR "%s: set_separators() failed\n",DEVNAME);
      return err;
    }
    printk(KERN_INFO
        "%s: write exit mode=separators count=%zu data_len=%zu separators_len=%zu cursor=%zu token_active=%d\n",
        DEVNAME,
        count,
        file->data_len,
        file->separators_len,
        file->cursor,
        file->token_active);
  } else {
    // Ordinary writes replace the current input data buffer.
    err=set_data(file,tmp,count);
    if (err) {
      kfree(tmp);
      printk(KERN_ERR "%s: set_data() failed\n",DEVNAME);
      return err;
    }
    printk(KERN_INFO
        "%s: write exit mode=data count=%zu data_len=%zu separators_len=%zu cursor=%zu token_active=%d\n",
        DEVNAME,
        count,
        file->data_len,
        file->separators_len,
        file->cursor,
        file->token_active);
  }

  kfree(tmp);
  return count;
}

static long ioctl(struct file *filp,
    unsigned int cmd,
    unsigned long arg) {
  File *file=filp->private_data;

  /*
   * ioctl() is a hook for "device-specific commands" that do not fit well into
   * plain read()/write().
   *
   * For this assignment, ioctl(cmd=0) means the next write() call should be
   * interpreted as a replacement separator set instead of input data.
   */
  if (cmd!=0)
    return -EINVAL;

  if (arg!=0)
    return -EINVAL;

  file->next_write_sets_separators=1;
  printk(KERN_INFO
      "%s: ioctl cmd=%u arg=%lu next_write_sets_separators=%d data_len=%zu separators_len=%zu\n",
      DEVNAME,
      cmd,
      arg,
      file->next_write_sets_separators,
      file->data_len,
      file->separators_len);
  return 0;
}

/*
 * file_operations is the dispatch table that tells the kernel which functions
 * implement this device's behavior.
 *
 * When a user program opens /dev/Scanner, the kernel looks here and calls
 * ops.open. When it reads, the kernel calls ops.read, and so on.
 */
static struct file_operations ops={
  .open=open,
  .release=release,
  .read=read,
  .write=write,
  .unlocked_ioctl=ioctl,
  .owner=THIS_MODULE
};

static int __init my_init(void) {
  /*
   * my_init() runs once when the module is loaded with insmod/modprobe.
   * Its job is to prepare the device and register it with the kernel.
   */
  int err;

  /*
   * Ask the kernel for a free character-device number.
   *
   * alloc_chrdev_region() comes from <linux/fs.h>.
   * Parameters:
   * - &device.devno: where the kernel writes the allocated dev_t
   * - 0: first minor number
   * - 1: number of minors requested
   * - DEVNAME: human-readable device name
   *
   * This reserves an identity for our device inside the kernel.
   */
  err=alloc_chrdev_region(&device.devno,0,1,DEVNAME);
  if (err<0) {
    printk(KERN_ERR "%s: alloc_chrdev_region() failed\n",DEVNAME);
    return err;
  }

  /*
   * Initialize the embedded cdev object so it points at our file_operations
   * table.
   */
  cdev_init(&device.cdev,&ops);
  device.cdev.owner=THIS_MODULE;

  /*
   * Finish registering the character device with the kernel.
   *
   * cdev_add() comes from <linux/cdev.h>. After this succeeds, the kernel knows
   * that operations on the allocated device number should be routed to our
   * file_operations handlers.
   */
  err=cdev_add(&device.cdev,device.devno,1);
  if (err) {
    printk(KERN_ERR "%s: cdev_add() failed\n",DEVNAME);
    unregister_chrdev_region(device.devno,1);
    return err;
  }

  /*
   * printk() is the kernel version of logging. Messages show up in the kernel
   * log, e.g. via dmesg.
   */
  printk(KERN_INFO "%s: init\n",DEVNAME);
  return 0;
}

static void __exit my_exit(void) {
  /*
   * Undo everything done in my_init(), in reverse order.
   * This runs when the module is unloaded.
   */
  cdev_del(&device.cdev);
  unregister_chrdev_region(device.devno,1);
  printk(KERN_INFO "%s: exit\n",DEVNAME);
}

/* Register the load and unload entry points for this module. */
module_init(my_init);
module_exit(my_exit);

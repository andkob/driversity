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

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("BSU CS 452 HW5");
MODULE_AUTHOR("<buff@cs.boisestate.edu>");

typedef struct {
  /* Kernel-assigned device number: major + minor packed together. */
  dev_t devno;
  /* Embedded character-device object registered with the kernel. */
  struct cdev cdev;
  /* Module-global string used as the template for each open file. */
  char *s;
} Device;			/* per-init() data */

typedef struct {
  /* Per-open copy of the string. Each open file gets its own state. */
  char *s;
} File;				/* per-open() data */

/* Single global device instance for this module. */
static Device device;

static int open(struct inode *inode, struct file *filp) {
  /*
   * open() runs when a user-space program opens /dev/Hello.
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

  /*
   * Allocate kernel memory for this open file's private copy of the string.
   *
   * kmalloc() comes from <linux/slab.h>. It is a kernel allocator, not the
   * user-space malloc() from libc. We pass GFP_KERNEL to say "normal kernel
   * allocation; it may sleep if needed."
   */
  file->s=(char *)kmalloc(strlen(device.s)+1,GFP_KERNEL);
  if (!file->s) {
    printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
    kfree(file);
    return -ENOMEM;
  }

  /* Make this open file start with the module's default string contents. */
  strcpy(file->s,device.s);

  /*
   * private_data is the standard place to hang driver-specific state off of a
   * particular open file. read()/write()/ioctl()/release() can recover it
   * later through filp->private_data.
   */
  filp->private_data=file;
  return 0;
}

static int release(struct inode *inode, struct file *filp) {
  /*
   * release() runs when the file descriptor is closed.
   * Free anything allocated in open() for this file instance.
   */
  File *file=filp->private_data;
  kfree(file->s);
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
  int n=strlen(file->s);

  /* Return at most count bytes, because that is how many bytes the caller requested. */
  n=(n<count ? n : count);
  if (copy_to_user(buf,file->s,n)) {
    printk(KERN_ERR "%s: copy_to_user() failed\n",DEVNAME);
    return 0;
  }

  /*
   * Returning n tells the caller how many bytes were produced.
   *
   * This demo driver always returns the start of file->s again because it does
   * not use *f_pos to track an offset. It is intentionally simple as a starter
   * example.
   */
  return n;
}

static long ioctl(struct file *filp,
    unsigned int cmd,
    unsigned long arg) {
  /*
   * ioctl() is a hook for "device-specific commands" that do not fit well into
   * plain read()/write().
   *
   * In this Hello example it does nothing yet. In Scanner, this is the natural
   * place to support commands such as "the next write defines separators" or
   * "reset scanner state", depending on the assignment's design.
   */
  return 0;
}

/*
 * file_operations is the dispatch table that tells the kernel which functions
 * implement this device's behavior.
 *
 * When a user program opens /dev/Hello, the kernel looks here and calls
 * ops.open. When it reads, the kernel calls ops.read, and so on.
 */
static struct file_operations ops={
  .open=open,
  .release=release,
  .read=read,
  .unlocked_ioctl=ioctl,
  .owner=THIS_MODULE
};

static int __init my_init(void) {
  /*
   * my_init() runs once when the module is loaded with insmod/modprobe.
   * Its job is to prepare the device and register it with the kernel.
   */
  const char *s="Hello world!\n";
  int err;

  /* Allocate module-global storage for the default message. */
  device.s=(char *)kmalloc(strlen(s)+1,GFP_KERNEL);
  if (!device.s) {
    printk(KERN_ERR "%s: kmalloc() failed\n",DEVNAME);
    return -ENOMEM;
  }
  strcpy(device.s,s);

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
    kfree(device.s);
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
    kfree(device.s);
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
  kfree(device.s);
  printk(KERN_INFO "%s: exit\n",DEVNAME);
}

/* Register the load and unload entry points for this module. */
module_init(my_init);
module_exit(my_exit);

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>

#include "helloioctl.h"

MODULE_DESCRIPTION("Ioctl fuckery");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static struct file_operations fops;
static unsigned int major;

static long unlocked_ioctl(struct file *file, unsigned int request_nr, unsigned long buf)
{
  if (request_nr == HELLO) {
    return copy_to_user((void *) buf, "Hello ioctl!", 13);
  } else {
    return -ENOTTY;
  }
}

static int __init helloioctl_init(void)
{
  fops.unlocked_ioctl = unlocked_ioctl;
  major = register_chrdev(0, "hello", &fops);

  if (major < 0) {
    return major;
  }

  pr_info("%d\n", major);
  return 0;
}

module_init(helloioctl_init);

static void __exit helloioctl_exit(void)
{
  unregister_chrdev(major, "hello");
}

module_exit(helloioctl_exit);

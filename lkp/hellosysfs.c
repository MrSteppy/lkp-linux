#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

MODULE_DESCRIPTION("Sysfs fuckery");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static char what[256] = "sysfs";

static ssize_t hello_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{

  return sysfs_emit(buf, "Hello %s!\n", what);
}

static ssize_t hello_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
  strscpy(what, buf, 256);
  return count;
}

static struct kobj_attribute hello_attribute = __ATTR_RW(hello);

static int __init hellosysfs_init(void)
{

  int err = sysfs_create_file(kernel_kobj, &hello_attribute.attr);
  if (!err) {
    pr_info("Nya :3\n");
  }
  return err;
}

module_init(hellosysfs_init);

static void __exit hellosysfs_exit(void)
{
  sysfs_remove_file(kernel_kobj, &hello_attribute.attr);
  pr_info("Undone the fuckery\n");
}

module_exit(hellosysfs_exit);

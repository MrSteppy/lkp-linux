#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/string.h>

MODULE_DESCRIPTION("Module which changes the release version of uname -r");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static char original[__NEW_UTS_LEN];

static int __init replace(void)
{
  struct new_utsname *uts = init_utsname();

  strncpy(original, uts->release, __NEW_UTS_LEN - 1);
  original[__NEW_UTS_LEN - 1] = '\0';
  strncpy(uts->release, "fuck", __NEW_UTS_LEN - 1);
  uts->release[__NEW_UTS_LEN - 1] = '\0';
  return 0;
}

module_init(replace);

static void __exit restore(void)
{
  struct new_utsname *uts = init_utsname();

  strncpy(uts->release, original, __NEW_UTS_LEN - 1);
  uts->release[__NEW_UTS_LEN - 1] = '\0';
}

module_exit(restore);

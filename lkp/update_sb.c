#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>

MODULE_DESCRIPTION("Dumps the information of all superblocks for a given filesystem type loaded into memory by the kernel and updates their last access time");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static char *type;
module_param(type, charp, 0444);
MODULE_PARM_DESC(type, "The name of the filesystem type for which to dump the superblock");

static void dump_n_update_super_block(struct super_block *block, void *arg)
{
  const char *type = block->s_type->name;
  uuid_t *uuid = &block->s_uuid;
  ktime_t last_dump_time = block->last_dump_time;

  printk("uuid=%pUb type=%s time=%lld\n", uuid, type, last_dump_time);
  block->last_dump_time = ktime_get();
}

static int __init dump(void)
{
  if (type == NULL) {
    pr_info("No fs type selected. Please pass one as a parameter when loading the mdoule\n");
    return 0;
  }

  struct file_system_type *fs_type = get_fs_type(type);

  if (fs_type == NULL) {
    pr_err("Unknown fs type: %s", type);
    return 1;
  }

  iterate_supers_type(fs_type, dump_n_update_super_block, NULL);

  put_filesystem(fs_type);
  return 0;
}

module_init(dump);

static void __exit noop(void)
{

}

module_exit(noop);

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>

MODULE_DESCRIPTION("Module which dumps the information of all superblocks loaded into memory by the kernel");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static void dump_super_block(struct super_block *block, void *arg)
{
  const char *type = block->s_type->name;
  uuid_t *uuid = &block->s_uuid;

  printk("uuid=%pUb type=%s\n", uuid, type);
}

static int __init dump(void)
{
  iterate_supers(dump_super_block, NULL);
  return 0;
}

module_init(dump);

static void __exit noop(void)
{

}

module_exit(noop);



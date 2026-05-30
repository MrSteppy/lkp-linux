#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_DESCRIPTION("Hello World Module");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static char *whom = "world";
module_param(whom, charp, 0644);
MODULE_PARM_DESC(whom, "Who to say hello to");

static int howmany = 1;
module_param(howmany, int, 0644);
MODULE_PARM_DESC(howmany, "How often to say hello");

static int __init hello_init(void)
{
  for (int i = 0; i < howmany; i++) {
      pr_info("(%d) Hello %s!\n", i, whom);
  }
  return 0;
}

module_init(hello_init);

static void __exit hello_exit(void)
{
  pr_info("Goodbye %s\n", whom);
}

module_exit(hello_exit);

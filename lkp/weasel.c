#include <linux/init.h>
 #include <linux/module.h>
 #include <linux/kernel.h>
 #include <linux/dcache.h>
 #include <linux/proc_fs.h>
 #include <linux/seq_file.h>

 #define DENTRY_HASHTABLE_SIZE (1 << (sizeof(unsigned int) * 8 - d_hash_shift))

MODULE_DESCRIPTION("Steals shit from the dentry hashtable");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

struct weasel_data {
  void *address;
  unsigned int size;
  unsigned int entries;
  unsigned int longest;
};

static struct proc_dir_entry *proc_dir;

static void weasel_data_update(struct weasel_data *data)
{
  struct hlist_bl_head *address = dentry_hashtable;
  unsigned int size = DENTRY_HASHTABLE_SIZE;
  unsigned int entries = 0;
  unsigned int longest = 0;

  struct hlist_bl_head *bucket;

  for (unsigned int i = 0; i < size; i++) {
    bucket = dentry_hashtable + i;
    unsigned int entries_per_bukket = 0;

    hlist_bl_lock(bucket);
    struct dentry *dentry;
    struct hlist_bl_node *entry;

    hlist_bl_for_each_entry(dentry, entry, bucket, d_hash) {
      entries_per_bukket++;
    }
    hlist_bl_unlock(bucket);
    entries += entries_per_bukket;
    if (entries_per_bukket > longest) {
      longest = entries_per_bukket;
    }
  }

  data->address = address;
  data->size = size;
  data->entries = entries;
  data->longest = longest;
}

static int weasel_whoami_show(struct seq_file *m, void *v)
{
  seq_puts(m, "I'm a weasel!\n");
  return 0;
}

static int weasel_info_show(struct seq_file *m, void *v)
{
  struct weasel_data data;

  weasel_data_update(&data);

  seq_printf(m, "address: 0x%px\n", data.address);
  seq_printf(m, "size: %d\n", data.size);
  seq_printf(m, "entries: %d\n", data.entries);
  seq_printf(m, "longest: %d\n", data.longest);
  return 0;
}

static int weasel_dcache_show(struct seq_file *m, void *v)
{
  struct hlist_bl_head *bucket;

  for (unsigned int i = 0; i < DENTRY_HASHTABLE_SIZE; i++) {
    bucket = dentry_hashtable + i;
    hlist_bl_lock(bucket);
    struct dentry *dentry;
    struct hlist_bl_node *entry;

    hlist_bl_for_each_entry(dentry, entry, bucket, d_hash) {
      char buf[64];

      seq_printf(m, "%s\n", dentry_path_raw(dentry, buf, sizeof(buf)));
    }
    hlist_bl_unlock(bucket);
  }
  return 0;
}

static int weasel_pwd_show(struct seq_file *m, void *v)
{
  struct hlist_bl_head *bucket;

  for (unsigned int i = 0; i < DENTRY_HASHTABLE_SIZE; i++) {
    bucket = dentry_hashtable + i;
    hlist_bl_lock(bucket);
    struct dentry *dentry;
    struct hlist_bl_node *entry;

    hlist_bl_for_each_entry(dentry, entry, bucket, d_hash) {
      if (dentry->d_inode) {
	continue;
      }

      char buf[64];

      seq_printf(m, "%s\n", dentry_path_raw(dentry, buf, sizeof(buf)));
    }
    hlist_bl_unlock(bucket);
  }
  return 0;
}

DEFINE_PROC_SHOW_ATTRIBUTE(weasel_whoami);
DEFINE_PROC_SHOW_ATTRIBUTE(weasel_info);
DEFINE_PROC_SHOW_ATTRIBUTE(weasel_dcache);
DEFINE_PROC_SHOW_ATTRIBUTE(weasel_pwd);

static int __init weasel_init(void)
{
  proc_dir = proc_mkdir("weasel", NULL);
  proc_create("whoami", 0444, proc_dir, &weasel_whoami_proc_ops);
  proc_create("info", 0444, proc_dir, &weasel_info_proc_ops);
  proc_create("dcache", 0444, proc_dir, &weasel_dcache_proc_ops);
  proc_create("pwd", 0444, proc_dir, &weasel_pwd_proc_ops);

  return 0;
}

module_init(weasel_init);

static void __exit weasel_exit(void)
{
  proc_remove(proc_dir);
}

module_exit(weasel_exit);

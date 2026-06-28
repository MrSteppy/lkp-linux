#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/mempool.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "taskmonitor.h"

MODULE_DESCRIPTION("You can run, but you can't hide");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

struct task_sample {
  u64 utime;
  u64 stime;
  unsigned long vm_total;
  unsigned long vm_stack;
  unsigned long vm_data;
  struct list_head list;
  struct kref kref;
};

struct task_monitor {
  pid_t pid;
  struct list_head samples;
  unsigned int samples_size;
  struct mutex samples_lock;
  struct list_head list;
  struct dentry *dentry;
};

void init_task_monitor(struct task_monitor *monitor, pid_t pid)
{
  mutex_init(&monitor->samples_lock);
  INIT_LIST_HEAD(&monitor->samples);
  monitor->pid = pid;
  INIT_LIST_HEAD(&monitor->list);
  monitor->dentry = NULL;
}

struct task_monitor *task_monitor_new(pid_t pid)
{
  struct task_monitor *monitor = kmalloc(sizeof(struct task_monitor), GFP_KERNEL);

  if (!monitor) {
    return NULL;
  }

  init_task_monitor(monitor, pid);

  return monitor;
}

bool get_sample(const struct task_monitor *monitor, struct task_sample *sample)
{
  bool sample_okay = false;
  pid_t pid_nr = monitor->pid;
  struct pid *pid = find_get_pid(pid_nr);

  if (!pid) {
    pr_err("Cannot fid a process with pid %d\n", pid_nr);
    goto no_pid;
  }

  struct task_struct *task = get_pid_task(pid, PIDTYPE_PID);

  if (!task) {
    pr_err("Cannot query task information for process with pid %d\n", pid_nr);
    goto put_pid;
  }

  if (!pid_alive(task)) {
    pr_err("Process with pid %d is no longer alive\n", pid_nr);
    goto put_task_struct;
  }

  sample->utime = task->utime;
  sample->stime = task->stime;
  struct mm_struct *mm = task->mm;

  if (mm) {
    sample->vm_total = mm->total_vm;
    sample->vm_stack = mm->stack_vm;
    sample->vm_data = mm->data_vm;
  }

  sample_okay = true;

put_task_struct:
  put_task_struct(task);

put_pid:
  put_pid(pid);

no_pid:
  return sample_okay;
}

int render_task_sample(const struct task_monitor *monitor, struct task_sample *sample, char *buf, size_t buffer_len)
{
  return snprintf(buf, buffer_len, "pid %d usr %llu sys %llu vm_total %lu vm_stack %lu vm_data %lu", monitor->pid, sample->utime, sample->stime, sample->vm_total, sample->vm_stack, sample->vm_data);
}

static long unlocked_ioctl(struct file *, unsigned int, unsigned long);
static int taskmonitor_open(struct inode *, struct file *);
static ssize_t taskmonitor_write(struct file*, const char __user *, size_t, loff_t *);
static ssize_t taskmonitor_show(struct kobject *, struct kobj_attribute *, char *);
static ssize_t taskmonitor_store(struct kobject *, struct kobj_attribute *, const char *, size_t);
static unsigned long taskmonitor_count_objects(struct shrinker *, struct shrink_control *);
static unsigned long taskmonitor_scan_objects(struct shrinker *, struct shrink_control *);
static void *taskmonitor_seq_start(struct seq_file *, loff_t *);
static void *taskmonitor_seq_next(struct seq_file *, void *, loff_t *);
static void taskmonitor_seq_stop(struct seq_file *, void *);
static int taskmonitor_seq_show(struct seq_file *, void *);

static pid_t target;
module_param(target, int, 0444);
MODULE_PARM_DESC(target, "PID of the process to monitor");

static struct task_monitor task_monitor;
static struct task_struct *monitor_fn_handle;
static const struct kobj_attribute monitor_attribute = __ATTR_RW(taskmonitor);
static unsigned int major;
static struct kmem_cache *task_sample_cache;
static mempool_t *task_sample_mempool;
static struct list_head tasks;
static const struct file_operations fops = {
  .owner = THIS_MODULE,
  .unlocked_ioctl = unlocked_ioctl,
  .open = taskmonitor_open,
  .read = seq_read,
  .write = taskmonitor_write,
  .llseek = seq_lseek,
  .release = seq_release,
};
static struct shrinker taskmonitor_shrinker = {
  .count_objects = taskmonitor_count_objects,
  .scan_objects = taskmonitor_scan_objects,
};
static struct dentry *dentry;
static const struct seq_operations taskmonitor_seq_ops = {
  .start = taskmonitor_seq_start,
  .next  = taskmonitor_seq_next,
  .stop  = taskmonitor_seq_stop,
  .show  = taskmonitor_seq_show,
};

static void free_task_sample(struct task_sample *sample)
{
  mempool_free(sample, task_sample_mempool);
}

static void release_task_sample(struct kref *kref)
{
  struct task_sample *sample = container_of(kref, struct task_sample, kref);

  free_task_sample(sample);
}

static void put_task_sample(struct task_sample *sample)
{
  kref_put(&sample->kref, release_task_sample);
}

static void get_task_sample(struct task_sample *sample)
{
  kref_get(&sample->kref);
}

static void task_monitor_clear_samples(struct task_monitor *monitor)
{
  mutex_lock(&monitor->samples_lock);
  struct task_sample *sample, *tmp;

  list_for_each_entry_safe(sample, tmp, &monitor->samples, list) {
    list_del(&sample->list);
    put_task_sample(sample);
  }
  monitor->samples_size = 0;
  mutex_unlock(&monitor->samples_lock);
}

static unsigned long taskmonitor_count_objects(struct shrinker *shrink, struct shrink_control *sc)
{
  mutex_lock(&task_monitor.samples_lock);
  unsigned int samples_size = task_monitor.samples_size;

  mutex_unlock(&task_monitor.samples_lock);

  return samples_size ? samples_size : SHRINK_EMPTY;
}

static unsigned long taskmonitor_scan_objects(struct shrinker *shrink, struct shrink_control *sc)
{
  unsigned long freed = 0;
  int nr_to_scan = sc->nr_to_scan;

  mutex_lock(&task_monitor.samples_lock);
  struct task_sample *sample, *tmp;

  list_for_each_entry_safe(sample, tmp, &task_monitor.samples, list) {
    if (!nr_to_scan--) {
      break;
    }

    list_del(&sample->list);
    put_task_sample(sample);
    task_monitor.samples_size--;
    freed++;
  }

  mutex_unlock(&task_monitor.samples_lock);
  return freed;
}

static int save_sample(struct task_monitor *monitor)
{
  int res = 0;

  struct task_sample *sample = mempool_alloc(task_sample_mempool, GFP_KERNEL);

  if (!sample) {
    pr_err("Failed to allocate space for a new task_sample\n");
    res = -1;
    goto out_alloc_err;
  }

  kref_init(&sample->kref);
  bool sample_okay = get_sample(monitor, sample);

  if (!sample_okay) {
    res = -2;
    goto out_sample_failure;
  }

  get_task_sample(sample); //increment ref here, since we use sample in list AND for printing

  mutex_lock(&monitor->samples_lock);
  list_add_tail(&sample->list, &monitor->samples);
  monitor->samples_size += 1;
  mutex_unlock(&monitor->samples_lock);

  if (monitor == &task_monitor) {
    char buf[256];

    render_task_sample(monitor, sample, buf, sizeof(buf));
    pr_info("%s\n", buf);
  }

  put_task_sample(sample);

  return 0;

out_sample_failure:
  put_task_sample(sample);
out_alloc_err:
  return res;
}

static int monitor_fn(void *arg)
{
  while (!kthread_should_stop()) {
    save_sample(&task_monitor);
    struct task_monitor *monitor;

    list_for_each_entry(monitor, &tasks, list) {
      save_sample(monitor);
    }
    ssleep(1);
  }

  return 0;
}

static int start_monitor_fn(void)
{
  //TODO maybe use a mutex here?
  if (monitor_fn_handle) {
    pr_err("monitor_fn is already running\n");
    return -1;
  }

  monitor_fn_handle = kthread_run(monitor_fn, NULL, "monitor_fn");
  if (!monitor_fn_handle) {
    pr_err("Failed to start kthread for monitor_fn\n");
    return -2;
  }

  pr_info("Started monitor_fn\n");
  return 0;
}

static int stop_monitor_fn(void)
{
  if (!monitor_fn_handle) {
    pr_err("monitor_fn is already stopped\n");
    return -1;
  }

  kthread_stop(monitor_fn_handle);
  monitor_fn_handle = NULL;
  pr_info("Stopped monitor_fn\n");
  return 0;
}

static ssize_t taskmonitor_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
  char render_buf[1000];
  int offset = 0;
  struct task_sample *sample;
  bool first = true;

  mutex_lock(&task_monitor.samples_lock);
  list_for_each_entry(sample, &task_monitor.samples, list) {
    if (first) {
      first = false;
    } else {
      offset += snprintf(render_buf + offset, sizeof(render_buf) - offset, "\n");
    }
    offset += render_task_sample(&task_monitor, sample, render_buf + offset, sizeof(render_buf) - offset);
  }
  mutex_unlock(&task_monitor.samples_lock);

  return sysfs_emit(buf, "%s\n", render_buf); //check patch automatically adds a\n here
}

static ssize_t taskmonitor_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
  if (strcmp(buf, "stop") == 0) {
    stop_monitor_fn();
  } else if (strcmp(buf, "start") == 0) {
    start_monitor_fn();
  }
  return count;
}

static long unlocked_ioctl(struct file *file, unsigned int request_nr, unsigned long buf)
{
  if (request_nr == TM_GET) {
    struct task_sample sample;

    if (get_sample(&task_monitor, &sample)) {
      char render_buf[256];

      render_task_sample(&task_monitor, &sample, render_buf, sizeof(render_buf));
      return copy_to_user((void *) buf, render_buf, sizeof(render_buf));
    }
    return 0;
  } else if (request_nr == TM_START) {
    start_monitor_fn();
    return 0;
  } else if (request_nr == TM_STOP) {
    stop_monitor_fn();
    return 0;
  } else if (request_nr == TM_PID) {
    pid_t arg;
    unsigned long err = copy_from_user(&arg, (void *) buf, sizeof(arg));

    if (err) {
      pr_err("Failed to read request parameter\n");
      return err;
    }
    if (arg < 0) {
      return copy_to_user((void *) buf, &task_monitor.pid, sizeof(task_monitor.pid));
    } else {
      task_monitor.pid = arg;
      return 0;
    }
  } else {
    return -ENOTTY;
  }
}

static void *taskmonitor_seq_start(struct seq_file *seq, loff_t *pos)
{
  struct task_monitor *monitor = seq->private;

  mutex_lock(&monitor->samples_lock);
  loff_t n = *pos;
  struct task_sample *sample;

  list_for_each_entry(sample, &monitor->samples, list) {
    if (n-- > 0) {
      continue;
    }

    get_task_sample(sample);
    return sample;
  }

  return NULL;
}

static void *taskmonitor_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
  struct task_monitor *monitor = seq->private;
  struct task_sample *old_sample = v;
  struct task_sample *next_sample = NULL;
  struct task_sample *sample = old_sample;

  ++*pos;

  list_for_each_entry_continue(sample, &monitor->samples, list) {
    get_task_sample(sample);
    next_sample = sample;
    break;
  }

  put_task_sample(old_sample); //decrement after we no longer need it to find the next element
  return next_sample;
}

static void taskmonitor_seq_stop(struct seq_file *seq, void *v)
{
  struct task_monitor *monitor = seq->private;
  struct task_sample *sample = v;

  if (sample) {
    put_task_sample(sample);
  }
  mutex_unlock(&monitor->samples_lock);
}


static int taskmonitor_seq_show(struct seq_file *seq, void *v)
{
  struct task_monitor *monitor = seq->private;
  struct task_sample *sample = v;
  char buf[256];

  render_task_sample(monitor, sample, buf, sizeof(buf));
  seq_printf(seq, "%s\n", buf);
  return 0;
}

static int taskmonitor_open(struct inode *inode, struct file *file)
{
  int err = seq_open(file, &taskmonitor_seq_ops);

  if (err) {
    return err;
  }

  struct seq_file *seq = file->private_data;

  seq->private = inode->i_private;

  return 0;
}

static ssize_t taskmonitor_write(struct file *file, const char __user *user_buf, size_t size, loff_t *ppos)
{
  int pid;
  int err = kstrtoint_from_user(user_buf, size, 10, &pid);

  if (err) {
    return err;
  }

  pr_info("Got %d\n", pid);

  if (pid >= 0) {
    struct task_monitor *monitor = task_monitor_new(pid);

    if (!monitor) {
      pr_err("Failed to create new task_monitor\n");
      return -1;
    }

    list_add_tail(&monitor->list, &tasks);

    char file_name[64];

    snprintf(file_name, sizeof(file_name), "%d", pid);
    struct dentry *dfile = debugfs_create_file(file_name, 0444, dentry, monitor, &fops);

    if (IS_ERR(dfile)) {
      pr_err("Failed to create dentry");
      return -1;
    }

    monitor->dentry = dfile;

    pr_info("Created new file /sys/kernel/debug/taskmonitor/%s\n", file_name);
  } else {
    struct task_monitor *monitor;

    list_for_each_entry(monitor, &tasks, list) {
      if (monitor->pid != -pid) {
	continue;
      }

      debugfs_remove(monitor->dentry);
      pr_info("Removed file for pid\n");
    }
  }

  return size;
}

static int __init taskmonitor_init(void)
{
  int err = -1;

  INIT_LIST_HEAD(&tasks);

  pr_info("Initializing task_monitor struct...\n");
  init_task_monitor(&task_monitor, target);

  //slabs
  pr_info("Creating slabs cache...\n");
  task_sample_cache = KMEM_CACHE(task_sample, 0);
  if (!task_sample_cache) {
    pr_err("Failed to create slabs cache\n");
    goto out_slabs;
  }

  //mempool
  pr_info("Initializing mempool...\n");
  task_sample_mempool = mempool_create_slab_pool(16, task_sample_cache);
  if (!task_sample_mempool) {
    pr_err("Failed to initialize mempool\n");
    goto out_mempool;
  }

  //shrinker
  pr_info("Registering shrinker...\n");
  int shrinker_err = register_shrinker(&taskmonitor_shrinker, "taskmonitor");

  if (shrinker_err) {
    pr_err("Failed to register shrinker\n");
    err = shrinker_err;
    goto out_shrinker;
  }

  //initial pid check
  if (target) {
    pr_info("Checking pid...\n");
    struct task_sample sample;
    bool sample_okay = get_sample(&task_monitor, &sample);

    if (!sample_okay) {
      pr_err("Failed to update the task sample\n");
      goto out_target;
    }
  }

  //monitor_fn kthread
  pr_info("Starting monitoring thread...\n");
  int monitor_fn_err = start_monitor_fn();

  if (monitor_fn_err) {
    pr_err("Failed to start monitor thread\n");
    err = monitor_fn_err;
    goto out_monitor_fn;
  }

  //sysfs
  pr_info("Creating sysfs interface...\n");
  int sysfs_err = sysfs_create_file(kernel_kobj, &monitor_attribute.attr);

  if (sysfs_err) {
    pr_err("Failed to create sysfs file\n");
    err = sysfs_err;
    goto out_sysfs;
  }

  //ioctl
  pr_info("Creating ioctl interface...\n");
  major = register_chrdev(0, "taskmonitor", &fops);
  if (major < 0) {
    pr_err("Failed to register ioctl device\n");
    err = major;
    goto out_ioctl;
  }

  //debugfs
  pr_info("Creating debugfs interface...\n");
  dentry = debugfs_create_dir("taskmonitor", NULL);
  debugfs_create_file("control", 0200, dentry, NULL, &fops);

  pr_info("Done!\n");

  return 0;


  // unregister_chrdev(major, "taskmonitor");
out_ioctl:
  sysfs_remove_file(kernel_kobj, &monitor_attribute.attr);
out_sysfs:
  stop_monitor_fn();
out_monitor_fn:
out_target:
  unregister_shrinker(&taskmonitor_shrinker);
out_shrinker:
  mempool_destroy(task_sample_mempool);
out_mempool:
  kmem_cache_destroy(task_sample_cache);
out_slabs:
  return err;
}

module_init(taskmonitor_init);

static void __exit taskmonitor_exit(void)
{
  pr_info("Stopping monitor thread...\n");
  stop_monitor_fn();

  pr_info("Removing sysfs interface...\n");
  sysfs_remove_file(kernel_kobj, &monitor_attribute.attr);

  pr_info("Removing ioctl interface...\n");
  unregister_chrdev(major, "taskmonitor");

  pr_info("Removing debugfs interface...\n");
  debugfs_remove(dentry);

  pr_info("Freeing allocated memory...\n");
  task_monitor_clear_samples(&task_monitor);
  struct task_monitor *monitor, *tmp;

  list_for_each_entry_safe(monitor, tmp, &tasks, list) {
    task_monitor_clear_samples(monitor);
    list_del(&monitor->list);
    kfree(monitor);
  }

  pr_info("Unregistering shrinker...\n");
  unregister_shrinker(&taskmonitor_shrinker);

  pr_info("Destroying mempool...\n");
  mempool_destroy(task_sample_mempool);

  pr_info("Destroying slabs cache...\n");
  kmem_cache_destroy(task_sample_cache);

  pr_info("Done.\n");
}

module_exit(taskmonitor_exit);

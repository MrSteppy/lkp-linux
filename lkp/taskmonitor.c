#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/fs.h>

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
};

struct task_monitor {
  pid_t pid;
  struct list_head samples;
  unsigned int samples_size;
  struct mutex samples_lock;
};

void init_task_monitor(struct task_monitor *monitor, pid_t pid)
{
  mutex_init(&monitor->samples_lock);
  INIT_LIST_HEAD(&monitor->samples);
  monitor->pid = pid;
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

static pid_t target;
module_param(target, int, 0444);
MODULE_PARM_DESC(target, "PID of the process to monitor");

static struct task_sample task_sample;
static struct task_monitor task_monitor;
static struct task_struct *monitor_fn_handle;
static unsigned int major;
static struct file_operations fops;
static struct kmem_cache *task_sample_cache;

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
    kmem_cache_free(task_sample_cache, sample);
    task_monitor.samples_size--;
    freed++;
  }

  mutex_unlock(&task_monitor.samples_lock);
  return freed;
}

static struct shrinker taskmonitor_shrinker = {
  .count_objects = taskmonitor_count_objects,
  .scan_objects = taskmonitor_scan_objects,
};

static int save_sample(void)
{
  int res = 0;

  struct task_sample *sample = kmem_cache_alloc(task_sample_cache, GFP_KERNEL);

  if (!sample) {
    pr_err("Failed to allocate space for a new task_sample\n");
    res = -1;
    goto sample_failure;
  }

  bool sample_okay = get_sample(&task_monitor, sample);

  if (!sample_okay) {
    res = -2;
    goto sample_failure;
  }

  mutex_lock(&task_monitor.samples_lock);
  list_add_tail(&sample->list, &task_monitor.samples);
  task_monitor.samples_size += 1;
  mutex_unlock(&task_monitor.samples_lock);

sample_failure:
  return res;
}

static int monitor_fn(void *arg)
{
  while (!save_sample() && !kthread_should_stop()) {
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

static struct kobj_attribute monitor_attribute = __ATTR_RW(taskmonitor);

static long unlocked_ioctl(struct file *file, unsigned int request_nr, unsigned long buf)
{
  if (request_nr == TM_GET) {
    if (get_sample(&task_monitor, &task_sample)) {
      char render_buf[256];

      render_task_sample(&task_monitor, &task_sample, render_buf, sizeof(render_buf));
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

static int __init taskmonitor_init(void)
{
  pr_info("Initializing task_monitor struct...\n");
  init_task_monitor(&task_monitor, target);

  //slabs
  pr_info("Creating slabs cache...\n");
  task_sample_cache = KMEM_CACHE(task_sample, 0);
  if (!task_sample_cache) {
    pr_err("Failed to create slabs cache\n");
    return -1;
  }

  pr_info("Checking pid...\n");
  bool sample_okay = get_sample(&task_monitor, &task_sample);

  if (!sample_okay) {
    pr_err("Failed to update the task sample\n");
    return -1;
  }

  //monitor_fn kthread
  pr_info("Starting monitoring thread...\n");
  int monitor_fn_err = start_monitor_fn();

  if (monitor_fn_err) {
    pr_err("Failed to start monitor thread\n");
    return monitor_fn_err;
  }

  //sysfs
  pr_info("Creating sysfs interface...\n");
  int sysfs_err = sysfs_create_file(kernel_kobj, &monitor_attribute.attr);

  if (sysfs_err) {
    pr_err("Failed to create sysfs file\n");
    return sysfs_err;
  }

  //ioctl
  pr_info("Creating ioctl interface...\n");
  fops.unlocked_ioctl = unlocked_ioctl;
  major = register_chrdev(0, "taskmonitor", &fops);
  if (major < 0) {
    pr_err("Failed to register ioctl device\n");
    return major;
  }

  //shrinker
  pr_info("Registering shrinker...\n");
  int shrinker_err = register_shrinker(&taskmonitor_shrinker, "taskmonitor");

  if (shrinker_err) {
    pr_err("Failed to register shrinker\n");
    return shrinker_err;
  }

  pr_info("Done!\n");

  return 0;
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

  pr_info("Freeing allocated memory...\n");
  mutex_lock(&task_monitor.samples_lock);
  struct task_sample *sample, *tmp;

  list_for_each_entry_safe(sample, tmp, &task_monitor.samples, list) {
    list_del(&sample->list);
    kmem_cache_free(task_sample_cache, sample);
  }
  task_monitor.samples_size = 0;
  mutex_unlock(&task_monitor.samples_lock);

  pr_info("Unregistering shrinker...\n");
  unregister_shrinker(&taskmonitor_shrinker);

  pr_info("Releasing slabs cache...\n");
  kmem_cache_destroy(task_sample_cache);

  pr_info("Done.\n");
}

module_exit(taskmonitor_exit);

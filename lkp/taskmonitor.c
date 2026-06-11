#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/sched/task.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

MODULE_DESCRIPTION("You can run, but you can't hide");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

struct task_stats {
  pid_t pid;
  u64 utime;
  u64 stime;
};

int task_stats_update_from_task_struct(struct task_stats *stats, const struct task_struct *task)
{
  if (!pid_alive(task)) {
    pr_err("Process with pid %d is no longer alive\n", stats->pid);
    return -1;
  }

  stats->utime = task->utime;
  stats->stime = task->stime;

  return 0;
}

int task_stats_update_from_pid(struct task_stats *stats, struct pid *pid)
{
  struct task_struct *task = get_pid_task(pid, PIDTYPE_PID);

  if (!task) {
    pr_err("Cannot query task information for process with pid %d\n", stats->pid);
    return -1;
  }

  int err = task_stats_update_from_task_struct(stats, task);

  put_task_struct(task);

  if (err) {
    return err - 1;
  }

  return 0;
}

int task_stats_update(struct task_stats *stats)
{
  pid_t pid_nr = stats->pid;
  struct pid *pid = find_get_pid(pid_nr);

  if (!pid) {
    pr_err("Cannot fid a process with pid %d\n", pid_nr);
    return -1;
  }

  int err = task_stats_update_from_pid(stats, pid);

  put_pid(pid);

  if (err) {
    return err - 1;
  }

  return 0;
}

int task_stats_render(const struct task_stats *stats, char *buf, size_t buffer_len)
{
  return snprintf(buf, buffer_len, "pid %d usr %llu sys %llu", stats->pid, stats->utime, stats->stime);
}

static int target;
module_param(target, int, 0444);
MODULE_PARM_DESC(target, "PID of the process to monitor");

static struct task_stats stats;
static struct task_struct *monitor_fn_handle;

static int monitor_fn(void *arg)
{
  while (task_stats_update(&stats) == 0 && !kthread_should_stop()) {
    char buf[256];

    task_stats_render(&stats, buf, sizeof(buf));
    pr_info("%s\n", buf);
    ssleep(1);
  }

  return 0;
}

static int start_monitor_fn(void)
{
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
  if (task_stats_update(&stats) == 0) {
    char render_buf[256];

    task_stats_render(&stats, render_buf, sizeof(render_buf));
    return sysfs_emit(buf, "%s\n", render_buf);
  }

  return sysfs_emit(buf, "-\n");
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

static int __init taskmonitor_init(void)
{
  stats.pid = target;
  int pid_err = task_stats_update(&stats);

  if (pid_err) {
    pr_err("Encountered a problem with the provided pid\n");
    return pid_err;
  }

  int monitor_fn_err = start_monitor_fn();

  if (monitor_fn_err) {
    pr_err("Failed to start monitor thread\n");
    return monitor_fn_err;
  }

  int sysfs_err = sysfs_create_file(kernel_kobj, &monitor_attribute.attr);

  if (sysfs_err) {
    pr_err("Failed to create sysfs file\n");
    return sysfs_err;
  }

  return 0;
}

module_init(taskmonitor_init);

static void __exit taskmonitor_exit(void)
{
  sysfs_remove_file(kernel_kobj, &monitor_attribute.attr);
  stop_monitor_fn();
}

module_exit(taskmonitor_exit);

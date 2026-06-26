#include <linux/init.h>
 #include <linux/module.h>
 #include <linux/kernel.h>
 #include <linux/kthread.h>
 #include <linux/sched/task.h>
 #include <linux/delay.h>

MODULE_DESCRIPTION("You can run, but you can't hide");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

static int target;
module_param(target, int, 0444);
MODULE_PARM_DESC(target, "PID of the process to monitor");

struct task_monitor {
  pid_t pid_nr;
  struct pid *pid;
};

static struct task_monitor monitor;

static int monitor_fn(void *arg)
{
  struct task_struct *task = get_pid_task(monitor.pid, PIDTYPE_PID);

  while (pid_alive(task)) {
    pr_info("pid %d usr %llu sys %llu\n", monitor.pid_nr, task->utime, task->stime);
    ssleep(1);
  }

  put_task_struct(task);
  return 0;
}

static int monitor_pid(pid_t pid)
{
  struct pid *pid_struct = find_get_pid(pid);

  if (!pid_struct) {
    pr_err("Cannot fid a process with id %d\n", pid);
    return -1;
  }

  monitor.pid_nr = pid;
  monitor.pid = pid_struct;
  return 0;
}

static int __init taskmonitor_init(void)
{
  kthread_run(monitor_fn, NULL, "monitor_fn");
  return monitor_pid(target);
}

module_init(taskmonitor_init);

static void __exit taskmonitor_exit(void)
{
  put_pid(monitor.pid);
}

module_exit(taskmonitor_exit);

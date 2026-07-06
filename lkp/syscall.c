#include <linux/syscalls.h>

SYSCALL_DEFINE1(hello, char __user *, who)
{
  //pid_t pid = task_tgid_vnr(current); //get pid of current process; weird cpu shit, this could have been a macro

  char buf[256];

  if (strncpy_from_user(buf, who, sizeof(buf)) > 0) {
    return -1;
  }

  pr_info("Hello %s!\n", buf);
  return 0;
}

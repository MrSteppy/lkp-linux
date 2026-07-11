#include <linux/syscalls.h>

SYSCALL_DEFINE4(hello, char __user *, who, int, who_size, char __user *, buffer, int, buffer_size)
{
	//pid_t pid = task_tgid_vnr(current); //get pid of current process; weird cpu shit, this could have been a macro

	if (who_size < 0 || buffer_size < 0) {
		pr_err("Fuck off\n");
		return -EINVAL;
	}

	char who_buf[64];

	if (who_size + 1 > sizeof(who_buf)) {
		pr_err("who_size exceeds size of buffer\n");
		return -1;
	}
	if (strncpy_from_user(who_buf, who, sizeof(who_buf)) == -EFAULT) {
		pr_err("Failed to copy who into kernel space\n");
		return -EFAULT;
	}

	pr_info("Hello %s!\n", who_buf);

	char buf[128];
	int len = scnprintf(buf, sizeof(buf), "Hello %s!\n", who_buf);

	pr_info("Length of final string is %d\n", len);

	if (buffer_size < len + 1) {
		pr_err("Buffer is too small to hold string\n");
		return -1;
	}


	if (copy_to_user(buffer, buf, len + 1)) {
		pr_err("Failed to copy buffer to userspace\n");
		return -EFAULT;
	}

	return len;
}

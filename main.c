#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <linux/netlink.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <sys/socket.h>

const int RETRIES = 10;
const int DELAY = 10000; // 10ms


char* parse_env(pid_t pid, const char* env_name) {
	char path[256];
	snprintf(path, sizeof(path), "/proc/%d/environ", pid);
	FILE *file = fopen(path, "r");
	if (file == NULL) {
		perror(" >parse_env< fopen");
		return NULL;
	}

	char buffer[4096];
	size_t len = fread(buffer, 1, sizeof(buffer), file);
	if (len == 0) {
		perror(" >parse_env< fread");
		fclose(file);
		return NULL;
	}

	buffer[len] = '\0';

	char *env = buffer;
	size_t env_name_len = strlen(env_name);
	while (env < buffer + len) {
		if (strncmp(env, env_name, env_name_len) == 0 && env[env_name_len] == '=') {
			char *env_value = strdup(env + env_name_len + 1);
			fclose(file);
			return env_value;
		}
		env += strlen(env) + 1; // Move to the next environment variable
	}

	fclose(file);
	fprintf(stderr, " >parse_env< Environment variable %s not found for PID %d\n", env_name, pid);
	return NULL;
}

char* get_pwd(pid_t pid, int shell_shlvl, int retries, int delay) {
	for (int i = 0; i < retries; ++i) {
		char *shlvl_str = parse_env(pid, "SHLVL");
		if (shlvl_str == NULL) {
			fprintf(stderr, " >get_pwd< Failed to get SHLVL for PID %d\n", pid);
			return NULL;
		}

		int shlvl = atoi(shlvl_str);
		free(shlvl_str);

		if (shlvl == shell_shlvl) {
			usleep(delay);
			continue;
		}

		char *pwd = parse_env(pid, "PWD");
		if (pwd == NULL) {
			fprintf(stderr, " >get_pwd< PWD not found for PID %d\n", pid);
			return NULL;
		}

		return pwd;

	}

	fprintf(stderr, " >get_pwd< SHLVL of PID %d does not change after retries\n", pid);
	return NULL;
}

int handle_fork_event(pid_t pid, pid_t ppid) {
	char *shell_shlvl_str = parse_env(ppid, "SHLVL");
	if (shell_shlvl_str == NULL) {
		fprintf(stderr, " >handle_fork_event< Failed to get SHLVL for shell PID %d\n", ppid);
		return -1;
	}
	int shell_shlvl = atoi(shell_shlvl_str);
	free(shell_shlvl_str);

	fprintf(stderr, "Child process created with PID %d\n", pid);

	char *pwd = get_pwd(pid, shell_shlvl, RETRIES, DELAY);
	if (pwd == NULL) {
		fprintf(stderr, " >handle_fork_event< Correct PWD not found for PID %d\n", pid);
		return -1;
	}
	char shell_cwd[4096];
	char resolved_pwd[4096];
	snprintf(shell_cwd, sizeof(shell_cwd), "/proc/%d/cwd", ppid);
	ssize_t len = readlink(shell_cwd, shell_cwd, sizeof(shell_cwd) - 1);

	if (len == -1) {
		perror(" >handle_fork_event< readlink");
		free(pwd);
		return -1;
	}

	if (realpath(pwd, resolved_pwd) == NULL) {
		perror(" >handle_fork_event< realpath");
		free(pwd);
		return -1;
	}

	shell_cwd[len] = '\0';
	if (strcmp(resolved_pwd, shell_cwd) != 0) {
		fprintf(stderr, " >handle_fork_event< PWD of PID %d does not match CWD of shell PID %d\n", pid, ppid);
	}

	printf("PWD of PID %d: %s\n", pid, pwd);
	fprintf(stderr, "Successfull get PWD of PID %d: %s\n", pid, pwd);
	free(pwd);
}


void process_netlink_messages(int nl_sock, pid_t shell_pid) {
	char buf[4096];
	struct nlmsghdr *nlh;
	struct cn_msg *cn_hdr;
	struct proc_event *ev;

	while (1) {
		int len = recv(nl_sock, buf, sizeof(buf), 0);
		if (len < 0) {
			perror("recv");
			break;
		}

		nlh = (struct nlmsghdr *)buf;
		while (NLMSG_OK(nlh, len)) {
			cn_hdr = (struct cn_msg *)NLMSG_DATA(nlh);
			ev = (struct proc_event *)cn_hdr->data;

			if (ev->what != PROC_EVENT_FORK) {
				nlh = NLMSG_NEXT(nlh, len);
				continue;
			}

			pid_t pid = ev->event_data.fork.child_pid;
			pid_t ppid = ev->event_data.fork.parent_pid;
			if (ppid == shell_pid) {
				handle_fork_event(pid, ppid);
			}
			nlh = NLMSG_NEXT(nlh, len);

		}
	}
}

int setup_netlink_socket() {
	int nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (nl_sock < 0) {
		perror("socket");
		return -1;
	}

	struct sockaddr_nl sa_nl;
	memset(&sa_nl, 0, sizeof(sa_nl));
	sa_nl.nl_family = AF_NETLINK;
	sa_nl.nl_groups = CN_IDX_PROC;
	sa_nl.nl_pid = getpid();

	if (bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl)) < 0) {
		perror("bind");
		close(nl_sock);
		return -1;
	}

	return nl_sock;
}

int send_netlink_message(int nl_sock) {
	char buf[4096];
	struct nlmsghdr *nlh = (struct nlmsghdr *)buf;
	struct cn_msg *cn_hdr = (struct cn_msg *)NLMSG_DATA(nlh);

	nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct cn_msg) + sizeof(enum proc_cn_mcast_op));
	nlh->nlmsg_type = NLMSG_DONE;
	nlh->nlmsg_flags = 0;
	nlh->nlmsg_seq = 0;
	nlh->nlmsg_pid = getpid();

	cn_hdr->id.idx = CN_IDX_PROC;
	cn_hdr->id.val = CN_VAL_PROC;
	cn_hdr->seq = 0;
	cn_hdr->ack = 0;
	cn_hdr->len = sizeof(enum proc_cn_mcast_op);

	enum proc_cn_mcast_op *mcop_msg = (enum proc_cn_mcast_op *)cn_hdr->data;
	*mcop_msg = PROC_CN_MCAST_LISTEN;

	if (send(nl_sock, nlh, nlh->nlmsg_len, 0) < 0) {
		perror("send");
		return -1;
	}

	return 0;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "This program demonstrates how to use the netlink connector to get the shell's working directory.\n");
		fprintf(stderr, "Usage: %s <shell_pid>\n", argv[0]);
		return EXIT_FAILURE;
	}

	pid_t shell_pid = atoi(argv[1]);

	int nl_sock = setup_netlink_socket();
	if (nl_sock < 0) {
		return EXIT_FAILURE;
	}

	if (send_netlink_message(nl_sock) < 0) {
		close(nl_sock);
		return EXIT_FAILURE;
	}

	process_netlink_messages(nl_sock, shell_pid);

	close(nl_sock);
	return EXIT_SUCCESS;
}

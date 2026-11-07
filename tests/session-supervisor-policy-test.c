#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../desktop/session-supervisor/policy.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
	if (!seyrix_settings_exit_is_intentional(0 << 8) ||
			seyrix_settings_exit_is_intentional(1 << 8) ||
			seyrix_settings_exit_is_intentional(SIGTERM) ||
			seyrix_settings_retry_delay_seconds(0) != 1 ||
			seyrix_settings_retry_delay_seconds(1) != 2 ||
			seyrix_settings_retry_delay_seconds(2) != 4 ||
			seyrix_settings_retry_delay_seconds(3) != 8 ||
			seyrix_settings_retry_delay_seconds(4) != 16 ||
			seyrix_settings_retry_delay_seconds(5) != 30 ||
			!seyrix_settings_attempt_allowed(0) ||
			!seyrix_settings_attempt_allowed(5) ||
			seyrix_settings_attempt_allowed(6)) {
		return 1;
	}
	static const char nul_suffix[] = "OPEN\0suffix";
	if (!seyrix_supervisor_packet_matches("OPEN", 4, "OPEN") ||
			seyrix_supervisor_packet_matches(nul_suffix, sizeof(nul_suffix) - 1,
				"OPEN") ||
			seyrix_supervisor_packet_matches("OPENx", 5, "OPEN") ||
			seyrix_supervisor_packet_matches("OPE", 3, "OPEN") ||
			seyrix_supervisor_packet_matches("READY", 6, "READY")) {
		return 1;
	}
	uid_t uid = geteuid();
	pid_t pid = getpid();
	pid_t session = getsid(0);
	uid_t other_uid = uid == 0 ? 1 : 0;
	if (!seyrix_supervisor_peer_authorized(uid, pid, session, uid, session) ||
			seyrix_supervisor_peer_authorized(other_uid, pid, session, uid, session) ||
			seyrix_supervisor_peer_authorized(uid, pid, session + 1, uid, session) ||
			seyrix_supervisor_peer_authorized(uid, -1, session, uid, session) ||
			seyrix_supervisor_peer_authorized(uid, pid, -1, uid, session)) {
		return 1;
	}
	int ready_pipe[2];
	if (pipe(ready_pipe) != 0) {
		return 1;
	}
	pid_t child = fork();
	if (child < 0) {
		close(ready_pipe[0]);
		close(ready_pipe[1]);
		return 1;
	}
	if (child == 0) {
		close(ready_pipe[0]);
		char ready = setsid() == -1 ? 0 : 1;
		(void)write(ready_pipe[1], &ready, sizeof(ready));
		close(ready_pipe[1]);
		if (ready == 0) {
			_exit(1);
		}
		for (;;) {
			pause();
		}
	}
	close(ready_pipe[1]);
	char ready = 0;
	ssize_t bytes_read = read(ready_pipe[0], &ready, sizeof(ready));
	close(ready_pipe[0]);
	pid_t child_session = getsid(child);
	bool rejected_new_session = bytes_read == sizeof(ready) && ready == 1 &&
		child_session > 0 && !seyrix_supervisor_peer_authorized(uid, child,
		child_session, uid, session);
	(void)kill(child, SIGTERM);
	int child_status = 0;
	while (waitpid(child, &child_status, 0) < 0) {
		if (errno != EINTR) {
			return 1;
		}
	}
	if (!rejected_new_session || !WIFSIGNALED(child_status) ||
			WTERMSIG(child_status) != SIGTERM) {
		return 1;
	}
	return 0;
}

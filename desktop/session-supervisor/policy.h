#ifndef SEYRIX_SESSION_SUPERVISOR_POLICY_H
#define SEYRIX_SESSION_SUPERVISOR_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

enum {
	SEYRIX_SETTINGS_RESTART_LIMIT = 6,
};

static inline bool seyrix_settings_exit_is_intentional(int status) {
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static inline bool seyrix_supervisor_peer_authorized(uid_t peer_uid,
		pid_t peer_pid, pid_t peer_session, uid_t expected_uid,
		pid_t expected_session) {
	return peer_uid == expected_uid && peer_pid > 0 && peer_session > 0 &&
		expected_session > 0 && peer_session == expected_session;
}

static inline bool seyrix_supervisor_packet_matches(const void *packet,
		size_t packet_size, const char *expected) {
	return packet != NULL && expected != NULL &&
		packet_size == strlen(expected) &&
		memcmp(packet, expected, packet_size) == 0;
}

static inline unsigned int seyrix_settings_retry_delay_seconds(
		unsigned int prior_attempts) {
	return prior_attempts < 5 ? 1U << prior_attempts : 30U;
}

static inline bool seyrix_settings_attempt_allowed(unsigned int attempts) {
	return attempts < SEYRIX_SETTINGS_RESTART_LIMIT;
}

#endif

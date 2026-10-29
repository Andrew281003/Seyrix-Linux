#ifndef SEYRIX_SETTINGS_CONTROL_H
#define SEYRIX_SETTINGS_CONTROL_H

#include <stdbool.h>
#include <sys/types.h>

struct seyrix_server;

bool seyrix_settings_control_init(struct seyrix_server *server);
void seyrix_settings_control_finish(struct seyrix_server *server);
void seyrix_settings_control_changed(struct seyrix_server *server);

/* Shared by the accept path and tests. Session identity is uid plus SID. */
bool seyrix_settings_peer_authorized(uid_t peer_uid, pid_t peer_pid,
	uid_t owner_uid, pid_t owner_pid);

#endif

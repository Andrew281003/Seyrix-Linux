#ifndef SEYRIX_SETTINGS_CLIENT_H
#define SEYRIX_SETTINGS_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "settings-model.h"

enum seyrix_settings_client_state {
	SEYRIX_SETTINGS_CLIENT_OFFLINE,
	SEYRIX_SETTINGS_CLIENT_CONNECTING,
	SEYRIX_SETTINGS_CLIENT_NEGOTIATING,
	SEYRIX_SETTINGS_CLIENT_CONNECTED,
};

struct seyrix_settings_client {
	int fd;
	unsigned int io_source;
	unsigned int retry_source;
	unsigned int retry_delay_ms;
	unsigned int connection_attempts;
	uint32_t next_request_id;
	uint64_t revision;
	enum seyrix_settings_client_state state;
	bool negotiated;
	bool subscribed;
	bool snapshot_pending;
	bool refresh_pending;
	bool has_snapshot;
	char status[256];
	struct seyrix_settings_snapshot snapshot;
	void (*changed)(void *data);
	void *changed_data;
	struct {
		uint32_t request_id;
		uint16_t opcode;
	} pending[16];
	unsigned int pending_count;
};

void seyrix_settings_client_init(struct seyrix_settings_client *client,
	void (*changed)(void *data), void *changed_data);
void seyrix_settings_client_start(struct seyrix_settings_client *client);
void seyrix_settings_client_finish(struct seyrix_settings_client *client);

#endif

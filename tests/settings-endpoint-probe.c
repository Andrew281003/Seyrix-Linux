#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../desktop/common/settings-ipc.h"
#include "../desktop/settings/src/settings-model.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum { PROBE_TIMEOUT_MS = 3000, PROBE_MAX_HELD_SUBSCRIBERS = 9 };

static int endpoint_connect(bool check_owner) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	if (runtime == NULL || runtime[0] != '/') {
		errno = ENOENT;
		return -1;
	}
	char directory[sizeof(((struct sockaddr_un *)0)->sun_path)];
	char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
	if (snprintf(directory, sizeof(directory), "%s/seyrix", runtime) >=
			(int)sizeof(directory) ||
		snprintf(path, sizeof(path), "%s/settings-v1.sock", directory) >=
			(int)sizeof(path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	if (check_owner) {
		struct stat runtime_status;
		struct stat directory_status;
		struct stat socket_status;
		if (lstat(runtime, &runtime_status) != 0 ||
				!S_ISDIR(runtime_status.st_mode) ||
				runtime_status.st_uid != geteuid() ||
				(runtime_status.st_mode & 0077) != 0 ||
				lstat(directory, &directory_status) != 0 ||
				!S_ISDIR(directory_status.st_mode) ||
				directory_status.st_uid != geteuid() ||
				(directory_status.st_mode & 0077) != 0 ||
				lstat(path, &socket_status) != 0 ||
				!S_ISSOCK(socket_status.st_mode) ||
				socket_status.st_uid != geteuid() ||
				(socket_status.st_mode & 0777) != 0600) {
			errno = EPERM;
			return -1;
		}
	}
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		return -1;
	}
	struct sockaddr_un address = {.sun_family = AF_UNIX};
	memcpy(address.sun_path, path, strlen(path) + 1);
	if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
		int saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return -1;
	}
	return fd;
}

static bool send_packet(int fd, uint16_t opcode, uint32_t request_id,
		uint64_t revision, const uint8_t *payload, size_t payload_size,
		uint8_t frame_version, bool invalid_magic, size_t extra_bytes) {
	if (payload_size > SEYRIX_SETTINGS_MAX_PACKET_SIZE -
			SEYRIX_SETTINGS_HEADER_SIZE) {
		return false;
	}
	uint8_t packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE + 1];
	if (!seyrix_settings_encode_header(packet, sizeof(packet), opcode,
			request_id, (uint32_t)payload_size, revision)) {
		return false;
	}
	packet[4] = frame_version;
	if (invalid_magic) {
		packet[0] = 'X';
	}
	if (payload_size > 0) {
		memcpy(packet + SEYRIX_SETTINGS_HEADER_SIZE, payload, payload_size);
	}
	if (extra_bytes > 1 || extra_bytes + SEYRIX_SETTINGS_HEADER_SIZE +
			payload_size > sizeof(packet)) {
		return false;
	}
	size_t packet_size = SEYRIX_SETTINGS_HEADER_SIZE + payload_size + extra_bytes;
	return send(fd, packet, packet_size, MSG_NOSIGNAL) == (ssize_t)packet_size;
}

static bool receive_frame(int fd, struct seyrix_settings_frame *frame,
		uint8_t *packet) {
	struct pollfd poll_fd = {.fd = fd, .events = POLLIN | POLLHUP | POLLERR};
	int ready;
	do {
		ready = poll(&poll_fd, 1, PROBE_TIMEOUT_MS);
	} while (ready < 0 && errno == EINTR);
	if (ready <= 0 || (poll_fd.revents & POLLIN) == 0) {
		return false;
	}
	char ancillary[CMSG_SPACE(sizeof(int) * 4)];
	struct iovec iov = {.iov_base = packet, .iov_len = sizeof(packet)};
	struct msghdr message = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = ancillary,
		.msg_controllen = sizeof(ancillary),
	};
	ssize_t received = recvmsg(fd, &message, MSG_TRUNC | MSG_CMSG_CLOEXEC);
	bool has_control = CMSG_FIRSTHDR(&message) != NULL;
	for (struct cmsghdr *header = CMSG_FIRSTHDR(&message);
			header != NULL; header = CMSG_NXTHDR(&message, header)) {
		if (header->cmsg_level == SOL_SOCKET &&
				header->cmsg_type == SCM_RIGHTS &&
				header->cmsg_len >= CMSG_LEN(sizeof(int))) {
			size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
			int *descriptors = (int *)CMSG_DATA(header);
			for (size_t i = 0; i < count; i++) {
				close(descriptors[i]);
			}
		}
	}
	return !has_control && received > 0 && (size_t)received <= sizeof(packet) &&
		(message.msg_flags & MSG_TRUNC) == 0 &&
		seyrix_settings_decode_frame(packet, (size_t)received, frame);
}

static bool exchange(int fd, uint16_t opcode, uint32_t request_id,
		uint64_t revision, const uint8_t *payload, size_t payload_size,
		uint8_t frame_version, bool invalid_magic,
		enum seyrix_settings_error expected_error,
		struct seyrix_settings_frame *response, uint8_t *response_storage) {
	if (!send_packet(fd, opcode, request_id, revision, payload, payload_size,
			frame_version, invalid_magic, 0) ||
		!receive_frame(fd, response, response_storage) ||
		response->opcode != SEYRIX_SETTINGS_OP_RESPONSE ||
		response->request_id != request_id || response->payload_size < 4 ||
		seyrix_settings_read_u16(response->payload + 2) != opcode ||
		seyrix_settings_read_u16(response->payload) != expected_error) {
		return false;
	}
	return true;
}

static bool handshake(int fd, uint32_t request_id) {
	const uint8_t versions[4] = {1, 1, 0, 0};
	struct seyrix_settings_frame response;
	uint8_t storage[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	return exchange(fd, SEYRIX_SETTINGS_OP_HELLO, request_id, 0, versions,
		sizeof(versions), 1, false, SEYRIX_SETTINGS_ERROR_OK,
		&response, storage) && response.payload_size == 6 &&
		response.payload[4] == 1 && response.payload[5] == 0;
}

static bool get_snapshot(int fd, uint32_t request_id, uint64_t revision,
		enum seyrix_settings_error expected_error,
		struct seyrix_settings_frame *response,
		struct seyrix_settings_snapshot *snapshot) {
	uint8_t storage[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	if (!exchange(fd, SEYRIX_SETTINGS_OP_GET_SNAPSHOT, request_id, revision,
			NULL, 0, 1, false, expected_error, response, storage) ||
		!seyrix_settings_snapshot_parse(response->payload + 4,
			response->payload_size - 4, response->revision, snapshot)) {
		return false;
	}
	return true;
}

static bool probe_wrong_session(void) {
	int fd = endpoint_connect(false);
	if (fd < 0) {
		perror("connect settings socket from a separate session");
		return false;
	}
	struct pollfd poll_fd = {.fd = fd, .events = POLLIN | POLLHUP | POLLERR};
	int ready;
	do {
		ready = poll(&poll_fd, 1, PROBE_TIMEOUT_MS);
	} while (ready < 0 && errno == EINTR);
	char byte;
	ssize_t received = ready > 0 ? recv(fd, &byte, sizeof(byte), MSG_DONTWAIT) : -1;
	close(fd);
	return ready > 0 && received == 0;
}

static bool probe_rejected_packet(const uint8_t *packet, size_t packet_size) {
	int fd = endpoint_connect(false);
	if (fd < 0) {
		return false;
	}
	ssize_t sent = send(fd, packet, packet_size, MSG_NOSIGNAL);
	if (sent != (ssize_t)packet_size) {
		close(fd);
		return false;
	}
	struct pollfd poll_fd = {.fd = fd, .events = POLLIN | POLLHUP | POLLERR};
	int ready;
	do {
		ready = poll(&poll_fd, 1, PROBE_TIMEOUT_MS);
	} while (ready < 0 && errno == EINTR);
	char byte;
	ssize_t received = ready > 0 ? recv(fd, &byte, sizeof(byte), MSG_DONTWAIT) : -1;
	close(fd);
	return ready > 0 && received == 0;
}

static bool probe_subscription_limit(uint64_t revision) {
	int held[PROBE_MAX_HELD_SUBSCRIBERS];
	for (size_t i = 0; i < PROBE_MAX_HELD_SUBSCRIBERS; i++) {
		held[i] = -1;
	}
	size_t held_count = 0;
	bool limit_reached = false;
	for (size_t i = 0; i < PROBE_MAX_HELD_SUBSCRIBERS; i++) {
		int fd = endpoint_connect(false);
		if (fd < 0 || !handshake(fd, 1)) {
			if (fd >= 0) close(fd);
			goto cleanup;
		}
		struct seyrix_settings_frame response;
		uint8_t storage[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
		bool sent = send_packet(fd, SEYRIX_SETTINGS_OP_SUBSCRIBE, 2,
			revision, NULL, 0, 1, false, 0);
		if (!sent || !receive_frame(fd, &response, storage) ||
				response.opcode != SEYRIX_SETTINGS_OP_RESPONSE ||
				response.request_id != 2 || response.payload_size < 4 ||
				seyrix_settings_read_u16(response.payload + 2) !=
					SEYRIX_SETTINGS_OP_SUBSCRIBE) {
			close(fd);
			goto cleanup;
		}
		uint16_t error = seyrix_settings_read_u16(response.payload);
		if (error == SEYRIX_SETTINGS_ERROR_TOO_MANY_CLIENTS) {
			limit_reached = true;
			close(fd);
			break;
		}
		if (error != SEYRIX_SETTINGS_ERROR_OK &&
				error != SEYRIX_SETTINGS_ERROR_STALE_REVISION) {
			close(fd);
			goto cleanup;
		}
		if (!seyrix_settings_snapshot_parse(response.payload + 4,
				response.payload_size - 4, response.revision,
				&(struct seyrix_settings_snapshot){0})) {
			close(fd);
			goto cleanup;
		}
		held[held_count++] = fd;
	}
cleanup:
	for (size_t i = 0; i < held_count; i++) {
		close(held[i]);
	}
	return limit_reached;
}

static bool probe_watch_change(void) {
	int fd = endpoint_connect(true);
	if (fd < 0 || !handshake(fd, 1)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_DETAIL=connect-or-hello\n");
		if (fd >= 0) close(fd);
		return false;
	}
	struct seyrix_settings_frame response;
	struct seyrix_settings_snapshot before;
	if (!get_snapshot(fd, 2, 0, SEYRIX_SETTINGS_ERROR_OK,
			&response, &before)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_DETAIL=initial-snapshot\n");
		close(fd);
		return false;
	}
	uint8_t storage[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	if (!exchange(fd, SEYRIX_SETTINGS_OP_SUBSCRIBE, 3, before.revision,
			NULL, 0, 1, false, SEYRIX_SETTINGS_ERROR_OK,
			&response, storage)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_DETAIL=subscribe\n");
		close(fd);
		return false;
	}
	struct seyrix_settings_snapshot subscribed;
	if (!seyrix_settings_snapshot_parse(response.payload + 4,
			response.payload_size - 4, response.revision, &subscribed)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_DETAIL=subscribe-snapshot\n");
		close(fd);
		return false;
	}
	puts("SETTINGS_ENDPOINT_SUBSCRIPTION_READY=passed");
	fflush(stdout);
	struct timespec started;
	clock_gettime(CLOCK_MONOTONIC, &started);
	while (true) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		int64_t elapsed_ms = (int64_t)(now.tv_sec - started.tv_sec) * 1000 +
			(now.tv_nsec - started.tv_nsec) / 1000000;
		if (elapsed_ms >= 25000) break;
		struct pollfd poll_fd = {.fd = fd, .events = POLLIN | POLLHUP | POLLERR};
		int ready = poll(&poll_fd, 1, 1000);
		if (ready < 0 && errno == EINTR) continue;
		if (ready <= 0 || (poll_fd.revents & POLLIN) == 0) continue;
		uint8_t event_packet[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
		if (!receive_frame(fd, &response, event_packet)) break;
		if (response.opcode != SEYRIX_SETTINGS_OP_CHANGED ||
				response.request_id != 0 || response.payload_size != 0 ||
				response.revision <= subscribed.revision) {
			fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_DETAIL=invalid-change-event\n");
			break;
		}
		if (!get_snapshot(fd, 4, subscribed.revision,
				SEYRIX_SETTINGS_ERROR_STALE_REVISION,
				&response, &subscribed)) {
			fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_DETAIL=changed-snapshot\n");
			break;
		}
		if (subscribed.active_workspace != before.active_workspace) {
			close(fd);
			return true;
		}
	}
	fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_DETAIL=change-timeout\n");
	close(fd);
	return false;
}

static bool run_protocol_probe(void) {
	int fd = endpoint_connect(true);
	if (fd < 0) {
		perror("settings endpoint path/ownership/connect");
		return false;
	}
	if (!handshake(fd, 1)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=hello-negotiation\n");
		close(fd);
		return false;
	}
	struct seyrix_settings_frame response;
	struct seyrix_settings_snapshot snapshot;
	if (!get_snapshot(fd, 2, 0, SEYRIX_SETTINGS_ERROR_OK,
			&response, &snapshot)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=initial-snapshot\n");
		close(fd);
		return false;
	}
	uint64_t revision = snapshot.revision;
	printf("SETTINGS_ENDPOINT_SNAPSHOT=passed revision=%llu active_workspace=%u outputs=%zu keyboards=%u\n",
		(unsigned long long)revision, snapshot.active_workspace,
		snapshot.output_count, snapshot.keyboard_count);
	uint64_t stale_revision = revision == UINT64_MAX ? revision - 1 : revision + 1;
	if (!get_snapshot(fd, 3, stale_revision,
			SEYRIX_SETTINGS_ERROR_STALE_REVISION, &response, &snapshot)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=stale-revision-snapshot\n");
		close(fd);
		return false;
	}
	const uint8_t malformed[] = {0};
	uint8_t storage[SEYRIX_SETTINGS_MAX_PACKET_SIZE];
	if (!exchange(fd, SEYRIX_SETTINGS_OP_GET_SNAPSHOT, 4, revision,
			malformed, sizeof(malformed), 1, false,
			SEYRIX_SETTINGS_ERROR_MALFORMED, &response, storage) ||
		!exchange(fd, 0x7ffe, 5, revision, NULL, 0, 1, false,
			SEYRIX_SETTINGS_ERROR_UNKNOWN_OPCODE, &response, storage) ||
		!exchange(fd, SEYRIX_SETTINGS_OP_GET_SNAPSHOT, 6, revision,
			NULL, 0, 2, false, SEYRIX_SETTINGS_ERROR_UNSUPPORTED_FRAME,
			&response, storage)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=malformed-version-unknown\n");
		close(fd);
		return false;
	}
	if (!send_packet(fd, SEYRIX_SETTINGS_OP_SUBSCRIBE, 7, revision,
			NULL, 0, 1, false, 0) ||
		!receive_frame(fd, &response, storage) ||
		response.opcode != SEYRIX_SETTINGS_OP_RESPONSE ||
		response.request_id != 7 || response.payload_size < 4 ||
		seyrix_settings_read_u16(response.payload + 2) !=
			SEYRIX_SETTINGS_OP_SUBSCRIBE) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=subscription-response\n");
		close(fd);
		return false;
	}
	uint16_t subscribe_error = seyrix_settings_read_u16(response.payload);
	if (subscribe_error != SEYRIX_SETTINGS_ERROR_OK &&
			subscribe_error != SEYRIX_SETTINGS_ERROR_STALE_REVISION) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=subscription-error-%u\n",
			subscribe_error);
		close(fd);
		return false;
	}
	struct seyrix_settings_snapshot subscribed;
	if (!seyrix_settings_snapshot_parse(response.payload + 4,
			response.payload_size - 4, response.revision, &subscribed)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=subscription-snapshot\n");
		close(fd);
		return false;
	}
	printf("SETTINGS_ENDPOINT_NEGOTIATION_CORRELATION=passed\n");
	printf("SETTINGS_ENDPOINT_STALE_REVISION=passed\n");
	printf("SETTINGS_ENDPOINT_MALFORMED_VERSION_UNKNOWN=passed\n");
	if (!probe_subscription_limit(revision)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=subscriber-bound\n");
		close(fd);
		return false;
	}
	printf("SETTINGS_ENDPOINT_SUBSCRIBER_BOUND=passed\n");
	close(fd);
	fd = endpoint_connect(false);
	if (fd < 0 || !handshake(fd, 1) ||
			!get_snapshot(fd, 2, 0, SEYRIX_SETTINGS_ERROR_OK,
				&response, &snapshot)) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=disconnect-reconnect\n");
		if (fd >= 0) close(fd);
		return false;
	}
	close(fd);
	printf("SETTINGS_ENDPOINT_DISCONNECT_RECONNECT=passed\n");
	uint8_t malformed_packet[SEYRIX_SETTINGS_HEADER_SIZE] = {0};
	seyrix_settings_write_u16(malformed_packet + 6,
		SEYRIX_SETTINGS_OP_GET_SNAPSHOT);
	if (!probe_rejected_packet(malformed_packet, sizeof(malformed_packet))) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=malformed-packet-rejection\n");
		return false;
	}
	uint8_t oversized[SEYRIX_SETTINGS_MAX_PACKET_SIZE + 1] = {0};
	if (!seyrix_settings_encode_header(oversized, sizeof(oversized),
			SEYRIX_SETTINGS_OP_GET_SNAPSHOT, 1,
			SEYRIX_SETTINGS_MAX_PACKET_SIZE - SEYRIX_SETTINGS_HEADER_SIZE, 0) ||
		!probe_rejected_packet(oversized, sizeof(oversized))) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE_DETAIL=oversized-packet-rejection\n");
		return false;
	}
	printf("SETTINGS_ENDPOINT_MALFORMED_OVERSIZED_REJECT=passed\n");
	return true;
}

int main(int argc, char **argv) {
	const char *result_path = getenv("SEYRIX_SETTINGS_PROBE_LOG");
	if (result_path != NULL) {
		if (result_path[0] != '/' || strlen(result_path) >= 4096 ||
				freopen(result_path, "w", stdout) == NULL ||
				freopen(result_path, "a", stderr) == NULL) {
			perror("open Settings endpoint probe log");
			return 2;
		}
	}
	if (argc == 2 && strcmp(argv[1], "--wrong-session") == 0) {
		if (!probe_wrong_session()) {
			fprintf(stderr, "SETTINGS_ENDPOINT_WRONG_SESSION=failed\n");
			return 1;
		}
		puts("SETTINGS_ENDPOINT_WRONG_SESSION=passed");
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "--watch-change") == 0) {
		if (!probe_watch_change()) {
			fprintf(stderr, "SETTINGS_ENDPOINT_SUBSCRIPTION_CHANGE=failed\n");
			return 1;
		}
		puts("SETTINGS_ENDPOINT_SUBSCRIPTION_CHANGE=passed");
		return 0;
	}
	if (argc != 1) {
		fprintf(stderr, "Usage: %s [--wrong-session|--watch-change]\n",
			argv[0]);
		return 2;
	}
	if (!run_protocol_probe()) {
		fprintf(stderr, "SETTINGS_ENDPOINT_PROBE=failed\n");
		return 1;
	}
	puts("SETTINGS_ENDPOINT_PROBE=passed");
	return 0;
}

#define _POSIX_C_SOURCE 200809L

#include "../desktop/common/shell-ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

static unsigned int failures;

#define CHECK(name, condition) do { \
	if (condition) { \
		printf("PASS %s\n", name); \
	} else { \
		fprintf(stderr, "FAIL %s\n", name); \
		failures++; \
	} \
} while (0)

static void initialize_header(struct seyrix_shell_packet_header *header,
		uint8_t type, size_t size) {
	*header = (struct seyrix_shell_packet_header){
		.version = SEYRIX_SHELL_IPC_VERSION,
		.type = type,
		.size = (uint16_t)size,
	};
}

static bool transmit_and_decode(const void *payload, size_t payload_size,
		struct seyrix_shell_request *request, size_t *received_size) {
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK,
			0, sockets) != 0) return false;
	ssize_t sent = send(sockets[0], payload, payload_size, MSG_NOSIGNAL);
	if (sent != (ssize_t)payload_size) {
		close(sockets[0]);
		close(sockets[1]);
		return false;
	}
	uint8_t buffer[sizeof(struct seyrix_shell_packet_workspace)];
	ssize_t received = recv(sockets[1], buffer, sizeof(buffer),
		MSG_DONTWAIT | MSG_TRUNC);
	close(sockets[0]);
	close(sockets[1]);
	if (received < 0) return false;
	if (received_size != NULL) *received_size = (size_t)received;
	return seyrix_shell_ipc_decode_request(buffer, (size_t)received, 4, request);
}

static bool exercise_decode_case(const void *payload, size_t payload_size,
		bool expected_valid) {
	struct seyrix_shell_request request;
	bool decoded = false;
	size_t received = 0;
	int sockets[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK,
			0, sockets) != 0) return false;
	ssize_t sent = send(sockets[0], payload, payload_size, MSG_NOSIGNAL);
	uint8_t buffer[sizeof(struct seyrix_shell_packet_workspace)];
	ssize_t read_size = sent == (ssize_t)payload_size ?
		recv(sockets[1], buffer, sizeof(buffer), MSG_DONTWAIT | MSG_TRUNC) : -1;
	if (read_size >= 0) {
		received = (size_t)read_size;
		decoded = seyrix_shell_ipc_decode_request(buffer, received, 4, &request);
	}
	close(sockets[0]);
	close(sockets[1]);
	return sent == (ssize_t)payload_size && read_size >= 0 &&
		decoded == expected_valid &&
		(!expected_valid || received == payload_size);
}

int main(void) {
	struct seyrix_shell_packet_workspace workspace = {0};
	initialize_header(&workspace.header, SEYRIX_SHELL_SELECT_WORKSPACE,
		sizeof(workspace));
	workspace.workspace = 3;
	CHECK("valid workspace datagram decodes",
		exercise_decode_case(&workspace, sizeof(workspace), true));

	struct seyrix_shell_packet_id id = {0};
	initialize_header(&id.header, SEYRIX_SHELL_REQUEST_FOCUS, sizeof(id));
	id.id = 17;
	CHECK("valid window identifier decodes",
		exercise_decode_case(&id, sizeof(id), true));

	struct seyrix_shell_packet_action action = {0};
	initialize_header(&action.header, SEYRIX_SHELL_CLOSE_OVERLAY, sizeof(action));
	CHECK("valid action datagram decodes",
		exercise_decode_case(&action, sizeof(action), true));

	uint8_t short_header = 0;
	CHECK("truncated header is rejected",
		exercise_decode_case(&short_header, sizeof(short_header), false));

	struct seyrix_shell_packet_header header_only = {0};
	initialize_header(&header_only, SEYRIX_SHELL_SELECT_WORKSPACE,
		sizeof(workspace));
	CHECK("truncated packet body is rejected",
		exercise_decode_case(&header_only, sizeof(header_only), false));

	uint8_t oversized[64] = {0};
	struct seyrix_shell_packet_workspace oversized_workspace = {0};
	initialize_header(&oversized_workspace.header, SEYRIX_SHELL_SELECT_WORKSPACE,
		sizeof(oversized_workspace));
	memcpy(oversized, &oversized_workspace, sizeof(oversized_workspace));
	CHECK("oversized datagram is rejected after MSG_TRUNC receive",
		exercise_decode_case(oversized, sizeof(oversized), false));

	struct seyrix_shell_packet_workspace wrong_version = workspace;
	wrong_version.header.version++;
	CHECK("incorrect protocol version is rejected",
		exercise_decode_case(&wrong_version, sizeof(wrong_version), false));

	struct seyrix_shell_packet_workspace wrong_size = workspace;
	wrong_size.header.size++;
	CHECK("declared packet size mismatch is rejected",
		exercise_decode_case(&wrong_size, sizeof(wrong_size), false));

	struct seyrix_shell_packet_action unknown_type = action;
	unknown_type.header.type = UINT8_MAX;
	CHECK("unknown message type is rejected",
		exercise_decode_case(&unknown_type, sizeof(unknown_type), false));

	struct seyrix_shell_packet_workspace bad_workspace = workspace;
	bad_workspace.workspace = 4;
	CHECK("out-of-range workspace is rejected",
		exercise_decode_case(&bad_workspace, sizeof(bad_workspace), false));

	struct seyrix_shell_packet_id zero_id = id;
	zero_id.id = 0;
	CHECK("zero window identifier is rejected",
		exercise_decode_case(&zero_id, sizeof(zero_id), false));

	struct seyrix_shell_packet_workspace reserved_workspace = workspace;
	reserved_workspace.reserved[0] = 1;
	CHECK("nonzero reserved workspace bytes are rejected",
		exercise_decode_case(&reserved_workspace, sizeof(reserved_workspace), false));

	struct seyrix_shell_packet_action reserved_action = action;
	reserved_action.reserved[2] = 1;
	CHECK("nonzero reserved action bytes are rejected",
		exercise_decode_case(&reserved_action, sizeof(reserved_action), false));

	int closed_peer[2];
	bool closed_peer_rejected = false;
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK,
			0, closed_peer) == 0) {
		close(closed_peer[1]);
		int send_error = 0;
		enum seyrix_shell_ipc_send_result result = seyrix_shell_ipc_send_packet(
			closed_peer[0], &action, sizeof(action), &send_error);
		closed_peer_rejected = result == SEYRIX_SHELL_IPC_SEND_ERROR &&
			(send_error == EPIPE || send_error == ECONNRESET);
		close(closed_peer[0]);
	}
	CHECK("closed peer is reported without SIGPIPE", closed_peer_rejected);

	int backpressure[2];
	bool backpressure_observed = false;
	if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK,
			0, backpressure) == 0) {
		int send_buffer = 1024;
		(void)setsockopt(backpressure[0], SOL_SOCKET, SO_SNDBUF, &send_buffer,
			sizeof(send_buffer));
		for (size_t i = 0; i < 100000; i++) {
			int send_error = 0;
			enum seyrix_shell_ipc_send_result result = seyrix_shell_ipc_send_packet(
				backpressure[0], &action, sizeof(action), &send_error);
			if (result == SEYRIX_SHELL_IPC_WOULD_BLOCK) {
				backpressure_observed = true;
				break;
			}
			if (result != SEYRIX_SHELL_IPC_SENT) break;
		}
		close(backpressure[0]);
		close(backpressure[1]);
	}
	CHECK("nonblocking send backpressure is reported", backpressure_observed);

	bool reconnects_ok = true;
	for (unsigned int i = 0; i < 128; i++) {
		struct seyrix_shell_request request;
		if (!transmit_and_decode(&action, sizeof(action), &request, NULL) ||
				request.type != SEYRIX_SHELL_CLOSE_OVERLAY) {
			reconnects_ok = false;
			break;
		}
	}
	CHECK("rapid independent socketpair decoder cycles remain valid",
		reconnects_ok);

	if (failures != 0) {
		fprintf(stderr, "SHELL_IPC_TEST_FAILURES=%u\n", failures);
		return EXIT_FAILURE;
	}
	puts("SHELL_IPC_TEST=passed");
	return EXIT_SUCCESS;
}

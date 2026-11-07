#define _POSIX_C_SOURCE 200809L

#include "wayland-probe.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <time.h>

#include <wayland-client.h>

struct sync_state {
	bool done;
};

static void sync_done(void *data, struct wl_callback *callback,
		uint32_t serial) {
	(void)callback;
	(void)serial;
	struct sync_state *state = data;
	state->done = true;
}

static const struct wl_callback_listener sync_listener = {
	.done = sync_done,
};

static uint64_t monotonic_ms(void) {
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
		return 0;
	}
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

bool seyrix_wayland_sync_wait(struct wl_display *display, int control_fd,
		unsigned int timeout_ms,
		seyrix_wayland_control_handler control_handler, void *control_data) {
	if (display == NULL || (control_fd >= 0 && control_handler == NULL)) {
		return false;
	}
	uint64_t start = monotonic_ms();
	uint64_t deadline = start + timeout_ms;
	if (deadline < start) {
		deadline = UINT64_MAX;
	}
	struct sync_state state = {0};
	struct wl_callback *callback = wl_display_sync(display);
	if (callback == NULL || wl_callback_add_listener(callback,
			&sync_listener, &state) != 0) {
		if (callback != NULL) {
			wl_callback_destroy(callback);
		}
		return false;
	}
	int display_fd = wl_display_get_fd(display);
	bool prepared = false;
	bool success = false;
	while (!state.done) {
		if (monotonic_ms() >= deadline) {
			break;
		}
		while (wl_display_prepare_read(display) != 0) {
			if (wl_display_dispatch_pending(display) < 0 || state.done) {
				goto finished;
			}
		}
		prepared = true;
		int flush_result = wl_display_flush(display);
		if (flush_result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			wl_display_cancel_read(display);
			prepared = false;
			goto finished;
		}
		uint64_t now = monotonic_ms();
		if (now >= deadline) {
			wl_display_cancel_read(display);
			prepared = false;
			break;
		}
		uint64_t remaining = deadline - now;
		struct pollfd poll_fds[2] = {
			{
				.fd = display_fd,
				.events = POLLIN | ((flush_result < 0) ? POLLOUT : 0),
			},
		};
		nfds_t poll_count = 1;
		if (control_fd >= 0) {
			poll_fds[poll_count++] = (struct pollfd) {
				.fd = control_fd,
				.events = POLLIN,
			};
		}
		int poll_timeout = remaining > INT_MAX ? INT_MAX : (int)remaining;
		int result = poll(poll_fds, poll_count, poll_timeout);
		if (result < 0) {
			int poll_error = errno;
			wl_display_cancel_read(display);
			prepared = false;
			if (poll_error == EINTR && control_handler != NULL &&
					!control_handler(control_data)) {
				goto finished;
			}
			if (poll_error == EINTR) {
				continue;
			}
			goto finished;
		}
		if (result == 0) {
			wl_display_cancel_read(display);
			prepared = false;
			break;
		}
		if (poll_count == 2 && poll_fds[1].revents != 0) {
			wl_display_cancel_read(display);
			prepared = false;
			if ((poll_fds[1].revents &
					(POLLERR | POLLHUP | POLLNVAL)) != 0 ||
					!control_handler(control_data)) {
				goto finished;
			}
			continue;
		}
		short display_events = poll_fds[0].revents;
		if ((display_events & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			wl_display_cancel_read(display);
			prepared = false;
			goto finished;
		}
		if ((display_events & POLLIN) != 0) {
			prepared = false;
			if (wl_display_read_events(display) < 0 ||
					wl_display_dispatch_pending(display) < 0) {
				goto finished;
			}
		} else {
			wl_display_cancel_read(display);
			prepared = false;
		}
	}
	success = state.done;

finished:
	if (prepared) {
		wl_display_cancel_read(display);
	}
	wl_callback_destroy(callback);
	return success || state.done;
}

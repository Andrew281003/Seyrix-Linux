#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>

static int parse_cycles(int argc, char **argv) {
	if (argc < 2) {
		return 1;
	}
	char *end = NULL;
	long value = strtol(argv[1], &end, 10);
	if (end == argv[1] || *end != '\0' || value < 1 || value > 100) {
		fprintf(stderr, "Usage: %s [cycles: 1..100]\n", argv[0]);
		return -1;
	}
	return (int)value;
}

static void log_message(enum wlr_log_importance importance, const char *format,
		va_list args) {
	static const char *const labels[] = {"ERROR", "INFO", "DEBUG"};
	int index = (int)importance;
	const char *label = index >= 0 && index < 3 ? labels[index] : "LOG";
	fprintf(stderr, "WLR_CONTROL_LOG[%s] ", label);
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
	fflush(stderr);
}

static int run_cycle(int cycle, int cycles) {
	int status = 1;
	struct wl_display *display = NULL;
	struct wlr_backend *backend = NULL;
	struct wlr_renderer *renderer = NULL;

	printf("WLR_CONTROL_CYCLE_BEGIN=%d/%d\n", cycle, cycles);
	printf("WLR_CONTROL_ENV_XDG_RUNTIME_DIR=%s\n",
		getenv("XDG_RUNTIME_DIR") != NULL ? getenv("XDG_RUNTIME_DIR") : "unset");
	printf("WLR_CONTROL_ENV_LIBSEAT_BACKEND=%s\n",
		getenv("LIBSEAT_BACKEND") != NULL ? getenv("LIBSEAT_BACKEND") : "unset");
	printf("WLR_CONTROL_ENV_LIBGL_ALWAYS_SOFTWARE=%s\n",
		getenv("LIBGL_ALWAYS_SOFTWARE") != NULL ? getenv("LIBGL_ALWAYS_SOFTWARE") : "unset");
	printf("WLR_CONTROL_ENV_WLR_RENDERER_ALLOW_SOFTWARE=%s\n",
		getenv("WLR_RENDERER_ALLOW_SOFTWARE") != NULL ? getenv("WLR_RENDERER_ALLOW_SOFTWARE") : "unset");

	display = wl_display_create();
	if (display == NULL) {
		fprintf(stderr, "WLR_CONTROL_DISPLAY_CREATE=failed errno=%d (%s)\n",
			errno, strerror(errno));
		goto cleanup;
	}
	printf("WLR_CONTROL_DISPLAY_CREATE=success\n");

	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);
	backend = wlr_backend_autocreate(event_loop, NULL);
	if (backend == NULL) {
		fprintf(stderr, "WLR_CONTROL_BACKEND_AUTO_CREATE=failed\n");
		goto cleanup;
	}
	printf("WLR_CONTROL_BACKEND_AUTO_CREATE=success\n");

	renderer = wlr_renderer_autocreate(backend);
	if (renderer == NULL) {
		fprintf(stderr, "WLR_CONTROL_RENDERER_AUTO_CREATE=failed\n");
		goto cleanup;
	}
	printf("WLR_CONTROL_RENDERER_AUTO_CREATE=success\n");

	if (!wlr_renderer_init_wl_display(renderer, display)) {
		fprintf(stderr, "WLR_CONTROL_RENDERER_INIT_WL_DISPLAY=failed\n");
		goto cleanup;
	}
	printf("WLR_CONTROL_RENDERER_INIT_WL_DISPLAY=success\n");
	status = 0;

cleanup:
	if (renderer != NULL) {
		wlr_renderer_destroy(renderer);
		printf("WLR_CONTROL_RENDERER_DESTROY=success\n");
	}
	if (backend != NULL) {
		wlr_backend_destroy(backend);
		printf("WLR_CONTROL_BACKEND_DESTROY=success\n");
	}
	if (display != NULL) {
		wl_display_destroy(display);
		printf("WLR_CONTROL_DISPLAY_DESTROY=success\n");
	}
	printf("WLR_CONTROL_CYCLE_END=%d result=%s\n", cycle,
		status == 0 ? "pass" : "fail");
	return status;
}

int main(int argc, char **argv) {
	int cycles = parse_cycles(argc, argv);
	if (cycles < 1) {
		return 2;
	}
	wlr_log_init(WLR_INFO, log_message);
	int status = 0;
	for (int cycle = 1; cycle <= cycles; cycle++) {
		if (run_cycle(cycle, cycles) != 0) {
			status = 1;
			break;
		}
	}
	printf("WLR_CONTROL_EXIT=%d\n", status);
	return status;
}

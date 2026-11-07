#include <EGL/egl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <wayland-server-core.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>

static bool has_extension(const char *extensions, const char *wanted) {
	if (extensions == NULL || wanted == NULL || wanted[0] == '\0') return false;
	size_t length = strlen(wanted);
	for (const char *match = extensions;
			(match = strstr(match, wanted)) != NULL; match += length) {
		if ((match == extensions || match[-1] == ' ') &&
				(match[length] == '\0' || match[length] == ' ')) return true;
	}
	return false;
}

static int run_cycle(int drm_fd, int cycle) {
	struct wl_display *wayland_display = wl_display_create();
	if (wayland_display == NULL) {
		fprintf(stderr, "cycle %d: wl_display_create failed\n", cycle);
		return 1;
	}
	struct wlr_renderer *renderer =
		wlr_gles2_renderer_create_with_drm_fd(drm_fd);
	if (renderer == NULL || !wlr_renderer_is_gles2(renderer)) {
		fprintf(stderr, "cycle %d: GLES2 renderer creation failed\n", cycle);
		if (renderer != NULL) wlr_renderer_destroy(renderer);
		wl_display_destroy(wayland_display);
		return 1;
	}
	if (!wlr_renderer_init_wl_display(renderer, wayland_display)) {
		fprintf(stderr, "cycle %d: renderer Wayland initialization failed\n", cycle);
		wl_display_destroy_clients(wayland_display);
		wlr_renderer_destroy(renderer);
		wl_display_destroy(wayland_display);
		return 1;
	}

	struct wlr_egl *wlr_egl = wlr_gles2_renderer_get_egl(renderer);
	EGLDisplay egl_display = wlr_egl != NULL ?
		wlr_egl_get_display(wlr_egl) : EGL_NO_DISPLAY;
	const char *client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (egl_display == EGL_NO_DISPLAY || client_extensions == NULL) {
		fprintf(stderr, "cycle %d: EGL display/extensions unavailable\n", cycle);
		wl_display_destroy_clients(wayland_display);
		wlr_renderer_destroy(renderer);
		wl_display_destroy(wayland_display);
		return 1;
	}
	bool wlroots_terminates_display = has_extension(client_extensions,
		"EGL_KHR_display_reference");

	wl_display_destroy_clients(wayland_display);
	wlr_renderer_destroy(renderer);
	wl_display_destroy(wayland_display);

	if (!wlroots_terminates_display &&
			eglQueryString(egl_display, EGL_VERSION) != NULL &&
			eglTerminate(egl_display) != EGL_TRUE) {
		fprintf(stderr, "cycle %d: eglTerminate failed with 0x%x\n",
			cycle, eglGetError());
		return 1;
	}
	return 0;
}

int main(void) {
	const char *render_node = "/dev/dri/renderD128";
	int drm_fd = open(render_node, O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		if (errno == ENOENT || errno == EACCES || errno == EPERM) {
			fprintf(stderr, "SKIP: cannot access %s: %s\n", render_node,
				strerror(errno));
			return 77;
		}
		fprintf(stderr, "cannot open %s: %s\n", render_node, strerror(errno));
		return 1;
	}

	for (int cycle = 1; cycle <= 5; cycle++) {
		if (run_cycle(drm_fd, cycle) != 0) {
			close(drm_fd);
			return 1;
		}
	}
	close(drm_fd);
	puts("PASS: five GLES2 renderer teardown cycles completed");
	return 0;
}

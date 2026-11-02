#define _GNU_SOURCE
#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>

static const char *egl_error_name(EGLint error) {
	switch (error) {
	case EGL_SUCCESS: return "EGL_SUCCESS";
	case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
	case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
	case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
	case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
	case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
	case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
	case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
	case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
	case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
	case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
	case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
	case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
	case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
	case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
	default: return "EGL_UNKNOWN_ERROR";
	}
}

static void print_egl_failure(const char *call) {
	EGLint error = eglGetError();
	fprintf(stderr, "EGL_CONTROL_CALL=%s result=EGL_FALSE egl_error=%s(0x%04x)\n",
		call, egl_error_name(error), error);
}

static bool has_extension(const char *extensions, const char *extension) {
	if (extensions == NULL || extension == NULL) {
		return false;
	}
	size_t length = strlen(extension);
	const char *cursor = extensions;
	while ((cursor = strstr(cursor, extension)) != NULL) {
		bool left_boundary = cursor == extensions || cursor[-1] == ' ';
		bool right_boundary = cursor[length] == '\0' || cursor[length] == ' ';
		if (left_boundary && right_boundary) {
			return true;
		}
		cursor += length;
	}
	return false;
}

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

static int open_primary_device(void) {
	const char *requested = getenv("SEYRIX_DRM_PRIMARY");
	if (requested != NULL && requested[0] != '\0') {
		int fd = open(requested, O_RDWR | O_CLOEXEC);
		if (fd < 0) {
			perror("open SEYRIX_DRM_PRIMARY");
		} else {
			printf("EGL_CONTROL_DRM_PRIMARY=%s\n", requested);
		}
		return fd;
	}

	for (unsigned int index = 0; index < 16; index++) {
		char path[64];
		int length = snprintf(path, sizeof(path), "/dev/dri/card%u", index);
		if (length < 0 || (size_t)length >= sizeof(path)) {
			return -1;
		}
		int fd = open(path, O_RDWR | O_CLOEXEC);
		if (fd >= 0) {
			printf("EGL_CONTROL_DRM_PRIMARY=%s (first openable primary node)\n", path);
			return fd;
		}
	}
	fprintf(stderr, "EGL_CONTROL_DRM_PRIMARY=open failed for /dev/dri/card0..card15\n");
	return -1;
}

static char *get_render_node(int primary_fd) {
	char *path = drmGetRenderDeviceNameFromFd(primary_fd);
	if (path == NULL) {
		path = drmGetPrimaryDeviceNameFromFd(primary_fd);
	}
	return path;
}

static int run_cycle(int cycle, int cycles) {
	int primary_fd = -1;
	int gbm_fd = -1;
	int status = 1;
	char *render_node = NULL;
	struct gbm_device *gbm = NULL;
	EGLDisplay display = EGL_NO_DISPLAY;
	EGLContext context = EGL_NO_CONTEXT;
	const char *client_extensions = NULL;
	const char *display_extensions = NULL;

	printf("EGL_CONTROL_CYCLE_BEGIN=%d/%d\n", cycle, cycles);
	printf("EGL_CONTROL_ENV_LIBGL_ALWAYS_SOFTWARE=%s\n",
		getenv("LIBGL_ALWAYS_SOFTWARE") != NULL ? getenv("LIBGL_ALWAYS_SOFTWARE") : "unset");
	printf("EGL_CONTROL_ENV_WLR_RENDERER_ALLOW_SOFTWARE=%s\n",
		getenv("WLR_RENDERER_ALLOW_SOFTWARE") != NULL ? getenv("WLR_RENDERER_ALLOW_SOFTWARE") : "unset");

	primary_fd = open_primary_device();
	if (primary_fd < 0) {
		goto cleanup;
	}
	drmVersionPtr drm_version = drmGetVersion(primary_fd);
	if (drm_version != NULL) {
		printf("EGL_CONTROL_DRM_DRIVER=%s\n",
			drm_version->name != NULL ? drm_version->name : "unknown");
		printf("EGL_CONTROL_DRM_VERSION=%d.%d.%d\n",
			drm_version->version_major, drm_version->version_minor,
			drm_version->version_patchlevel);
		drmFreeVersion(drm_version);
	}

	render_node = get_render_node(primary_fd);
	if (render_node == NULL) {
		fprintf(stderr, "EGL_CONTROL_DRM_RENDER_NODE=unavailable\n");
		goto cleanup;
	}
	printf("EGL_CONTROL_DRM_RENDER_NODE=%s\n", render_node);
	gbm_fd = open(render_node, O_RDWR | O_CLOEXEC);
	if (gbm_fd < 0) {
		perror("open DRM render node");
		goto cleanup;
	}
	gbm = gbm_create_device(gbm_fd);
	if (gbm == NULL) {
		fprintf(stderr, "EGL_CONTROL_GBM_CREATE=failed\n");
		goto cleanup;
	}
	printf("EGL_CONTROL_GBM_CREATE=success\n");

	client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (client_extensions == NULL) {
		print_egl_failure("eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)");
		goto cleanup;
	}
	printf("EGL_CONTROL_CLIENT_EXTENSIONS=%s\n", client_extensions);
	if (!has_extension(client_extensions, "EGL_EXT_platform_base")) {
		fprintf(stderr, "EGL_CONTROL_REQUIRED_EXTENSION=EGL_EXT_platform_base missing\n");
		goto cleanup;
	}
	bool has_khr_gbm = has_extension(client_extensions, "EGL_KHR_platform_gbm");
	bool has_mesa_gbm = has_extension(client_extensions, "EGL_MESA_platform_gbm");
	if (!has_khr_gbm && !has_mesa_gbm) {
		fprintf(stderr,
			"EGL_CONTROL_REQUIRED_EXTENSION=EGL_KHR_platform_gbm/EGL_MESA_platform_gbm missing\n");
		goto cleanup;
	}
	printf("EGL_CONTROL_GBM_EXTENSIONS=KHR:%s MESA:%s\n",
		has_khr_gbm ? "yes" : "no", has_mesa_gbm ? "yes" : "no");
	printf("EGL_CONTROL_GBM_PLATFORM=0x%04x\n", EGL_PLATFORM_GBM_KHR);

	PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display =
		(PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (get_platform_display == NULL) {
		fprintf(stderr, "EGL_CONTROL_GET_PROC_ADDRESS=eglGetPlatformDisplayEXT unavailable\n");
		goto cleanup;
	}
	display = get_platform_display(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (display == EGL_NO_DISPLAY) {
		print_egl_failure("eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR)");
		goto cleanup;
	}
	printf("EGL_CONTROL_GET_PLATFORM_DISPLAY=success handle=%p\n", (void *)display);

	EGLint major = 0;
	EGLint minor = 0;
	if (eglInitialize(display, &major, &minor) != EGL_TRUE) {
		print_egl_failure("eglInitialize");
		goto cleanup;
	}
	printf("EGL_CONTROL_INITIALIZE=success version=%d.%d\n", major, minor);
	display_extensions = eglQueryString(display, EGL_EXTENSIONS);
	if (display_extensions == NULL) {
		print_egl_failure("eglQueryString(display, EGL_EXTENSIONS)");
		goto cleanup;
	}
	printf("EGL_CONTROL_DISPLAY_EXTENSIONS=%s\n", display_extensions);
	if (!has_extension(display_extensions, "EGL_KHR_surfaceless_context") ||
			(!has_extension(display_extensions, "EGL_KHR_no_config_context") &&
			 !has_extension(display_extensions, "EGL_MESA_configless_context"))) {
		fprintf(stderr, "EGL_CONTROL_REQUIRED_EXTENSION=configless/surfaceless GLES context missing\n");
		goto cleanup;
	}
	printf("EGL_CONTROL_EGL_VENDOR=%s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL_CONTROL_EGL_VERSION=%s\n", eglQueryString(display, EGL_VERSION));

	if (eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
		print_egl_failure("eglBindAPI(EGL_OPENGL_ES_API)");
		goto cleanup;
	}
	printf("EGL_CONTROL_BIND_API=success\n");
	const EGLint context_attributes[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE,
	};
	context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT,
		context_attributes);
	if (context == EGL_NO_CONTEXT) {
		print_egl_failure("eglCreateContext(configless GLES2)");
		goto cleanup;
	}
	printf("EGL_CONTROL_CREATE_CONTEXT=success\n");
	if (eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) != EGL_TRUE) {
		print_egl_failure("eglMakeCurrent(surfaceless)");
		goto cleanup;
	}
	printf("EGL_CONTROL_MAKE_CURRENT=success\n");
	const GLubyte *gl_vendor = glGetString(GL_VENDOR);
	const GLubyte *gl_renderer = glGetString(GL_RENDERER);
	const GLubyte *gl_version = glGetString(GL_VERSION);
	if (gl_vendor == NULL || gl_renderer == NULL || gl_version == NULL) {
		fprintf(stderr, "EGL_CONTROL_GL_IDENTITY=unavailable\n");
		goto cleanup;
	}
	printf("EGL_CONTROL_GL_VENDOR=%s\n", gl_vendor);
	printf("EGL_CONTROL_GL_RENDERER=%s\n", gl_renderer);
	printf("EGL_CONTROL_GL_VERSION=%s\n", gl_version);
	status = 0;

cleanup:
	if (display != EGL_NO_DISPLAY) {
		if (context != EGL_NO_CONTEXT &&
				eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
					EGL_NO_CONTEXT) != EGL_TRUE) {
			print_egl_failure("eglMakeCurrent(unbind)");
			status = 1;
		}
		if (context != EGL_NO_CONTEXT && eglDestroyContext(display, context) != EGL_TRUE) {
			print_egl_failure("eglDestroyContext");
			status = 1;
		}
		if (eglTerminate(display) != EGL_TRUE) {
			print_egl_failure("eglTerminate");
			status = 1;
		} else {
			printf("EGL_CONTROL_TERMINATE=success\n");
		}
	}
	if (eglReleaseThread() != EGL_TRUE) {
		print_egl_failure("eglReleaseThread");
		status = 1;
	}
	if (gbm != NULL) {
		gbm_device_destroy(gbm);
	}
	if (gbm_fd >= 0 && close(gbm_fd) != 0) {
		perror("close GBM DRM fd");
		status = 1;
	}
	if (primary_fd >= 0 && close(primary_fd) != 0) {
		perror("close primary DRM fd");
		status = 1;
	}
	free(render_node);
	printf("EGL_CONTROL_CYCLE_END=%d result=%s\n", cycle,
		status == 0 ? "pass" : "fail");
	return status;
}

int main(int argc, char **argv) {
	int cycles = parse_cycles(argc, argv);
	if (cycles < 1) {
		return 2;
	}
	int status = 0;
	for (int cycle = 1; cycle <= cycles; cycle++) {
		if (run_cycle(cycle, cycles) != 0) {
			status = 1;
			break;
		}
	}
	printf("EGL_CONTROL_EXIT=%d\n", status);
	return status;
}

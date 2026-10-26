#define _GNU_SOURCE
#define EGL_EGLEXT_PROTOTYPES

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
	int fd = -1;
	int status = 1;
	struct gbm_device *gbm = NULL;
	EGLDisplay display = EGL_NO_DISPLAY;
	EGLContext context = EGL_NO_CONTEXT;
	EGLSurface surface = EGL_NO_SURFACE;
	EGLConfig config;
	EGLint count = 0;
	EGLint egl_error = EGL_SUCCESS;
	EGLint context_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	EGLint config_attrs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_NONE,
	};
	EGLint pbuffer_attrs[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
	Dl_info egl_library = {0};
	void *egl_initialize_address = dlsym(RTLD_DEFAULT, "eglInitialize");

	if (egl_initialize_address != NULL &&
			dladdr(egl_initialize_address, &egl_library) != 0 &&
			egl_library.dli_fname != NULL) {
		printf("EGL_CONTROL_LIBRARY=%s\n", egl_library.dli_fname);
	}

	fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open /dev/dri/renderD128");
		goto cleanup;
	}
	gbm = gbm_create_device(fd);
	if (gbm == NULL) {
		fprintf(stderr, "EGL_CONTROL_GBM_CREATE=failed\n");
		goto cleanup;
	}
	display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, gbm, NULL);
	if (display == EGL_NO_DISPLAY) {
		egl_error = eglGetError();
		fprintf(stderr, "EGL_CONTROL_GET_PLATFORM_DISPLAY=failed egl_error=0x%x\n",
			egl_error);
		goto cleanup;
	}
	if (!eglInitialize(display, NULL, NULL)) {
		egl_error = eglGetError();
		fprintf(stderr, "EGL_CONTROL_INITIALIZE=failed egl_error=0x%x\n",
			egl_error);
		goto cleanup;
	}
	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		egl_error = eglGetError();
		fprintf(stderr, "EGL_CONTROL_BIND_API=failed egl_error=0x%x\n",
			egl_error);
		goto cleanup;
	}
	if (!eglChooseConfig(display, config_attrs, &config, 1, &count) || count != 1) {
		egl_error = eglGetError();
		fprintf(stderr,
			"EGL_CONTROL_CHOOSE_CONFIG=failed egl_error=0x%x config_count=%d\n",
			egl_error, count);
		goto cleanup;
	}
	context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attrs);
	surface = eglCreatePbufferSurface(display, config, pbuffer_attrs);
	if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
			!eglMakeCurrent(display, surface, surface, context)) {
		fprintf(stderr, "EGL_CONTROL_CONTEXT=failed egl_error=0x%x\n",
			eglGetError());
		goto cleanup;
	}

	printf("EGL_CONTROL_VENDOR=%s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL_CONTROL_VERSION=%s\n", eglQueryString(display, EGL_VERSION));
	printf("EGL_CONTROL_GL_VENDOR=%s\n", glGetString(GL_VENDOR));
	printf("EGL_CONTROL_GL_RENDERER=%s\n", glGetString(GL_RENDERER));
	fflush(stdout);
	status = 0;

cleanup:
	if (display != EGL_NO_DISPLAY) {
		if (context != EGL_NO_CONTEXT &&
				!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
					EGL_NO_CONTEXT)) {
			fprintf(stderr, "EGL_CONTROL_UNBIND=failed egl_error=0x%x\n",
				eglGetError());
			status = 1;
		}
		if (surface != EGL_NO_SURFACE && !eglDestroySurface(display, surface)) {
			fprintf(stderr, "EGL_CONTROL_DESTROY_SURFACE=failed egl_error=0x%x\n",
				eglGetError());
			status = 1;
		}
		if (context != EGL_NO_CONTEXT && !eglDestroyContext(display, context)) {
			fprintf(stderr, "EGL_CONTROL_DESTROY_CONTEXT=failed egl_error=0x%x\n",
				eglGetError());
			status = 1;
		}
		if (!eglTerminate(display)) {
			fprintf(stderr, "EGL_CONTROL_TERMINATE=failed egl_error=0x%x\n",
				eglGetError());
			status = 1;
		}
	}
	if (gbm != NULL) {
		gbm_device_destroy(gbm);
	}
	if (fd >= 0) {
		close(fd);
	}
	return status;
}

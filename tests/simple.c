#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <wayland-client-protocol.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <pthread.h>
#include <signal.h>

// global vars
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
//pthread_mutex_t cond  = PTHREAD_COND_INITIALIZER;

int status = 0;
struct wl_display * display = NULL;
struct wl_compositor *wl_compositor = NULL;
struct wl_shell * wl_shell;
EGLDisplay egl_display;

struct window {
	struct wl_egl_window * wl_egl_window;
	struct wl_surface * wl_surface;
	struct wl_shell * wl_shell;
	struct wl_shell_surface * wl_shell_surface;
	struct wl_region * wl_region;
	struct wl_event_queue * wl_event_queue;

	EGLConfig egl_conf;
	EGLSurface egl_surface;
	EGLContext egl_context;
};

struct wl_shell_surface *shell_surface;


static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
						const char *interface, uint32_t version)
{
    printf("Got a registry event for %s id %d\n", interface, id);
    if (strcmp(interface, "wl_compositor") == 0) {
        wl_compositor = wl_registry_bind(registry,
									  id,
									  &wl_compositor_interface,
									  1);
    } else if (strcmp(interface, "wl_shell") == 0) {
		wl_shell = wl_registry_bind(registry, id,
									&wl_shell_interface, 1);

    }
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id)
{
    printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler,
    global_registry_remover
};


static int
egl_dpy_init()
{
    EGLint major, minor, count, n, size;
    EGLConfig *configs;
    int i;
    EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
    };

    static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
    };

    egl_display = eglGetDisplay((EGLNativeDisplayType) display);
    if (egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "Can't create egl display\n");
		return -1;
    } else {
		fprintf(stderr, "Created egl display\n");
    }

    if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
		fprintf(stderr, "Can't initialise egl display\n");
		return -1;
    }
    printf("EGL major: %d, minor %d\n", major, minor);

	return 0;
}

static void
egl_dpy_fini()
{
}

static int
global_init(void) {

    display = wl_display_connect(NULL);
    if (display == NULL) {
		fprintf(stderr, "Can't connect to display\n");
		return -1;
    }
    printf("connected to display\n");

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (wl_compositor == NULL || wl_shell == NULL) {
		fprintf(stderr, "Can't find wl_compositor or shell\n");
		return -1;
    } else {
		fprintf(stderr, "Found wl_compositor and shell\n");
    }

	egl_dpy_init();

	return 0;
}

static int
win_egl_ctx_init(struct window * win)
{
    int i;
    EGLint major, minor, count, n, size;
    EGLConfig *configs;

    EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
    };

    static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
    };

    eglGetConfigs(egl_display, NULL, 0, &count);
    printf("EGL has %d configs\n", count);

    configs = calloc(count, sizeof *configs);
    eglChooseConfig(egl_display, config_attribs,
					configs, count, &n);
    for (i = 0; i < n; i++) {
		eglGetConfigAttrib(egl_display,
						   configs[i], EGL_BUFFER_SIZE, &size);
		printf("Buffer size for config %d is %d\n", i, size);
		eglGetConfigAttrib(egl_display,
						   configs[i], EGL_RED_SIZE, &size);
		printf("Red size for config %d is %d\n", i, size);
		// just choose the first one
		win->egl_conf = configs[i];
		break;
    }

    win->egl_context =
		eglCreateContext(egl_display,
						 win->egl_conf,
						 EGL_NO_CONTEXT, context_attribs);
	free(configs);

	return 0;
}

static void
win_egl_ctx_fini(struct window * win)
{
}


static int
win_render(struct window * win)
{
	static float det = 0.001f;
	static float bk = 0.0f;
	bk += det;
	if (bk > 1) {
		det = -0.001f;
	} else if (bk < 0) {
		det = 0.001f;
	}

	glClearColor(bk, bk, bk, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glFlush();

    if (!eglSwapBuffers(egl_display, win->egl_surface)) {
		fprintf(stderr, "Swapped buffers failed\n");
    }

	return 0;
}


static int
win_egl_surf_init(struct window * win) {
	win->wl_egl_window =
		wl_egl_window_create(win->wl_surface,
							 320, 200);
    if (win->wl_egl_window == EGL_NO_SURFACE) {
		fprintf(stderr, "Can't create egl window\n");
		return -1;
	} else {
		fprintf(stderr, "Created egl window\n");
	}

	win->egl_surface = eglCreateWindowSurface(egl_display,
										 win->egl_conf,
										 win->wl_egl_window, NULL);

	if (!win->egl_surface) {
		fprintf(stderr, "eglCreateWindowSurface failed\n");
		return -1;
	}

    if (eglMakeCurrent(egl_display,
					   win->egl_surface,
					   win->egl_surface,
					   win->egl_context)) {
		fprintf(stderr, "Made current\n");
    } else {
		fprintf(stderr, "Made current failed\n");
		return -1;
    }
	win_render(win);
	return 0;
}

static void
win_egl_surf_fini(struct window * win)
{
	//TODO
}

static int
window_init(struct window * win)
{
	win->wl_event_queue = wl_display_create_queue(display);

    win->wl_surface = wl_compositor_create_surface(wl_compositor);
    if (win->wl_surface == NULL) {
		fprintf(stderr, "Can't create surface\n");
		return -1;
    } else {
		fprintf(stderr, "Created surface\n");
    }
	wl_proxy_set_queue((struct wl_proxy *)win->wl_surface,
					   win->wl_event_queue);

    win->wl_shell_surface =
		wl_shell_get_shell_surface(wl_shell,
								   win->wl_surface);

	wl_proxy_set_queue((struct wl_proxy *)win->wl_shell_surface,
					   win->wl_event_queue);

    wl_shell_surface_set_toplevel(win->wl_shell_surface);

	win->wl_region = wl_compositor_create_region(wl_compositor);
	wl_region_add(win->wl_region, 0, 0, 480, 360);
    wl_surface_set_opaque_region(win->wl_surface, win->wl_region);

    win_egl_ctx_init(win);
    win_egl_surf_init(win);

	return 0;
}

static void *
win_thread(void *param) {
	struct window * win = param;
	window_init(win);

	int quit = 0;
    while (!quit) {
		pthread_mutex_lock(&mutex);
		quit = status == -1 ? 1 : 0;
		pthread_mutex_unlock(&mutex);

		if (!quit && wl_display_dispatch_queue(display, win->wl_event_queue) != -1) {
			win_render(win);
		}
    }
	return NULL;
}

int main(int argc, char **argv)
{
	global_init();

	sigset_t sigset;
	siginfo_t seginfo;
	sigfillset(&sigset);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	int i, num = 20;
	struct window win[100];
	pthread_t tid[100];
	for (i=0; i< num; i++) {
		pthread_create(&tid[i], NULL, win_thread, (void *)&win[i]);
	}

	while (1) {
		printf("sigwaitinfo:\n");
		if (sigwaitinfo(&sigset, &seginfo) == -1) {
			printf("siginfo: si_signo = %x \n", seginfo.si_signo);
			continue;
		}
		printf("siginfo: si_signo = %x \n", seginfo.si_signo);
		if (seginfo.si_signo == SIGTERM ||
			seginfo.si_signo == SIGINT) {
			break;
		}
	}
	printf("cleanup...\n");

	// emit exit signal
	pthread_mutex_lock(&mutex);
	status = -1;
	//pthread_cond_broadcast(&cond);
	pthread_mutex_unlock(&mutex);

	printf("waiting for threads ...");
	for(i=0; i<num; i++) {
		pthread_join(tid[i], NULL);
	}
	printf("done!\n");

    /* while (wl_display_dispatch(display) != -1) { */
	/* 	; */
    /* } */

    wl_display_disconnect(display);
    printf("disconnected from display\n");

    return 0;
}

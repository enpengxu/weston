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

static char * tabs[] = {
	"",
	"\t",
	"\t\t",
	"\t\t\t",
	"\t\t\t\t",
	"\t\t\t\t\t",
	"\t\t\t\t\t\t",
	"\t\t\t\t\t\t\t\t",
	"\t\t\t\t\t\t\t\t\t",
	"\t\t\t\t\t\t\t\t\t\t",
	"\t\t\t\t\t\t\t\t\t\t\t",
};

// debug log output
#define dlog(...)														\
	if(verbose >= log_verbose) {										\
		printf("%s:%d : %s", __FUNCTION__, __LINE__, tabs[log_verbose]); \
		printf(__VA_ARGS__);											\
	}

#define FAILED_IF(rc)							\
	if (rc) {									\
		dlog("Failed, rc = %d \n", rc);		\
		return rc;								\
	}

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

static void registry_handle_global(void *data,
								   struct wl_registry *registry,
								   uint32_t id,
								   const char *interface,
								   uint32_t version);
static void registry_handle_global_remover(void *data,
										   struct wl_registry *registry,
										   uint32_t id);


// global vars
static __thread int log_verbose = 1;
static int verbose = 0;
static int status = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
struct wl_display * wl_display = NULL;
struct wl_compositor *wl_compositor = NULL;
struct wl_shell * wl_shell;
static EGLDisplay egl_display;

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remover
};


static void
registry_handle_global(void *data,
					   struct wl_registry *registry,
					   uint32_t id,
					   const char *interface,
					   uint32_t version)
{
    dlog("Got a registry event for %s id %d\n", interface, id);
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
registry_handle_global_remover(void *data,
							   struct wl_registry *registry,
							   uint32_t id)
{
    dlog("Got a registry losing event for %d\n", id);
}

static int
sys_egl_dpy_init()
{
	int i, rc = 0;
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
	log_verbose ++;

    egl_display = eglGetDisplay((EGLNativeDisplayType) wl_display);
    if (egl_display == EGL_NO_DISPLAY) {
		rc = -1;
		dlog("Can't create egl display\n");
		goto failed;
    }
	dlog("Created egl display\n");

    if (eglInitialize(egl_display, &major, &minor) != EGL_TRUE) {
		rc = -1;
		dlog("Can't initialise egl display\n");
		goto failed;
    }
    dlog("EGL major: %d, minor %d\n", major, minor);

  failed:
	log_verbose --;
	return 0;
}

static void
sys_egl_dpy_fini()
{
	eglTerminate(egl_display);
	eglReleaseThread();
}

static int
sys_init(void) {

    wl_display = wl_display_connect(NULL);
    if (wl_display == NULL) {
		dlog("Can't connect to display\n");
		return -1;
    }
    dlog("connected to display\n");

    struct wl_registry *registry = wl_display_get_registry(wl_display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_dispatch(wl_display);
    wl_display_roundtrip(wl_display);

    if (wl_compositor == NULL || wl_shell == NULL) {
		dlog("Can't find wl_compositor or shell\n");
		return -1;
    } else {
		dlog("Found wl_compositor and shell\n");
    }

	sys_egl_dpy_init();

	return 0;
}

static void
sys_fini()
{
	sys_egl_dpy_fini();

    wl_display_disconnect(wl_display);
    dlog("disconnected from display\n");
}

static int
win_egl_ctx_init(struct window * win)
{
    int i, rc = 0;
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

	log_verbose ++;

    eglGetConfigs(egl_display, NULL, 0, &count);
    dlog("EGL has %d configs\n", count);

    configs = calloc(count, sizeof *configs);
    eglChooseConfig(egl_display, config_attribs,
					configs, count, &n);
    for (i = 0; i < n; i++) {
		eglGetConfigAttrib(egl_display,
						   configs[i], EGL_BUFFER_SIZE, &size);
		dlog("Buffer size for config %d is %d\n", i, size);
		eglGetConfigAttrib(egl_display,
						   configs[i], EGL_RED_SIZE, &size);
		dlog("Red size for config %d is %d\n", i, size);
		// pickup the first one
		win->egl_conf = configs[i];
		break;
    }

    win->egl_context =
		eglCreateContext(egl_display,
						 win->egl_conf,
						 EGL_NO_CONTEXT, context_attribs);
	free(configs);

	log_verbose --;
	return rc;
}

static void
win_egl_ctx_fini(struct window * win)
{
	eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
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
		dlog("Swapped buffers failed\n");
    }

	return 0;
}


static int
win_egl_surf_init(struct window * win)
{
	int rc = 0;

	log_verbose ++;

	win->wl_egl_window =
		wl_egl_window_create(win->wl_surface,
							 320, 200);
    if (win->wl_egl_window == EGL_NO_SURFACE) {
		rc = -1;
		dlog("Can't create egl window\n");
		goto failed;
	} else {
		dlog("Created egl window\n");
	}

	win->egl_surface = eglCreateWindowSurface(egl_display,
										 win->egl_conf,
										 win->wl_egl_window, NULL);

	if (!win->egl_surface) {
		rc = -1;
		dlog("eglCreateWindowSurface failed\n");
		goto failed;
	}

    if (eglMakeCurrent(egl_display,
					   win->egl_surface,
					   win->egl_surface,
					   win->egl_context)) {
		dlog("Made current\n");
    } else {
		rc = -1;
		dlog("Made current failed\n");
		goto failed;
    }
	win_render(win);

  failed:
	log_verbose --;
	return rc;
}

static void
win_egl_surf_fini(struct window * win)
{
	eglDestroySurface(egl_display, win->egl_surface);
	wl_egl_window_destroy(win->wl_egl_window);
}

static int
win_wl_init(struct window * win)
{
	int rc = 0;
	log_verbose ++;

	win->wl_event_queue = wl_display_create_queue(wl_display);

    win->wl_surface = wl_compositor_create_surface(wl_compositor);
    if (win->wl_surface == NULL) {
		rc = -1;
		dlog("Can't create surface\n");
		goto failed;
    } else {
		dlog("Created surface\n");
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

  failed:
	log_verbose --;
	return rc;
}

static int
win_wl_fini(struct window * win)
{
	log_verbose ++;

	dlog("wl_shell_surface_destroy\n");
	wl_shell_surface_destroy(win->wl_shell_surface);

	dlog("wl_surface_destroy\n");
    wl_surface_destroy(win->wl_surface);

	dlog("wl_region_destroy\n");
	wl_region_destroy(win->wl_region);

	dlog("wl_event_queue_destroy\n");
	wl_event_queue_destroy(win->wl_event_queue);

	log_verbose --;
	return 0;
}

static int
win_init(struct window * win)
{
	int rc;
	dlog("win_init \n");

	rc = win_wl_init(win);
	FAILED_IF(rc);

	dlog("win_egl_ctx_init ...\n");
	rc = win_egl_ctx_init(win);
	FAILED_IF(rc);

	dlog("win_egl_surf_init ...\n");
	rc = win_egl_surf_init(win);
	FAILED_IF(rc);

	return rc;
}

static void
win_fini(struct window * win)
{
	// correct order should be :
	// wl_fini
	// egl_surf_fini
	// egl_ctx_fini
	// but we will get troubles in wayland egl driver(damage_thread),
	// it try to access a surface proxy but it has been destroied.
	// make a work around here.

	dlog("win_egl_surf_fini ...\n");
    win_egl_surf_fini(win);

	dlog("win_egl_ctx_fini ...\n");
	win_egl_ctx_fini(win);

	dlog("win_wl_fini ...\n");
	win_wl_fini(win);
}

static void *
win_thread(void *param) {
	struct window * win = param;

	dlog("win_init ...\n");

	win_init(win);

	int quit = 0;
    while (!quit) {
		if (wl_display_dispatch_queue(wl_display, win->wl_event_queue) != -1) {
			win_render(win);
		}
		pthread_mutex_lock(&mutex);
		quit = status == -1 ? 1 : 0;
		pthread_mutex_unlock(&mutex);
	}

	dlog("win_fini ...\n");

	win_fini(win);

	return NULL;
}

#define MAX_THREAD_NUM 256
int main(int argc, char **argv)
{
	int i, num = 1;
	struct window win[MAX_THREAD_NUM];
	pthread_t tid[MAX_THREAD_NUM];

	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i], "-verbose", strlen("-verbose")) == 0) {
			verbose = atoi(argv[i] + strlen("-verbose="));
		}
		if (strncmp(argv[i], "-threads=", strlen("-threads=")) == 0) {
			num = atoi(argv[i] + strlen("-threads="));
			if (num > MAX_THREAD_NUM) {
				num = MAX_THREAD_NUM;
			}
		}
	}

	// init wayland winsys
	sys_init();

	// block all signals
	sigset_t sigset;
	siginfo_t seginfo;
	sigfillset(&sigset);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);

	// create threads
	for (i=0; i< num; i++) {
		pthread_create(&tid[i], NULL, win_thread, (void *)&win[i]);
	}

	// main thread loop,waiting for signals to exit.
	while (1) {
		dlog("sigwaitinfo:\n");
		if (sigwaitinfo(&sigset, &seginfo) == -1) {
			dlog("siginfo: si_signo = %x \n", seginfo.si_signo);
			continue;
		}
		dlog("siginfo: si_signo = %x \n", seginfo.si_signo);

		// exit if get SIGTERM or SIGINT
		if (seginfo.si_signo == SIGTERM ||
			seginfo.si_signo == SIGINT ) {
			break;
		}
	}
	dlog("cleanup...\n");

	pthread_mutex_lock(&mutex);
	status = -1;
	pthread_mutex_unlock(&mutex);

	dlog("joining windows threads ...\n");

	for(i=0; i<num; i++) {
		pthread_join(tid[i], NULL);
	}

	dlog("done!\n");

	sys_fini();
    return 0;
}

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "config.h"


/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY          0
#define XEMBED_WINDOW_ACTIVATE          1
#define XEMBED_WINDOW_DEACTIVATE        2
#define XEMBED_REQUEST_FOCUS            3
#define XEMBED_FOCUS_IN                 4
#define XEMBED_FOCUS_OUT                5
#define XEMBED_FOCUS_NEXT               6
#define XEMBED_FOCUS_PREV               7
/* 8-9 were used for XEMBED_GRAB_KEY/XEMBED_UNGRAB_KEY */
#define XEMBED_MODALITY_ON              10
#define XEMBED_MODALITY_OFF             11
#define XEMBED_REGISTER_ACCELERATOR     12
#define XEMBED_UNREGISTER_ACCELERATOR   13
#define XEMBED_ACTIVATE_ACCELERATOR     14

/* Details for  XEMBED_FOCUS_IN: */
#define XEMBED_FOCUS_CURRENT            0
#define XEMBED_FOCUS_FIRST              1
#define XEMBED_FOCUS_LAST               2

enum { XEmbed, WMLast }; /* default atoms */

void expose(const XEvent *);
void maprequest(const XEvent *);
void createnotify(const XEvent *);
void destroynotify(const XEvent *);
void unmapnotify(const XEvent *);
void keypress(const XEvent *);
void configurenotify(const XEvent *e);
void focusin(const XEvent *e);
void configurerequest(const XEvent *e);

void manage(Window , int , int );
void unmanage(Window w);
void resize(Window win, int w, int h);
void sendxembed(Window win, long msg, long detail, long d1, long d2);
void focus(Window win);
static void grabkeyboard(void);
static void grabfocus(void);

void logevent(const char *name, Window w) ;
void spawnchild(long int, int n, char **);
void lock();
void _unlock();
void noop();
void die(const char *errstr, ...);

void (*unlock)() = noop;

int pid;
int running = 1;
Display *dpy;
Window root, win, cwin = 0, focuswin;
static Atom wmatom[WMLast];
static int wx, wy, ww, wh, rwh, rww;

char lockpath[PATHSIZE] = { '\0' };
FILE *logfile = NULL;

static void (*handler[LASTEvent]) (const XEvent *) = {
	[ConfigureNotify] = configurenotify,
	[ConfigureRequest] = configurerequest,
	[CreateNotify] = createnotify,
	//[MapRequest] = maprequest,
    [UnmapNotify] = unmapnotify,
	[DestroyNotify] = destroynotify,
	//[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[KeyRelease] = keypress,
};


void expose(const XEvent *e) {
    const XExposeEvent *ev=&e->xexpose;
    logevent("Expose", ev->window);
}

void 
maprequest(const XEvent *e)
{
	const XMapRequestEvent *ev = &e->xmaprequest;
    logevent("MapRequest", ev->window);
}

void 
createnotify(const XEvent *e)
{
	const XCreateWindowEvent *ev = &e->xcreatewindow;
    logevent("CreateNotify", ev->window);

	if (ev->window != win && cwin == 0) {
        manage(ev->window, ev->width, ev->height);
    }
}

void 
destroynotify(const XEvent *e) {

	const XDestroyWindowEvent *ev = &e->xdestroywindow;
    logevent("DestroyNotify", ev->window);
    if (ev->window == cwin)
        unmanage(ev->window);
    running = 0;
}

void 
unmapnotify(const XEvent *e) {

	const XUnmapEvent *ev = &e->xunmap;
    logevent("Unmap", ev->window);
    if (ev->window == cwin)
        unmanage(ev->window);
    running = 0;
}

void
configurenotify(const XEvent *e)
{
	const XConfigureEvent *ev = &e->xconfigure;
    logevent("ConfigureNotify", ev->window);

	if (ev->window == win && (ev->width != ww || ev->height != wh)) {
		ww = ev->width;
		wh = ev->height;
		if (cwin > 0)
			resize(cwin, ww, wh);
		XSync(dpy, False);
	}
}

void
focusin(const XEvent *e)
{
	const XFocusChangeEvent *ev = &e->xfocus;
	int dummy;
	Window focused;

    logevent("FocusIn", ev->window);
    
	if (ev->mode != NotifyUngrab) {
		XGetInputFocus(dpy, &focused, &dummy);
        logevent("FocusIn(focused)", focused);
		if (cwin > 0 && focused == cwin )
			focus(cwin);
	}
}

void
configurerequest(const XEvent *e)
{
	const XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;
    logevent("ConfiguRerequest", ev->window);

	if (cwin > 0) {
		wc.x = 0;
		wc.y = 0;
		wc.width = ww;
		wc.height = wh;
		wc.border_width = 0;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, cwin, ev->value_mask, &wc);
	}
}

void
keypress(const XEvent *e)
{

	XKeyEvent ev = e->xkey;
    logevent("KeyEvent", ev.window);
    if (cwin == 0) 
        return;
    ev.window=cwin;
    XSendEvent(dpy, cwin, False, NoEventMask, (XEvent *)&ev);
    XSync(dpy, False);
}

void manage(Window w, int ww, int wh) {

    XEvent e;
    logevent("Mange Window", w);

    cwin = w;

    XMoveWindow(dpy, win,  (rww-ww)/2, (rwh-wh)/2);
    XResizeWindow(dpy, win, ww, wh);
    XMapWindow(dpy, win);
    XSync(dpy, False);
    grabfocus();
    grabkeyboard();

    XWithdrawWindow(dpy, w, 0);
    XReparentWindow(dpy, w, win, 0, 0);
    XSelectInput(dpy, w, PropertyChangeMask |
            StructureNotifyMask | EnterWindowMask);
    XSync(dpy, False);
    XLowerWindow(dpy, w);
    XMapWindow(dpy, w);

    e.xclient.window = w;
    e.xclient.type = ClientMessage;
    e.xclient.message_type = wmatom[XEmbed];
    e.xclient.format = 32;
    e.xclient.data.l[0] = CurrentTime;
    e.xclient.data.l[1] = XEMBED_EMBEDDED_NOTIFY;
    e.xclient.data.l[2] = 0;
    e.xclient.data.l[3] = win;
    e.xclient.data.l[4] = 0;
    XSendEvent(dpy, root, False, NoEventMask, &e);
    XSync(dpy, False);
    focus(w);
}

void unmanage(Window w) {
    logevent("Unmange Window", w);
    XSync(dpy, False);
    cwin = 0;
}

void
focus(Window w) 
{
    logevent("Focus Window", w);
	resize(w, ww, wh);
	XRaiseWindow(dpy, w);
	XSetInputFocus(dpy, w, RevertToParent, CurrentTime);
	sendxembed(w, XEMBED_FOCUS_IN, XEMBED_FOCUS_CURRENT, 0, 0);
	sendxembed(w, XEMBED_WINDOW_ACTIVATE, 0, 0, 0);
	XSync(dpy, False);
}

void
sendxembed(Window win, long msg, long detail, long d1, long d2) {
	XEvent e = { 0 };

	e.xclient.window = win;
	e.xclient.type = ClientMessage;
	e.xclient.message_type = wmatom[XEmbed];
	e.xclient.format = 32;
	e.xclient.data.l[0] = CurrentTime;
	e.xclient.data.l[1] = msg;
	e.xclient.data.l[2] = detail;
	e.xclient.data.l[3] = d1;
	e.xclient.data.l[4] = d2;
	XSendEvent(dpy, win, False, NoEventMask, &e);
}

void
resize(Window win, int w, int h)
{
	XConfigureEvent ce;
	XWindowChanges wc;

	ce.x = 0;
	ce.y = 0;
	ce.width = wc.width = w;
	ce.height = wc.height = h;
	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = win;
	ce.window = win;
	ce.above = None;
	ce.override_redirect = False;
	ce.border_width = 0;

	XConfigureWindow(dpy, win, CWWidth | CWHeight, &wc);
	XSendEvent(dpy, win, False, StructureNotifyMask,
	           (XEvent *)&ce);
}

static void
grabkeyboard(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000  };
	int i;

	/* try to grab keyboard, we may have to wait for another process to ungrab */
	for (i = 0; i < 1000; i++) {
		if (XGrabKeyboard(dpy, win, True, GrabModeSync,
		                  GrabModeAsync, CurrentTime) == GrabSuccess)
			return;
		nanosleep(&ts, NULL);
	}
	die("cannot grab keyboard");
}

static void
grabfocus(void)
{
	struct timespec ts = { .tv_sec = 0, .tv_nsec = 10000000  };
	Window focuswin;
	int i, revertwin;

	for (i = 0; i < 100; ++i) {
		XGetInputFocus(dpy, &focuswin, &revertwin);
		if (focuswin == win)
			return;
		XSetInputFocus(dpy, win, RevertToPointerRoot, CurrentTime);
		nanosleep(&ts, NULL);
	}
	die("cannot grab focus");
}
 
void lock() {
    char *home = getenv("HOME");
    if (home == NULL)
        die("No HOME variable\n");
    int r = snprintf(lockpath, PATHSIZE, "%s/.modallock", home);
    if (r >= PATHSIZE) {
        die("HOME path is too length\n");
    }
    r = open(lockpath, O_CREAT|O_EXCL, 0666);
    if (r == -1) {
        die("Can't obtain lock: %s\n", strerror(errno));
    }
    close(r);
    unlock = _unlock;
}

void _unlock() {
    if (lockpath[0] == '\0') {
        return;
    }
    int r = unlink(lockpath);
    if (r == -1) {
        lockpath[0] = '\0';
        die("Can't release lock: %s\n", strerror(errno));
    }
}


void logevent(const char *name, Window w) 
{
    if ((logfile == NULL) && strcmp(LOGFILE, "") == 0) {
        logfile = stdout;
    }
    else if (logfile == NULL) {
        char path[PATHSIZE];
        char *home = getenv("HOME");
        if (home == NULL)
            die("No HOME variable\n");
        int r = snprintf(path, PATHSIZE, "%s/%s", home, LOGFILE);
        if (r >= PATHSIZE) {
            die("HOME variable and LOGFILE is too length\n");
        }
        logfile = fopen(path, "w+");
        if (logfile == NULL) {
            die("Can't open log file: %s\n", strerror(errno));
        }
    }
    fprintf(logfile, "%s: %ld %s\n", name, w, (w==win)? "p" : ((w==cwin)?"c":"u"));
}

void noop() {}

void
die(const char *errstr, ...)
{
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
    unlock();
	exit(1);
}

void
spawnchild(long int wid, int n, char **args) {
    char win[20];
    snprintf(win, 20, "%ld", wid);
    printf("XXX: --%s--%ld--\n", win, wid);
    int r = setenv("XEMBED", win, 1);
    if (r == -1) {
        die("Can not set XEMBED: %s\n", strerror(errno));
    }
    pid = fork();
    if (pid == -1) {
        die("fork: %s\n", errno);
    } else if (pid == 0) {
        char *argv[n+1];
        for (int i=0;i<n;i++)
            argv[i] = args[i];
        argv[n]=NULL;
        r = execvp(args[0], argv);
        if (r == -1)
            die("execv: %s\n", strerror(errno));
    }
}

int waitchild() {
    int status;
    int r = waitpid(pid, &status, 0);
    if (r == -1) {
        die("waitpid: %s\n", strerror(errno));
    }
    if (!WIFEXITED(status)) {
        die("Child process terminate unexpectedly\n");
    } 
    return WEXITSTATUS(status);
}

void 
init(void) {
    XSetWindowAttributes swa;
    XWindowAttributes xwa;
    int s;
    int revertwin;

    lock();
    dpy = XOpenDisplay(NULL);
    if (dpy == NULL) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }
    wmatom[XEmbed] = XInternAtom(dpy, "_XEMBED", False);

    s = DefaultScreen(dpy);
    root = RootWindow(dpy, s);
    XGetWindowAttributes(dpy, root, &xwa);
    rww = xwa.width;
    rwh = xwa.height;
    wx = wy = 1;
    wh = ww = 1;

    swa.override_redirect = True;
    swa.event_mask = SubstructureNotifyMask | FocusChangeMask |
        ButtonPressMask | ExposureMask | KeyPressMask |
        PropertyChangeMask | StructureNotifyMask |
        SubstructureRedirectMask;
    win = XCreateWindow(dpy, root, wx, wy, ww, wh, 0,
            CopyFromParent, CopyFromParent, CopyFromParent,
            CWOverrideRedirect |
            CWEventMask, &swa);
    printf("Window %ld\n", win);
    XGetInputFocus(dpy, &focuswin, &revertwin);
}

void
run() {
    XEvent e;
    while (running) {
        XNextEvent(dpy, &e);
        if (handler[e.type])
            (handler[e.type])(&e); /* call handler */
    }
}

int fini() {
    int r = waitchild();
    printf("child process exited with %d\n", r);
    XUngrabKeyboard(dpy, CurrentTime);
    XDestroyWindow(dpy, win);
    XSetInputFocus(dpy, focuswin, RevertToPointerRoot, CurrentTime);
    XSync(dpy, False);
    XCloseDisplay(dpy);
    unlock();
    return 0;
}
int
main(int n, char **args) {
    init();
    
    spawnchild(win, n-1, &args[1]);
    run();
    fini();
}

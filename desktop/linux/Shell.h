/* IED's desktop window on Linux (docs/27 "Linux"): GTK 3 and WebKitGTK behind a small C interface the C4 app binds
   (desktop/linux/GtkShell.ch). Shell.c loads them when the app starts (dlopen), so the program needs neither at build
   time and a machine without WebKitGTK still runs it: shell_start says what is missing and the app opens the browser.
   Everything but shell_post runs on the UI thread: the thread that called shell_start. */
#ifndef IED_DESKTOP_SHELL_H
#define IED_DESKTOP_SHELL_H

/* A window is a handle (void*): its structure is Shell.c's own. */

/* What a window tells its owner; `context` goes back with each. All on the UI thread. */
typedef struct ShellEvents {
	void* context;
	/* A message the page posted (window.webkit.messageHandlers.ied.postMessage), with the page's address then. */
	void (*message)(void* context, const char* source, const char* text);
	/* A navigation the page starts: in this window, or (newWindow 1) in a new one; 1: it goes ahead, 0: it is stopped
	   (the owner sent it elsewhere). userGesture: the person clicked or typed for it. */
	int (*navigation)(void* context, const char* url, int newWindow, int userGesture);
	void (*title)(void* context, const char* title);
	/* The page did not load (not a stop the app or the page made): its address and why. 1: the owner showed a page of
	   its own in its place. */
	int (*failed)(void* context, const char* url, const char* why);
	/* The window is about to close (the person closed it, or shell_window_close), then it is gone. */
	void (*closing)(void* context);
	void (*closed)(void* context);
	/* The page's web process ended. */
	void (*crashed)(void* context);
} ShellEvents;

/* A window's place on the screen in its normal state, and whether it is maximized. `known` 0: the system chooses. */
typedef struct ShellPlacement {
	int known;
	int x;
	int y;
	int width;
	int height;
	int maximized;
} ShellPlacement;

/* Loads GTK and WebKitGTK and opens the display; the windows' web data (the signed-in session's cookie, caches) is
   kept under dataFolder, and iconFile (may be empty) is every window's icon. 0, with a sentence in error, when they
   cannot be loaded or there is no display. */
int shell_start(const char* dataFolder, const char* iconFile, char* error, int errorSize);

/* A new window showing nothing yet, `script` added before each page's own scripts. */
void* shell_window_new(const ShellEvents* events, const char* script, const ShellPlacement* placement, int minWidth, int minHeight);
void shell_window_load(void* window, const char* url);
/* A page of the app's own (an error page), as about:blank. */
void shell_window_load_html(void* window, const char* html);
/* The page's address now, cut to outSize - 1 bytes; "" before the first. */
void shell_window_uri(void* window, char* out, int outSize);
void shell_window_placement(void* window, ShellPlacement* placement);
void shell_window_set_title(void* window, const char* title);
/* As the person closing it: `closing`, then the window goes and `closed`. */
void shell_window_close(void* window);
/* Raised and given the focus (restored when minimized); the desktop may only mark it as wanting attention. */
void shell_window_present(void* window);
void shell_window_inspect(void* window);
void shell_window_reload(void* window);
/* A modal message over the window (or over none). */
void shell_window_alert(void* window, const char* text);
/* The system's folder picker over the window: 1 with the folder in out, 0 when the person cancelled. */
int shell_pick_folder(void* window, char* out, int outSize);

/* Where the pointer is on the screen (0, 0 where the system does not say, on Wayland). */
void shell_pointer(int* x, int* y);
/* Runs the UI until shell_quit. */
void shell_run(void);
void shell_quit(void);
/* From any thread: work(data) on the UI thread, soon. */
void shell_post(void (*work)(void*), void* data);

#endif

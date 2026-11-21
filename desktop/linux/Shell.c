/* Shell.h's windows: GTK 3 and WebKitGTK (the 4.1 API, else 4.0), looked up when the app starts. Only the functions
   below are used, each declared here with the types they take, so nothing of GTK's or WebKit's headers is needed to
   build: their objects are opaque pointers, and the few structures read (GError, GdkRGBA, a type instance's class)
   are spelled out as GLib lays them out. */

#define _GNU_SOURCE   /* setenv, in C11 mode */
#include "Shell.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int gboolean;
typedef void* gpointer;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef size_t GType;
typedef unsigned int GQuark;

typedef struct ShellError { GQuark domain; int code; char* message; } ShellError;   /* GError */
typedef struct ShellRgba { double red; double green; double blue; double alpha; } ShellRgba;   /* GdkRGBA */
typedef struct ShellTypeClass { GType type; } ShellTypeClass;   /* GTypeClass */
typedef struct ShellTypeInstance { ShellTypeClass* klass; } ShellTypeInstance;   /* GTypeInstance */

enum {
	kWindowToplevel = 0,               /* GTK_WINDOW_TOPLEVEL */
	kDialogModal = 1,                  /* GTK_DIALOG_MODAL */
	kDialogDestroyWithParent = 2,      /* GTK_DIALOG_DESTROY_WITH_PARENT */
	kMessageError = 3,                 /* GTK_MESSAGE_ERROR */
	kButtonsOk = 1,                    /* GTK_BUTTONS_OK */
	kChooseFolder = 2,                 /* GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER */
	kResponseAccept = -3,              /* GTK_RESPONSE_ACCEPT */
	kInjectTopFrame = 1,               /* WEBKIT_USER_CONTENT_INJECT_TOP_FRAME */
	kInjectAtStart = 0,                /* WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START */
	kCookiesSqlite = 1,                /* WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE */
	kDecideNavigation = 0,             /* WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION */
	kDecideNewWindow = 1,              /* WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION */
	kNetworkCancelled = 302            /* WEBKIT_NETWORK_ERROR_CANCELLED */
};

/* The functions used, found by name in the loaded libraries. */
static struct {
	void (*g_set_prgname)(const char*);
	gulong (*g_signal_connect_data)(gpointer, const char*, void*, gpointer, void*, int);
	gpointer (*g_object_new)(GType, const char*, ...);
	void (*g_object_set)(gpointer, const char*, ...);
	void (*g_object_unref)(gpointer);
	guint (*g_idle_add)(gboolean (*)(gpointer), gpointer);
	void (*g_free)(gpointer);
	const char* (*g_type_name)(GType);

	gboolean (*gtk_init_check)(int*, char***);
	void (*gtk_main)(void);
	void (*gtk_main_quit)(void);
	gpointer (*gtk_window_new)(int);
	void (*gtk_window_set_title)(gpointer, const char*);
	void (*gtk_window_set_default_size)(gpointer, int, int);
	void (*gtk_window_move)(gpointer, int, int);
	void (*gtk_window_maximize)(gpointer);
	void (*gtk_window_get_position)(gpointer, int*, int*);
	void (*gtk_window_get_size)(gpointer, int*, int*);
	gboolean (*gtk_window_is_maximized)(gpointer);
	void (*gtk_window_present)(gpointer);
	void (*gtk_window_close)(gpointer);
	gboolean (*gtk_window_set_default_icon_from_file)(const char*, ShellError**);
	void (*gtk_window_set_default_icon_name)(const char*);
	void (*gtk_container_add)(gpointer, gpointer);
	void (*gtk_widget_show_all)(gpointer);
	void (*gtk_widget_destroy)(gpointer);
	void (*gtk_widget_set_size_request)(gpointer, int, int);
	gpointer (*gtk_message_dialog_new)(gpointer, int, int, int, const char*, ...);
	int (*gtk_dialog_run)(gpointer);
	gpointer (*gtk_file_chooser_native_new)(const char*, gpointer, int, const char*, const char*);
	int (*gtk_native_dialog_run)(gpointer);
	char* (*gtk_file_chooser_get_filename)(gpointer);
	gpointer (*gtk_settings_get_default)(void);
	gpointer (*gdk_display_get_default)(void);
	gpointer (*gdk_display_get_default_seat)(gpointer);
	gpointer (*gdk_seat_get_pointer)(gpointer);
	void (*gdk_device_get_position)(gpointer, gpointer*, int*, int*);

	GType (*webkit_web_view_get_type)(void);
	gpointer (*webkit_website_data_manager_new)(const char*, ...);
	gpointer (*webkit_website_data_manager_get_cookie_manager)(gpointer);
	gpointer (*webkit_web_context_new_with_website_data_manager)(gpointer);
	void (*webkit_cookie_manager_set_persistent_storage)(gpointer, const char*, int);
	gpointer (*webkit_user_content_manager_new)(void);
	gboolean (*webkit_user_content_manager_register_script_message_handler)(gpointer, const char*);
	gpointer (*webkit_user_script_new)(const char*, int, int, const char* const*, const char* const*);
	void (*webkit_user_content_manager_add_script)(gpointer, gpointer);
	void (*webkit_user_script_unref)(gpointer);
	void (*webkit_web_view_load_uri)(gpointer, const char*);
	void (*webkit_web_view_load_html)(gpointer, const char*, const char*);
	const char* (*webkit_web_view_get_uri)(gpointer);
	const char* (*webkit_web_view_get_title)(gpointer);
	gpointer (*webkit_web_view_get_settings)(gpointer);
	void (*webkit_settings_set_enable_developer_extras)(gpointer, gboolean);
	void (*webkit_settings_set_javascript_can_access_clipboard)(gpointer, gboolean);
	gpointer (*webkit_web_view_get_inspector)(gpointer);
	void (*webkit_web_inspector_show)(gpointer);
	void (*webkit_web_view_set_background_color)(gpointer, const ShellRgba*);
	void (*webkit_web_view_reload)(gpointer);
	gpointer (*webkit_javascript_result_get_js_value)(gpointer);
	char* (*jsc_value_to_string)(gpointer);
	gpointer (*webkit_navigation_policy_decision_get_navigation_action)(gpointer);
	gpointer (*webkit_navigation_action_get_request)(gpointer);
	gboolean (*webkit_navigation_action_is_user_gesture)(gpointer);
	const char* (*webkit_uri_request_get_uri)(gpointer);
	void (*webkit_policy_decision_ignore)(gpointer);
	void (*webkit_policy_decision_use)(gpointer);
	void (*webkit_permission_request_allow)(gpointer);
	GQuark (*webkit_network_error_quark)(void);
	GQuark (*webkit_policy_error_quark)(void);
} G;

#define NAMED(name) { #name, (void**)&G.name }
static const struct { const char* name; void** slot; } s_Functions[] = {
	NAMED(g_set_prgname), NAMED(g_signal_connect_data), NAMED(g_object_new), NAMED(g_object_set), NAMED(g_object_unref),
	NAMED(g_idle_add), NAMED(g_free), NAMED(g_type_name),
	NAMED(gtk_init_check), NAMED(gtk_main), NAMED(gtk_main_quit), NAMED(gtk_window_new), NAMED(gtk_window_set_title),
	NAMED(gtk_window_set_default_size), NAMED(gtk_window_move), NAMED(gtk_window_maximize), NAMED(gtk_window_get_position),
	NAMED(gtk_window_get_size), NAMED(gtk_window_is_maximized), NAMED(gtk_window_present), NAMED(gtk_window_close),
	NAMED(gtk_window_set_default_icon_from_file), NAMED(gtk_window_set_default_icon_name), NAMED(gtk_container_add),
	NAMED(gtk_widget_show_all), NAMED(gtk_widget_destroy), NAMED(gtk_widget_set_size_request), NAMED(gtk_message_dialog_new),
	NAMED(gtk_dialog_run), NAMED(gtk_file_chooser_native_new), NAMED(gtk_native_dialog_run), NAMED(gtk_file_chooser_get_filename),
	NAMED(gtk_settings_get_default), NAMED(gdk_display_get_default), NAMED(gdk_display_get_default_seat),
	NAMED(gdk_seat_get_pointer), NAMED(gdk_device_get_position),
	NAMED(webkit_web_view_get_type), NAMED(webkit_website_data_manager_new), NAMED(webkit_website_data_manager_get_cookie_manager),
	NAMED(webkit_web_context_new_with_website_data_manager), NAMED(webkit_cookie_manager_set_persistent_storage),
	NAMED(webkit_user_content_manager_new), NAMED(webkit_user_content_manager_register_script_message_handler),
	NAMED(webkit_user_script_new), NAMED(webkit_user_content_manager_add_script), NAMED(webkit_user_script_unref),
	NAMED(webkit_web_view_load_uri), NAMED(webkit_web_view_load_html), NAMED(webkit_web_view_get_uri),
	NAMED(webkit_web_view_get_title), NAMED(webkit_web_view_get_settings), NAMED(webkit_settings_set_enable_developer_extras),
	NAMED(webkit_settings_set_javascript_can_access_clipboard), NAMED(webkit_web_view_get_inspector),
	NAMED(webkit_web_inspector_show), NAMED(webkit_web_view_set_background_color), NAMED(webkit_web_view_reload),
	NAMED(webkit_javascript_result_get_js_value), NAMED(jsc_value_to_string),
	NAMED(webkit_navigation_policy_decision_get_navigation_action), NAMED(webkit_navigation_action_get_request),
	NAMED(webkit_navigation_action_is_user_gesture), NAMED(webkit_uri_request_get_uri), NAMED(webkit_policy_decision_ignore),
	NAMED(webkit_policy_decision_use), NAMED(webkit_permission_request_allow), NAMED(webkit_network_error_quark),
	NAMED(webkit_policy_error_quark),
};

/* WebKitGTK's libraries, newest API first; the GTK and GLib it loads come with it. */
static const char* const s_Libraries[] = { "libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37" };

static gpointer s_Context;   /* the WebKitWebContext every window shares: one signed-in profile */

typedef struct ShellWindow {
	ShellEvents events;
	gpointer window;          /* GtkWindow */
	gpointer view;            /* WebKitWebView */
	gpointer content;         /* WebKitUserContentManager */
	int alive;                /* 0 once destroyed: a late signal does nothing */
	ShellPlacement normal;    /* the last place and size while not maximized */
} ShellWindow;

static void Say(char* error, int errorSize, const char* text) {
	if (error && errorSize > 0) snprintf(error, (size_t)errorSize, "%s", text);
}

static void Copy(char* out, int outSize, const char* text) {
	if (out && outSize > 0) snprintf(out, (size_t)outSize, "%s", text ? text : "");
}

static void Connect(gpointer instance, const char* signal, void* handler, gpointer data) {
	G.g_signal_connect_data(instance, signal, handler, data, NULL, 0);
}

int shell_start(const char* dataFolder, const char* iconFile, char* error, int errorSize) {
	char message[512];
	if (!getenv("DISPLAY") && !getenv("WAYLAND_DISPLAY")) {
		Say(error, errorSize, "there is no desktop session to open a window in (neither DISPLAY nor WAYLAND_DISPLAY is set)");
		return 0;
	}
	void* library = NULL;
	for (size_t i = 0; i < sizeof s_Libraries / sizeof s_Libraries[0] && !library; ++i)
		library = dlopen(s_Libraries[i], RTLD_NOW | RTLD_GLOBAL);
	if (!library) {
		Say(error, errorSize, "IED's desktop window needs WebKitGTK, which is not installed: install libwebkit2gtk-4.1 "
			"(Debian and Ubuntu: sudo apt install libwebkit2gtk-4.1-0; Fedora: sudo dnf install webkit2gtk4.1)");
		return 0;
	}
	for (size_t i = 0; i < sizeof s_Functions / sizeof s_Functions[0]; ++i) {
		*s_Functions[i].slot = dlsym(library, s_Functions[i].name);
		if (!*s_Functions[i].slot) {
			snprintf(message, sizeof message, "the WebKitGTK installed is too old: it has no %s", s_Functions[i].name);
			Say(error, errorSize, message);
			return 0;
		}
	}
	/* NVIDIA's driver shows WebKitGTK's DMA-BUF renderer as a blank window; the shared-memory one works everywhere. */
	if (access("/proc/driver/nvidia/version", F_OK) == 0) setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 0);
	/* The windows' class (X11) and app id (Wayland): the desktop matches them to ied.desktop, its icon and name. */
	G.g_set_prgname("ied");
	if (!G.gtk_init_check(NULL, NULL)) {
		Say(error, errorSize, "GTK could not open the display");
		return 0;
	}
	G.g_object_set(G.gtk_settings_get_default(), "gtk-application-prefer-dark-theme", 1, NULL);
	G.gtk_window_set_default_icon_name("ied");
	if (iconFile && iconFile[0] && access(iconFile, R_OK) == 0) G.gtk_window_set_default_icon_from_file(iconFile, NULL);

	char data[4096];
	char cache[4096];
	char cookies[4096];
	snprintf(data, sizeof data, "%s/webkit", dataFolder);
	snprintf(cache, sizeof cache, "%s/webkit-cache", dataFolder);
	snprintf(cookies, sizeof cookies, "%s/webkit/cookies.sqlite", dataFolder);
	gpointer manager = G.webkit_website_data_manager_new("base-data-directory", data, "base-cache-directory", cache, NULL);
	G.webkit_cookie_manager_set_persistent_storage(G.webkit_website_data_manager_get_cookie_manager(manager), cookies, kCookiesSqlite);
	s_Context = G.webkit_web_context_new_with_website_data_manager(manager);
	return 1;
}

static void OnMessage(gpointer manager, gpointer result, gpointer data) {
	ShellWindow* window = data;
	if (!window->alive) return;
	char* text = G.jsc_value_to_string(G.webkit_javascript_result_get_js_value(result));
	const char* source = G.webkit_web_view_get_uri(window->view);
	window->events.message(window->events.context, source ? source : "", text ? text : "");
	if (text) G.g_free(text);
}

static gboolean OnDecide(gpointer view, gpointer decision, int type, gpointer data) {
	ShellWindow* window = data;
	if (type != kDecideNavigation && type != kDecideNewWindow) return 0;   /* a response: WebKit's own choice */
	gpointer action = G.webkit_navigation_policy_decision_get_navigation_action(decision);
	const char* url = G.webkit_uri_request_get_uri(G.webkit_navigation_action_get_request(action));
	const int gesture = G.webkit_navigation_action_is_user_gesture(action) ? 1 : 0;
	if (window->alive && window->events.navigation(window->events.context, url ? url : "", type == kDecideNewWindow, gesture))
		G.webkit_policy_decision_use(decision);
	else
		G.webkit_policy_decision_ignore(decision);
	return 1;
}

static void OnTitle(gpointer view, gpointer property, gpointer data) {
	ShellWindow* window = data;
	if (!window->alive) return;
	const char* title = G.webkit_web_view_get_title(window->view);
	window->events.title(window->events.context, title ? title : "");
}

static gboolean OnFailed(gpointer view, int event, const char* url, ShellError* error, gpointer data) {
	ShellWindow* window = data;
	if (!window->alive || !error) return 0;
	/* Stopped by the app (a navigation it sent elsewhere) or by a newer load: no failure. */
	if (error->domain == G.webkit_policy_error_quark()) return 0;
	if (error->domain == G.webkit_network_error_quark() && error->code == kNetworkCancelled) return 0;
	return window->events.failed(window->events.context, url ? url : "", error->message ? error->message : "") ? 1 : 0;
}

static void OnCrashed(gpointer view, int reason, gpointer data) {
	ShellWindow* window = data;
	if (window->alive) window->events.crashed(window->events.context);
}

/* The server's pages may show notifications and use the clipboard (paste in the terminal); anything else WebKit's
   default (refused). */
static gboolean OnPermission(gpointer view, gpointer request, gpointer data) {
	const char* type = G.g_type_name(((ShellTypeInstance*)request)->klass->type);
	if (type && (!strcmp(type, "WebKitNotificationPermissionRequest") || !strcmp(type, "WebKitClipboardPermissionRequest"))) {
		G.webkit_permission_request_allow(request);
		return 1;
	}
	return 0;
}

static gboolean OnConfigure(gpointer widget, gpointer event, gpointer data) {
	ShellWindow* window = data;
	if (window->alive && !G.gtk_window_is_maximized(window->window)) {
		G.gtk_window_get_position(window->window, &window->normal.x, &window->normal.y);
		G.gtk_window_get_size(window->window, &window->normal.width, &window->normal.height);
		window->normal.known = 1;
	}
	return 0;
}

static gboolean OnDelete(gpointer widget, gpointer event, gpointer data) {
	ShellWindow* window = data;
	if (window->alive) window->events.closing(window->events.context);
	return 0;
}

static void OnDestroy(gpointer widget, gpointer data) {
	ShellWindow* window = data;
	if (!window->alive) return;
	window->alive = 0;
	window->events.closed(window->events.context);
	/* The structure stays: a signal already queued for the view may still name it. */
}

void* shell_window_new(const ShellEvents* events, const char* script, const ShellPlacement* placement, int minWidth, int minHeight) {
	ShellWindow* window = calloc(1, sizeof *window);
	if (!window) return NULL;
	window->events = *events;
	window->alive = 1;

	window->content = G.webkit_user_content_manager_new();
	Connect(window->content, "script-message-received::ied", (void*)OnMessage, window);
	G.webkit_user_content_manager_register_script_message_handler(window->content, "ied");
	gpointer bridge = G.webkit_user_script_new(script, kInjectTopFrame, kInjectAtStart, NULL, NULL);
	G.webkit_user_content_manager_add_script(window->content, bridge);
	G.webkit_user_script_unref(bridge);

	window->view = G.g_object_new(G.webkit_web_view_get_type(), "web-context", s_Context, "user-content-manager", window->content, NULL);
	gpointer settings = G.webkit_web_view_get_settings(window->view);
	G.webkit_settings_set_enable_developer_extras(settings, 1);
	G.webkit_settings_set_javascript_can_access_clipboard(settings, 1);
	/* --bg-0 (theme.css): nothing flashes white while a page loads. */
	ShellRgba background = { 14.0 / 255.0, 14.0 / 255.0, 14.0 / 255.0, 1.0 };
	G.webkit_web_view_set_background_color(window->view, &background);
	Connect(window->view, "decide-policy", (void*)OnDecide, window);
	Connect(window->view, "notify::title", (void*)OnTitle, window);
	Connect(window->view, "load-failed", (void*)OnFailed, window);
	Connect(window->view, "web-process-terminated", (void*)OnCrashed, window);
	Connect(window->view, "permission-request", (void*)OnPermission, window);
	G.gtk_widget_set_size_request(window->view, minWidth, minHeight);

	window->window = G.gtk_window_new(kWindowToplevel);
	G.gtk_window_set_title(window->window, "IED");
	G.gtk_window_set_default_size(window->window, placement->width, placement->height);
	/* A place of (0, 0) is one never learnt (Wayland tells none): the window manager places the window. */
	if (placement->known && (placement->x != 0 || placement->y != 0)) G.gtk_window_move(window->window, placement->x, placement->y);
	window->normal = *placement;
	G.gtk_container_add(window->window, window->view);
	Connect(window->window, "configure-event", (void*)OnConfigure, window);
	Connect(window->window, "delete-event", (void*)OnDelete, window);
	Connect(window->window, "destroy", (void*)OnDestroy, window);
	if (placement->maximized) G.gtk_window_maximize(window->window);
	G.gtk_widget_show_all(window->window);
	return window;
}

void shell_window_load(void* handle, const char* url) {
	ShellWindow* window = handle;
	if (window && window->alive) G.webkit_web_view_load_uri(window->view, url);
}

void shell_window_load_html(void* handle, const char* html) {
	ShellWindow* window = handle;
	if (window && window->alive) G.webkit_web_view_load_html(window->view, html, "about:blank");
}

void shell_window_uri(void* handle, char* out, int outSize) {
	ShellWindow* window = handle;
	Copy(out, outSize, window && window->alive ? G.webkit_web_view_get_uri(window->view) : "");
}

void shell_window_placement(void* handle, ShellPlacement* placement) {
	ShellWindow* window = handle;
	memset(placement, 0, sizeof *placement);
	if (!window || !window->alive) return;
	/* Maximized: the size it had (or was to have) before, so that it comes back maximized with a size to return to. */
	*placement = window->normal;
	placement->known = 1;
	placement->maximized = G.gtk_window_is_maximized(window->window) ? 1 : 0;
	if (!placement->maximized) {
		G.gtk_window_get_position(window->window, &placement->x, &placement->y);
		G.gtk_window_get_size(window->window, &placement->width, &placement->height);
		placement->known = 1;
	}
}

void shell_window_set_title(void* handle, const char* title) {
	ShellWindow* window = handle;
	if (window && window->alive) G.gtk_window_set_title(window->window, title);
}

void shell_window_close(void* handle) {
	ShellWindow* window = handle;
	if (window && window->alive) G.gtk_window_close(window->window);
}

void shell_window_present(void* handle) {
	ShellWindow* window = handle;
	if (window && window->alive) G.gtk_window_present(window->window);
}

void shell_window_inspect(void* handle) {
	ShellWindow* window = handle;
	if (window && window->alive) G.webkit_web_inspector_show(G.webkit_web_view_get_inspector(window->view));
}

void shell_window_reload(void* handle) {
	ShellWindow* window = handle;
	if (window && window->alive) G.webkit_web_view_reload(window->view);
}

void shell_window_alert(void* handle, const char* text) {
	ShellWindow* window = handle;
	gpointer parent = window && window->alive ? window->window : NULL;
	gpointer dialog = G.gtk_message_dialog_new(parent, kDialogModal | kDialogDestroyWithParent, kMessageError, kButtonsOk, "%s", text);
	G.gtk_window_set_title(dialog, "IED");
	G.gtk_dialog_run(dialog);
	G.gtk_widget_destroy(dialog);
}

int shell_pick_folder(void* handle, char* out, int outSize) {
	ShellWindow* window = handle;
	Copy(out, outSize, "");
	gpointer parent = window && window->alive ? window->window : NULL;
	gpointer dialog = G.gtk_file_chooser_native_new("Open Folder", parent, kChooseFolder, "_Open", "_Cancel");
	int picked = 0;
	if (G.gtk_native_dialog_run(dialog) == kResponseAccept) {
		char* folder = G.gtk_file_chooser_get_filename(dialog);
		if (folder) {
			Copy(out, outSize, folder);
			G.g_free(folder);
			picked = 1;
		}
	}
	G.g_object_unref(dialog);
	return picked;
}

void shell_pointer(int* x, int* y) {
	*x = 0;
	*y = 0;
	gpointer display = G.gdk_display_get_default();
	gpointer seat = display ? G.gdk_display_get_default_seat(display) : NULL;
	gpointer pointer = seat ? G.gdk_seat_get_pointer(seat) : NULL;
	if (pointer) G.gdk_device_get_position(pointer, NULL, x, y);
}

void shell_run(void) { G.gtk_main(); }

void shell_quit(void) { G.gtk_main_quit(); }

typedef struct Posted { void (*work)(void*); void* data; } Posted;

static gboolean RunPosted(gpointer data) {
	Posted* posted = data;
	posted->work(posted->data);
	free(posted);
	return 0;   /* G_SOURCE_REMOVE: once */
}

void shell_post(void (*work)(void*), void* data) {
	Posted* posted = malloc(sizeof *posted);
	if (!posted) return;
	posted->work = work;
	posted->data = data;
	G.g_idle_add(RunPosted, posted);
}

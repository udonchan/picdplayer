#include <signal.h>
#include <string.h>
#include <glib-unix.h>
#include <wpe/drm/wpe-drm.h>
#include <wpe/wpe-platform.h>
#include <wpe/webkit.h>

typedef struct {
    GMainLoop* loop;
    gboolean failed;
} Kiosk;

static gboolean stop_loop(gpointer data)
{
    g_main_loop_quit(((Kiosk*)data)->loop);
    return G_SOURCE_REMOVE;
}

static gboolean load_failed(WebKitWebView* view, WebKitLoadEvent event,
    const gchar* uri, GError* error, gpointer data)
{
    (void)view;
    (void)event;
    (void)uri;
    Kiosk* kiosk = data;
    kiosk->failed = TRUE;
    g_warning("WPE kiosk navigation failed: %s", error->message);
    g_main_loop_quit(kiosk->loop);
    return FALSE;
}

static void process_terminated(WebKitWebView* view,
    WebKitWebProcessTerminationReason reason, gpointer data)
{
    (void)view;
    Kiosk* kiosk = data;
    kiosk->failed = TRUE;
    g_warning("WPE kiosk web process terminated: %d", reason);
    g_main_loop_quit(kiosk->loop);
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        g_print("%u.%u.%u\n", webkit_get_major_version(), webkit_get_minor_version(), webkit_get_micro_version());
        return 0;
    }
    if (argc != 2) {
        g_printerr("usage: picdplayer-wpe URL | --version\n");
        return 2;
    }
    g_autoptr(WPEDisplay) display = WPE_DISPLAY(wpe_display_drm_new());
    g_autoptr(GError) error = NULL;
    if (!wpe_display_connect(display, &error)) {
        g_printerr("WPE kiosk DRM connection failed: %s\n", error->message);
        return 1;
    }
    g_autoptr(WebKitWebView) view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "display", display, NULL));
    WPEView* platform_view = webkit_web_view_get_wpe_view(view);
    WPEToplevel* toplevel = platform_view ? wpe_view_get_toplevel(platform_view) : NULL;
    if (!toplevel) {
        g_printerr("WPE kiosk has no toplevel\n");
        return 1;
    }
    wpe_toplevel_fullscreen(toplevel);
    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    Kiosk kiosk = {loop, FALSE};
    g_unix_signal_add(SIGINT, stop_loop, &kiosk);
    g_unix_signal_add(SIGTERM, stop_loop, &kiosk);
    g_signal_connect(view, "load-failed", G_CALLBACK(load_failed), &kiosk);
    g_signal_connect(view, "web-process-terminated", G_CALLBACK(process_terminated), &kiosk);
    webkit_web_view_load_uri(view, argv[1]);
    g_main_loop_run(loop);
    return kiosk.failed ? 1 : 0;
}

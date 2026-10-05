#include <signal.h>
#include <glib-unix.h>
#include <wpe/drm/wpe-drm.h>
#include <wpe/wpe-platform.h>
#include <wpe/webkit.h>

static gboolean stop_main_loop(gpointer data)
{
    g_main_loop_quit(data);
    return G_SOURCE_REMOVE;
}

static void on_load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer data)
{
    gboolean* failed = data;
    if (event == WEBKIT_LOAD_FINISHED)
        g_message("WPE PoC: navigation finished (%s): %s",
            *failed ? "failed" : "success", webkit_web_view_get_uri(view));
}

static gboolean on_load_failed(WebKitWebView* view, WebKitLoadEvent event,
    const gchar* uri, GError* error, gpointer data)
{
    (void)view;
    (void)event;
    gboolean* failed = data;
    *failed = TRUE;
    g_warning("WPE PoC: failed to load %s: %s", uri, error->message);
    return FALSE;
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        g_printerr("usage: %s URL\n", argv[0]);
        return 2;
    }

    g_message("WPE PoC: creating DRM display");
    g_autoptr(WPEDisplay) display = WPE_DISPLAY(wpe_display_drm_new());
    g_autoptr(GError) error = NULL;
    g_message("WPE PoC: connecting DRM display");
    if (!wpe_display_connect(display, &error)) {
        g_printerr("WPE PoC: DRM display connection failed: %s\n", error->message);
        return 1;
    }

    g_message("WPE PoC: creating web view");
    g_autoptr(WebKitWebView) view = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW, "display", display, NULL));
    if (!view)
        return 1;

    g_message("WPE PoC: configuring fullscreen view");
    WPEView* wpe_view = webkit_web_view_get_wpe_view(view);
    if (wpe_view) {
        WPEToplevel* toplevel = wpe_view_get_toplevel(wpe_view);
        if (toplevel)
            wpe_toplevel_fullscreen(toplevel);
    }

    g_autoptr(GMainLoop) loop = g_main_loop_new(NULL, FALSE);
    gboolean load_failed = FALSE;
    g_unix_signal_add(SIGINT, stop_main_loop, loop);
    g_unix_signal_add(SIGTERM, stop_main_loop, loop);
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), &load_failed);
    g_signal_connect(view, "load-failed", G_CALLBACK(on_load_failed), &load_failed);
    g_message("WPE PoC: loading %s", argv[1]);
    webkit_web_view_load_uri(view, argv[1]);
    g_main_loop_run(loop);
    return 0;
}

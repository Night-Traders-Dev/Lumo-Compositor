/* Lumo Browser — lightweight WebKitGTK browser for Lumo Compositor.
 * Runs as a standalone Wayland client using GTK4 + WebKitGTK 6.0.
 * Launched by the compositor when the Browser tile is tapped. */

#include <gtk/gtk.h>
#include <webkit/webkit.h>

#define LUMO_BROWSER_HOME "https://lite.duckduckgo.com/"
#define LUMO_BROWSER_TITLE "Lumo Browser"

typedef struct {
    GtkWindow *window;
    WebKitWebView *web_view;
    GtkEntry *url_bar;
    GtkButton *back_btn;
    GtkButton *forward_btn;
    GtkButton *reload_btn;
} LumoBrowser;

/* percent-encode a string for safe URL query use */
static void url_encode(const char *src, char *dst, size_t dst_size) {
    static const char *safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 3 < dst_size; i++) {
        if (strchr(safe, src[i])) {
            dst[j++] = src[i];
        } else if (src[i] == ' ') {
            dst[j++] = '+';
        } else {
            snprintf(dst + j, dst_size - j, "%%%02X",
                (unsigned char)src[i]);
            j += 3;
        }
    }
    dst[j] = '\0';
}

static void on_url_activate(GtkEntry *entry, gpointer data) {
    LumoBrowser *browser = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (text == NULL || text[0] == '\0') return;

    char url[4096];
    if (strstr(text, "://") != NULL) {
        /* already has scheme */
        snprintf(url, sizeof(url), "%s", text);
    } else if (strstr(text, "localhost") != NULL) {
        /* local dev server — add http:// */
        snprintf(url, sizeof(url), "http://%s", text);
    } else if (strchr(text, '.') != NULL && strchr(text, ' ') == NULL) {
        /* looks like a domain name */
        snprintf(url, sizeof(url), "https://%s", text);
    } else {
        /* search query — percent-encode for safety */
        char encoded[2048];
        url_encode(text, encoded, sizeof(encoded));
        snprintf(url, sizeof(url),
            "https://lite.duckduckgo.com/lite/?q=%s", encoded);
    }
    webkit_web_view_load_uri(browser->web_view, url);
}

static void on_load_changed(WebKitWebView *web_view,
    WebKitLoadEvent event, gpointer data)
{
    LumoBrowser *browser = data;
    if (event == WEBKIT_LOAD_COMMITTED || event == WEBKIT_LOAD_FINISHED) {
        const char *uri = webkit_web_view_get_uri(web_view);
        if (uri != NULL) {
            gtk_editable_set_text(GTK_EDITABLE(browser->url_bar), uri);
        }
    }
    if (event == WEBKIT_LOAD_FINISHED) {
        const char *title = webkit_web_view_get_title(web_view);
        if (title != NULL) {
            char win_title[256];
            snprintf(win_title, sizeof(win_title), "%s - %s",
                title, LUMO_BROWSER_TITLE);
            gtk_window_set_title(browser->window, win_title);
        }
    }
}

static void on_back(GtkButton *btn, gpointer data) {
    LumoBrowser *browser = data;
    (void)btn;
    if (webkit_web_view_can_go_back(browser->web_view))
        webkit_web_view_go_back(browser->web_view);
}

static void on_forward(GtkButton *btn, gpointer data) {
    LumoBrowser *browser = data;
    (void)btn;
    if (webkit_web_view_can_go_forward(browser->web_view))
        webkit_web_view_go_forward(browser->web_view);
}

static void on_reload(GtkButton *btn, gpointer data) {
    LumoBrowser *browser = data;
    (void)btn;
    webkit_web_view_reload(browser->web_view);
}

static void activate(GtkApplication *app, gpointer user_data) {
    LumoBrowser *browser = user_data;
    GtkWidget *box, *toolbar, *scroll;
    WebKitSettings *settings;

    browser->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(browser->window, LUMO_BROWSER_TITLE);
    gtk_window_set_default_size(browser->window, 800, 600);
    gtk_window_fullscreen(browser->window);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(browser->window, box);

    /* toolbar */
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(toolbar, 4);
    gtk_widget_set_margin_end(toolbar, 4);
    gtk_widget_set_margin_top(toolbar, 4);
    gtk_widget_set_margin_bottom(toolbar, 4);

    browser->back_btn = GTK_BUTTON(gtk_button_new_with_label("<"));
    browser->forward_btn = GTK_BUTTON(gtk_button_new_with_label(">"));
    browser->reload_btn = GTK_BUTTON(gtk_button_new_with_label("R"));
    browser->url_bar = GTK_ENTRY(gtk_entry_new());

    gtk_editable_set_text(GTK_EDITABLE(browser->url_bar), LUMO_BROWSER_HOME);
    gtk_widget_set_hexpand(GTK_WIDGET(browser->url_bar), TRUE);

    g_signal_connect(browser->back_btn, "clicked", G_CALLBACK(on_back), browser);
    g_signal_connect(browser->forward_btn, "clicked", G_CALLBACK(on_forward), browser);
    g_signal_connect(browser->reload_btn, "clicked", G_CALLBACK(on_reload), browser);
    g_signal_connect(browser->url_bar, "activate", G_CALLBACK(on_url_activate), browser);

    gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(browser->back_btn));
    gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(browser->forward_btn));
    gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(browser->reload_btn));
    gtk_box_append(GTK_BOX(toolbar), GTK_WIDGET(browser->url_bar));

    gtk_box_append(GTK_BOX(box), toolbar);

    /* web view */
    browser->web_view = WEBKIT_WEB_VIEW(webkit_web_view_new());
    settings = webkit_web_view_get_settings(browser->web_view);

    /* performance: disable heavy features for riscv64 */
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, FALSE);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    webkit_settings_set_user_agent_with_application_details(settings,
        "LumoBrowser", "1.0");

    g_signal_connect(browser->web_view, "load-changed",
        G_CALLBACK(on_load_changed), browser);

    gtk_widget_set_vexpand(GTK_WIDGET(browser->web_view), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(browser->web_view), TRUE);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(browser->web_view));

    webkit_web_view_load_uri(browser->web_view, LUMO_BROWSER_HOME);

    gtk_window_present(browser->window);
}

int main(int argc, char **argv) {
    GtkApplication *app;
    LumoBrowser browser = {0};
    int status;

    app = gtk_application_new("com.lumo.browser", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &browser);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

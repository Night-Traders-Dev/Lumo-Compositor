/* Lumo Browser — touch-first WebKitGTK browser for Lumo Compositor.
 * Full custom UI: tabbed browsing, address bar, navigation, bookmarks.
 * Runs as a standalone Wayland client using GTK4 + WebKitGTK 6.0. */

#include <gtk/gtk.h>
#include <webkit/webkit.h>
#include <string.h>
#include <stdio.h>

#define LUMO_BROWSER_TITLE "Lumo Browser"
#define LUMO_MAX_TABS 8
#define LUMO_MAX_BOOKMARKS 16

/* ── Lumo theme colors ─────────────────────────────────────────────── */
#define LUMO_BG       "#2C001E"
#define LUMO_SURFACE  "#3D0028"
#define LUMO_ACCENT   "#E95420"
#define LUMO_TEXT     "#FFFFFF"
#define LUMO_DIM      "#AEA79F"
#define LUMO_BORDER   "#77216F"

/* ── Data structures ───────────────────────────────────────────────── */

typedef struct {
    WebKitWebView *web_view;
    GtkWidget *tab_button;
    char title[128];
    char uri[2048];
} LumoTab;

typedef struct {
    char title[64];
    char uri[2048];
} LumoBookmark;

typedef struct {
    GtkWindow *window;
    GtkBox *main_box;
    GtkBox *toolbar;
    GtkBox *tab_bar;
    GtkStack *view_stack;
    GtkEntry *url_bar;
    GtkButton *back_btn;
    GtkButton *forward_btn;
    GtkButton *reload_btn;
    GtkButton *new_tab_btn;
    GtkButton *bookmark_btn;
    GtkButton *menu_btn;

    LumoTab tabs[LUMO_MAX_TABS];
    int tab_count;
    int active_tab;

    LumoBookmark bookmarks[LUMO_MAX_BOOKMARKS];
    int bookmark_count;

    GtkWidget *bookmark_popover;
    GtkWidget *menu_popover;
} LumoBrowser;

/* ── URL handling ──────────────────────────────────────────────────── */

static void url_encode(const char *src, char *dst, size_t dst_size) {
    static const char *safe =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";
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

static void resolve_url(const char *text, char *url, size_t url_size) {
    if (strstr(text, "://") != NULL) {
        snprintf(url, url_size, "%s", text);
    } else if (strstr(text, "localhost") != NULL) {
        snprintf(url, url_size, "http://%s", text);
    } else if (strchr(text, '.') != NULL && strchr(text, ' ') == NULL) {
        snprintf(url, url_size, "https://%s", text);
    } else {
        char encoded[2048];
        url_encode(text, encoded, sizeof(encoded));
        snprintf(url, url_size,
            "https://duckduckgo.com/?q=%s", encoded);
    }
}

/* ── Bookmark persistence ──────────────────────────────────────────── */

static void bookmarks_load(LumoBrowser *b) {
    char path[512];
    const char *home = g_get_home_dir();
    FILE *fp;
    snprintf(path, sizeof(path), "%s/.lumo-bookmarks", home);
    fp = fopen(path, "r");
    if (!fp) {
        /* default bookmarks */
        snprintf(b->bookmarks[0].title, 64, "DuckDuckGo");
        snprintf(b->bookmarks[0].uri, 2048, "https://duckduckgo.com/");
        snprintf(b->bookmarks[1].title, 64, "Wikipedia");
        snprintf(b->bookmarks[1].uri, 2048, "https://en.m.wikipedia.org/");
        snprintf(b->bookmarks[2].title, 64, "GitHub");
        snprintf(b->bookmarks[2].uri, 2048, "https://github.com/");
        b->bookmark_count = 3;
        return;
    }
    b->bookmark_count = 0;
    char line[4096];
    while (b->bookmark_count < LUMO_MAX_BOOKMARKS &&
            fgets(line, sizeof(line), fp)) {
        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        char *nl = strchr(sep + 1, '\n');
        if (nl) *nl = '\0';
        snprintf(b->bookmarks[b->bookmark_count].title, 64, "%s", line);
        snprintf(b->bookmarks[b->bookmark_count].uri, 2048, "%s", sep + 1);
        b->bookmark_count++;
    }
    fclose(fp);
}

static void bookmarks_save(LumoBrowser *b) {
    char path[512];
    const char *home = g_get_home_dir();
    FILE *fp;
    snprintf(path, sizeof(path), "%s/.lumo-bookmarks", home);
    fp = fopen(path, "w");
    if (!fp) return;
    for (int i = 0; i < b->bookmark_count; i++) {
        fprintf(fp, "%s|%s\n", b->bookmarks[i].title, b->bookmarks[i].uri);
    }
    fclose(fp);
}

/* ── Tab management ────────────────────────────────────────────────── */

static void switch_to_tab(LumoBrowser *b, int idx);

static void on_tab_clicked(GtkButton *btn, gpointer data) {
    LumoBrowser *b = data;
    int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "tab-index"));
    switch_to_tab(b, idx);
}

static void update_tab_bar(LumoBrowser *b) {
    /* remove all children except the + and X buttons at the end */
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(b->tab_bar));
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        const char *name = gtk_widget_get_name(child);
        if (name == NULL || strncmp(name, "tab-btn-", 8) == 0)
            gtk_box_remove(b->tab_bar, child);
        child = next;
    }

    /* insert tab buttons at the beginning (before + and X) */
    GtkWidget *first = gtk_widget_get_first_child(GTK_WIDGET(b->tab_bar));
    for (int i = b->tab_count - 1; i >= 0; i--) {
        char label[48];
        char wname[24];
        const char *title = b->tabs[i].title[0] ? b->tabs[i].title : "New Tab";
        snprintf(label, sizeof(label), "%.20s", title);
        snprintf(wname, sizeof(wname), "tab-btn-%d", i);

        GtkWidget *btn = gtk_button_new_with_label(label);
        gtk_widget_set_name(btn, wname);
        gtk_widget_set_hexpand(btn, TRUE);

        if (i == b->active_tab) {
            gtk_widget_add_css_class(btn, "tab-active");
        } else {
            gtk_widget_add_css_class(btn, "tab-inactive");
        }

        g_object_set_data(G_OBJECT(btn), "tab-index",
            GINT_TO_POINTER(i));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_tab_clicked), b);

        b->tabs[i].tab_button = btn;
        if (first)
            gtk_box_insert_child_after(b->tab_bar, btn, NULL);
        else
            gtk_box_prepend(b->tab_bar, btn);
    }
}

static WebKitWebView *create_web_view(LumoBrowser *b) {
    WebKitWebView *wv = WEBKIT_WEB_VIEW(webkit_web_view_new());
    WebKitSettings *settings = webkit_web_view_get_settings(wv);

    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, FALSE);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    webkit_settings_set_enable_media(settings, FALSE);
    webkit_settings_set_enable_webaudio(settings, FALSE);
    webkit_settings_set_enable_media_stream(settings, FALSE);
    webkit_settings_set_enable_webgl(settings, FALSE);

    webkit_settings_set_user_agent(settings,
        "Mozilla/5.0 (Linux; Android 14) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36");

    gtk_widget_set_vexpand(GTK_WIDGET(wv), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(wv), TRUE);

    return wv;
}

static void on_tab_load_changed(WebKitWebView *wv,
    WebKitLoadEvent event, gpointer data)
{
    LumoBrowser *b = data;
    int idx = -1;
    for (int i = 0; i < b->tab_count; i++) {
        if (b->tabs[i].web_view == wv) { idx = i; break; }
    }
    if (idx < 0) return;

    if (event == WEBKIT_LOAD_COMMITTED || event == WEBKIT_LOAD_FINISHED) {
        const char *uri = webkit_web_view_get_uri(wv);
        if (uri) snprintf(b->tabs[idx].uri, sizeof(b->tabs[idx].uri),
            "%s", uri);
        if (idx == b->active_tab && uri)
            gtk_editable_set_text(GTK_EDITABLE(b->url_bar), uri);
    }
    if (event == WEBKIT_LOAD_FINISHED) {
        const char *title = webkit_web_view_get_title(wv);
        if (title) {
            snprintf(b->tabs[idx].title, sizeof(b->tabs[idx].title),
                "%s", title);
            update_tab_bar(b);
        }
    }
}

static int add_tab(LumoBrowser *b, const char *uri) {
    if (b->tab_count >= LUMO_MAX_TABS) return -1;
    int idx = b->tab_count++;
    LumoTab *tab = &b->tabs[idx];

    tab->web_view = create_web_view(b);
    tab->title[0] = '\0';
    snprintf(tab->uri, sizeof(tab->uri), "%s",
        uri ? uri : "about:blank");

    g_signal_connect(tab->web_view, "load-changed",
        G_CALLBACK(on_tab_load_changed), b);

    char stack_name[16];
    snprintf(stack_name, sizeof(stack_name), "tab%d", idx);
    gtk_stack_add_named(b->view_stack, GTK_WIDGET(tab->web_view),
        stack_name);

    if (uri && uri[0])
        webkit_web_view_load_uri(tab->web_view, uri);

    return idx;
}

static void switch_to_tab(LumoBrowser *b, int idx) {
    /* when called from signal, extract index from button data */
    if (idx == 0 || (idx > 0 && idx < b->tab_count)) {
        /* direct call with valid index */
    } else {
        /* called from button signal — 'b' is actually the browser,
         * we need to find the button that was clicked */
        GtkWidget *btn = gtk_event_controller_get_widget(
            GTK_EVENT_CONTROLLER(g_object_get_data(G_OBJECT(b), "last-controller")));
        if (btn) {
            idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "tab-index"));
        } else {
            idx = 0;
        }
    }

    if (idx < 0 || idx >= b->tab_count) return;
    b->active_tab = idx;

    char stack_name[16];
    snprintf(stack_name, sizeof(stack_name), "tab%d", idx);
    gtk_stack_set_visible_child_name(b->view_stack, stack_name);

    const char *uri = webkit_web_view_get_uri(b->tabs[idx].web_view);
    if (uri)
        gtk_editable_set_text(GTK_EDITABLE(b->url_bar), uri);

    update_tab_bar(b);
}

/* ── UI callbacks ──────────────────────────────────────────────────── */

static void on_url_activate(GtkEntry *entry, gpointer data) {
    LumoBrowser *b = data;
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!text || !text[0]) return;

    char url[4096];
    resolve_url(text, url, sizeof(url));

    if (b->active_tab >= 0 && b->active_tab < b->tab_count)
        webkit_web_view_load_uri(b->tabs[b->active_tab].web_view, url);
}

static void on_back(GtkButton *btn, gpointer data) {
    LumoBrowser *b = data; (void)btn;
    if (b->active_tab >= 0 && b->active_tab < b->tab_count)
        webkit_web_view_go_back(b->tabs[b->active_tab].web_view);
}

static void on_forward(GtkButton *btn, gpointer data) {
    LumoBrowser *b = data; (void)btn;
    if (b->active_tab >= 0 && b->active_tab < b->tab_count)
        webkit_web_view_go_forward(b->tabs[b->active_tab].web_view);
}

static void on_reload(GtkButton *btn, gpointer data) {
    LumoBrowser *b = data; (void)btn;
    if (b->active_tab >= 0 && b->active_tab < b->tab_count)
        webkit_web_view_reload(b->tabs[b->active_tab].web_view);
}

static void on_new_tab(GtkButton *btn, gpointer data) {
    LumoBrowser *b = data; (void)btn;
    int idx = add_tab(b, NULL);
    if (idx >= 0) {
        switch_to_tab(b, idx);
        /* load start page */
        webkit_web_view_load_html(b->tabs[idx].web_view,
            "<!DOCTYPE html><html><head>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<style>"
            "body{background:" LUMO_BG ";color:" LUMO_TEXT ";font-family:sans-serif;"
            "display:flex;flex-direction:column;align-items:center;"
            "justify-content:center;height:90vh;margin:0}"
            "h1{color:" LUMO_ACCENT ";font-size:2em;margin-bottom:0.2em}"
            "form{width:80%;max-width:400px}"
            "input{width:100%;padding:12px;font-size:1.1em;border:2px solid " LUMO_BORDER ";"
            "border-radius:24px;background:#1d0014;color:" LUMO_TEXT ";text-align:center}"
            "input:focus{border-color:" LUMO_ACCENT ";outline:none}"
            ".bookmarks{display:flex;flex-wrap:wrap;gap:12px;margin-top:24px;"
            "justify-content:center}"
            ".bm{background:" LUMO_SURFACE ";color:" LUMO_DIM ";padding:10px 18px;"
            "border-radius:16px;text-decoration:none;font-size:0.9em;"
            "border:1px solid " LUMO_BORDER "}"
            ".bm:hover{border-color:" LUMO_ACCENT ";color:" LUMO_TEXT "}"
            "p{color:" LUMO_DIM ";font-size:0.8em;margin-top:2em}"
            "</style></head><body>"
            "<h1>Lumo Browser</h1>"
            "<form action='https://duckduckgo.com/' method='get'>"
            "<input name='q' placeholder='Search the web...' autofocus>"
            "</form>"
            "<div class='bookmarks'>"
            "<a class='bm' href='https://duckduckgo.com/'>DuckDuckGo</a>"
            "<a class='bm' href='https://en.m.wikipedia.org/'>Wikipedia</a>"
            "<a class='bm' href='https://github.com/'>GitHub</a>"
            "</div>"
            "<p>Lumo Browser v0.0.59 | WebKit</p>"
            "</body></html>",
            "about:lumo");
        update_tab_bar(b);
    }
}

static void on_close_tab(GtkButton *btn, gpointer data) {
    LumoBrowser *b = data; (void)btn;
    if (b->tab_count <= 1) {
        gtk_window_close(b->window);
        return;
    }
    int idx = b->active_tab;
    char stack_name[16];
    snprintf(stack_name, sizeof(stack_name), "tab%d", idx);
    GtkWidget *child = gtk_stack_get_child_by_name(b->view_stack, stack_name);
    if (child) gtk_stack_remove(b->view_stack, child);

    for (int i = idx; i < b->tab_count - 1; i++)
        b->tabs[i] = b->tabs[i + 1];
    b->tab_count--;

    if (b->active_tab >= b->tab_count)
        b->active_tab = b->tab_count - 1;
    if (b->active_tab < 0) b->active_tab = 0;

    /* re-add remaining tabs to stack with correct names */
    /* (simplified: just switch to the active one) */
    switch_to_tab(b, b->active_tab);
    update_tab_bar(b);
}

static void on_add_bookmark(GtkButton *btn, gpointer data) {
    LumoBrowser *b = data; (void)btn;
    if (b->bookmark_count >= LUMO_MAX_BOOKMARKS) return;
    if (b->active_tab < 0 || b->active_tab >= b->tab_count) return;

    LumoTab *tab = &b->tabs[b->active_tab];
    const char *title = webkit_web_view_get_title(tab->web_view);
    const char *uri = webkit_web_view_get_uri(tab->web_view);
    if (!uri || !uri[0]) return;

    /* check for duplicate */
    for (int i = 0; i < b->bookmark_count; i++) {
        if (strcmp(b->bookmarks[i].uri, uri) == 0) return;
    }

    snprintf(b->bookmarks[b->bookmark_count].title, 64, "%s",
        title ? title : "Untitled");
    snprintf(b->bookmarks[b->bookmark_count].uri, 2048, "%s", uri);
    b->bookmark_count++;
    bookmarks_save(b);
}

/* ── CSS theming ───────────────────────────────────────────────────── */

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "window { background: " LUMO_BG "; }\n"
        "box { background: transparent; }\n"
        ".toolbar { background: " LUMO_SURFACE "; padding: 4px; }\n"
        ".tab-bar { background: " LUMO_BG "; padding: 2px 4px; }\n"
        ".tab-active { background: " LUMO_ACCENT "; color: " LUMO_TEXT ";"
        "  border-radius: 8px; padding: 6px 12px; font-weight: bold;"
        "  border: none; min-height: 28px; }\n"
        ".tab-inactive { background: " LUMO_SURFACE "; color: " LUMO_DIM ";"
        "  border-radius: 8px; padding: 6px 12px;"
        "  border: 1px solid " LUMO_BORDER "; min-height: 28px; }\n"
        ".tab-inactive:hover { color: " LUMO_TEXT "; border-color: " LUMO_ACCENT "; }\n"
        ".nav-btn { background: " LUMO_SURFACE "; color: " LUMO_DIM ";"
        "  border-radius: 8px; padding: 4px 10px;"
        "  border: 1px solid " LUMO_BORDER "; min-width: 32px; min-height: 32px; }\n"
        ".nav-btn:hover { color: " LUMO_ACCENT "; border-color: " LUMO_ACCENT "; }\n"
        ".accent-btn { background: " LUMO_ACCENT "; color: " LUMO_TEXT ";"
        "  border-radius: 8px; padding: 4px 10px;"
        "  border: none; min-width: 32px; min-height: 32px; }\n"
        ".url-entry { background: #1D0014; color: " LUMO_TEXT ";"
        "  border: 2px solid " LUMO_BORDER "; border-radius: 20px;"
        "  padding: 6px 16px; font-size: 14px; }\n"
        ".url-entry:focus { border-color: " LUMO_ACCENT "; }\n"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/* ── App activation ────────────────────────────────────────────────── */

static void activate(GtkApplication *app, gpointer user_data) {
    LumoBrowser *b = user_data;

    apply_css();
    bookmarks_load(b);

    b->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(b->window, LUMO_BROWSER_TITLE);
    gtk_window_set_default_size(b->window, 1280, 800);
    gtk_window_fullscreen(b->window);

    b->main_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_window_set_child(b->window, GTK_WIDGET(b->main_box));

    /* ── Tab bar ── */
    b->tab_bar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    gtk_widget_add_css_class(GTK_WIDGET(b->tab_bar), "tab-bar");

    b->new_tab_btn = GTK_BUTTON(gtk_button_new_with_label("+"));
    gtk_widget_add_css_class(GTK_WIDGET(b->new_tab_btn), "accent-btn");
    g_signal_connect(b->new_tab_btn, "clicked",
        G_CALLBACK(on_new_tab), b);
    gtk_box_append(b->tab_bar, GTK_WIDGET(b->new_tab_btn));

    GtkButton *close_tab_btn = GTK_BUTTON(gtk_button_new_with_label("X"));
    gtk_widget_add_css_class(GTK_WIDGET(close_tab_btn), "nav-btn");
    g_signal_connect(close_tab_btn, "clicked",
        G_CALLBACK(on_close_tab), b);
    gtk_box_append(b->tab_bar, GTK_WIDGET(close_tab_btn));

    gtk_box_append(b->main_box, GTK_WIDGET(b->tab_bar));

    /* ── Toolbar ── */
    b->toolbar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    gtk_widget_add_css_class(GTK_WIDGET(b->toolbar), "toolbar");

    b->back_btn = GTK_BUTTON(gtk_button_new_with_label("<"));
    b->forward_btn = GTK_BUTTON(gtk_button_new_with_label(">"));
    b->reload_btn = GTK_BUTTON(gtk_button_new_with_label("R"));
    b->bookmark_btn = GTK_BUTTON(gtk_button_new_with_label("*"));

    gtk_widget_add_css_class(GTK_WIDGET(b->back_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(b->forward_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(b->reload_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(b->bookmark_btn), "nav-btn");

    b->url_bar = GTK_ENTRY(gtk_entry_new());
    gtk_widget_add_css_class(GTK_WIDGET(b->url_bar), "url-entry");
    gtk_editable_set_text(GTK_EDITABLE(b->url_bar), "about:lumo");
    gtk_widget_set_hexpand(GTK_WIDGET(b->url_bar), TRUE);

    g_signal_connect(b->back_btn, "clicked", G_CALLBACK(on_back), b);
    g_signal_connect(b->forward_btn, "clicked", G_CALLBACK(on_forward), b);
    g_signal_connect(b->reload_btn, "clicked", G_CALLBACK(on_reload), b);
    g_signal_connect(b->bookmark_btn, "clicked",
        G_CALLBACK(on_add_bookmark), b);
    g_signal_connect(b->url_bar, "activate",
        G_CALLBACK(on_url_activate), b);

    gtk_box_append(b->toolbar, GTK_WIDGET(b->back_btn));
    gtk_box_append(b->toolbar, GTK_WIDGET(b->forward_btn));
    gtk_box_append(b->toolbar, GTK_WIDGET(b->reload_btn));
    gtk_box_append(b->toolbar, GTK_WIDGET(b->url_bar));
    gtk_box_append(b->toolbar, GTK_WIDGET(b->bookmark_btn));

    gtk_box_append(b->main_box, GTK_WIDGET(b->toolbar));

    /* ── View stack (tab content) ── */
    b->view_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(b->view_stack,
        GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(b->view_stack, 150);
    gtk_widget_set_vexpand(GTK_WIDGET(b->view_stack), TRUE);
    gtk_box_append(b->main_box, GTK_WIDGET(b->view_stack));

    /* ── First tab with start page ── */
    b->active_tab = 0;
    on_new_tab(NULL, b);

    gtk_window_present(b->window);
}

int main(int argc, char **argv) {
    GtkApplication *app;
    LumoBrowser browser = {0};
    int status;

    /* reduce WebKit memory overhead on riscv64 */
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
    setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 0);
    setenv("G_SLICE", "always-malloc", 0);

    app = gtk_application_new("com.lumo.browser",
        G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &browser);
    status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

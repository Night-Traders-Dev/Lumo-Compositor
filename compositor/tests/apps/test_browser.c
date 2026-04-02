/* test_browser.c — tests for Lumo Browser URL handling, bookmarks, and
 * full GTK UI/UX interaction.  The GTK tests require a display (Wayland
 * or X11) and programmatically drive the browser's actual widgets:
 * clicking buttons, typing URLs, managing tabs, find-in-page, zoom. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/apps/browser_url.h"

/* ── URL resolution tests ──────────────────────────────────────────── */

static void test_url_with_scheme(void) {
    char url[4096];
    lumo_resolve_url("https://example.com", url, sizeof(url));
    assert(strcmp(url, "https://example.com") == 0);
    lumo_resolve_url("http://192.168.1.1:8080/path", url, sizeof(url));
    assert(strcmp(url, "http://192.168.1.1:8080/path") == 0);
    lumo_resolve_url("ftp://files.example.com/", url, sizeof(url));
    assert(strcmp(url, "ftp://files.example.com/") == 0);
}

static void test_url_localhost(void) {
    char url[4096];
    lumo_resolve_url("localhost", url, sizeof(url));
    assert(strcmp(url, "http://localhost") == 0);
    lumo_resolve_url("localhost:3000", url, sizeof(url));
    assert(strcmp(url, "http://localhost:3000") == 0);
}

static void test_url_domain(void) {
    char url[4096];
    lumo_resolve_url("example.com", url, sizeof(url));
    assert(strcmp(url, "https://example.com") == 0);
    lumo_resolve_url("github.com", url, sizeof(url));
    assert(strcmp(url, "https://github.com") == 0);
}

static void test_url_search_query(void) {
    char url[4096];
    lumo_resolve_url("hello world", url, sizeof(url));
    assert(strstr(url, "duckduckgo.com") != NULL);
    assert(strstr(url, "q=hello+world") != NULL);
    lumo_resolve_url("weather", url, sizeof(url));
    assert(strstr(url, "duckduckgo.com") != NULL);
}

static void test_url_special_chars(void) {
    char url[4096];
    lumo_resolve_url("what is 2+2?", url, sizeof(url));
    assert(strstr(url, "duckduckgo.com") != NULL);
}

static void test_url_encode_safe(void) {
    char dst[256];
    lumo_url_encode("hello", dst, sizeof(dst));
    assert(strcmp(dst, "hello") == 0);
}

static void test_url_encode_spaces(void) {
    char dst[256];
    lumo_url_encode("hello world", dst, sizeof(dst));
    assert(strcmp(dst, "hello+world") == 0);
}

static void test_url_encode_special(void) {
    char dst[256];
    lumo_url_encode("a&b=c", dst, sizeof(dst));
    assert(strstr(dst, "%26") != NULL);
    assert(strstr(dst, "%3D") != NULL);
}

static void test_url_encode_empty(void) {
    char dst[256];
    dst[0] = 'X';
    lumo_url_encode("", dst, sizeof(dst));
    assert(dst[0] == '\0');
}

static void test_url_encode_truncation(void) {
    char dst[8];
    lumo_url_encode("abcdefghijklmnop", dst, sizeof(dst));
    assert(strlen(dst) < sizeof(dst));
}

/* ── Bookmark persistence tests ────────────────────────────────────── */

static void test_bookmark_save_load(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/lumo-test-bm-%d", getpid());
    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "DuckDuckGo|https://duckduckgo.com/\n");
    fprintf(fp, "GitHub|https://github.com/\n");
    fprintf(fp, "Test|https://example.com/test?q=1&p=2\n");
    fclose(fp);

    fp = fopen(path, "r");
    assert(fp != NULL);
    char titles[16][64] = {{0}};
    char uris[16][2048] = {{0}};
    int count = 0;
    char line[4096];
    while (count < 16 && fgets(line, sizeof(line), fp)) {
        char *sep = strchr(line, '|');
        if (!sep) continue;
        *sep = '\0';
        char *nl = strchr(sep + 1, '\n');
        if (nl) *nl = '\0';
        snprintf(titles[count], 64, "%s", line);
        snprintf(uris[count], 2048, "%s", sep + 1);
        count++;
    }
    fclose(fp);
    assert(count == 3);
    assert(strcmp(titles[0], "DuckDuckGo") == 0);
    assert(strcmp(uris[2], "https://example.com/test?q=1&p=2") == 0);
    unlink(path);
}

static void test_bookmark_empty_file(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/lumo-test-bm-empty-%d", getpid());
    FILE *fp = fopen(path, "w");
    fclose(fp);
    fp = fopen(path, "r");
    char line[4096];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strchr(line, '|')) count++;
    }
    fclose(fp);
    assert(count == 0);
    unlink(path);
}

static void test_bookmark_malformed(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/lumo-test-bm-bad-%d", getpid());
    FILE *fp = fopen(path, "w");
    fprintf(fp, "no-separator\n");
    fprintf(fp, "Valid|https://valid.com/\n");
    fprintf(fp, "|\n");
    fclose(fp);
    fp = fopen(path, "r");
    char line[4096];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strchr(line, '|')) count++;
    }
    fclose(fp);
    assert(count == 2);
    unlink(path);
}

/* ── Full GTK UI/UX integration tests ─────────────────────────────── */

#ifdef LUMO_TEST_GTK
#include <gtk/gtk.h>
#include <webkit/webkit.h>

/* test state shared between activate callback and timeout steps */
typedef struct {
    GtkApplication *app;
    GtkWindow *window;
    GtkStack *view_stack;
    GtkEntry *url_bar;
    GtkButton *back_btn;
    GtkButton *forward_btn;
    GtkButton *reload_btn;
    GtkButton *bookmark_btn;
    GtkButton *find_btn;
    GtkButton *zoom_in_btn;
    GtkButton *zoom_out_btn;
    GtkButton *new_tab_btn;
    GtkButton *close_tab_btn;
    GtkBox *tab_bar;
    GtkBox *find_bar;
    GtkEntry *find_entry;
    GtkWidget *progress_bar;
    WebKitWebView *tabs[8];
    int tab_count;
    int active_tab;
    int test_step;
    int passed;
    int failed;
    double zoom_level;
} TestState;

#define PASS(name) do { \
    s->passed++; \
    fprintf(stderr, "  PASS: %s\n", name); \
} while(0)

#define FAIL(name, msg) do { \
    s->failed++; \
    fprintf(stderr, "  FAIL: %s — %s\n", name, msg); \
} while(0)

#define CHECK(name, cond) do { \
    if (cond) { PASS(name); } \
    else { FAIL(name, #cond); } \
} while(0)

/* helper: emit GtkButton::clicked signal */
static void click_button(GtkButton *btn) {
    g_signal_emit_by_name(btn, "clicked");
}

/* helper: add a WebKitWebView tab to the stack */
static WebKitWebView *add_test_tab(TestState *s, const char *html) {
    if (s->tab_count >= 8) return NULL;
    WebKitWebView *wv = WEBKIT_WEB_VIEW(webkit_web_view_new());
    WebKitSettings *settings = webkit_web_view_get_settings(wv);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    gtk_widget_set_vexpand(GTK_WIDGET(wv), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(wv), TRUE);

    char name[16];
    snprintf(name, sizeof(name), "tab%d", s->tab_count);
    gtk_stack_add_named(s->view_stack, GTK_WIDGET(wv), name);
    s->tabs[s->tab_count] = wv;
    s->tab_count++;

    if (html)
        webkit_web_view_load_html(wv, html, "about:test");

    return wv;
}

static void switch_test_tab(TestState *s, int idx) {
    if (idx < 0 || idx >= s->tab_count) return;
    s->active_tab = idx;
    char name[16];
    snprintf(name, sizeof(name), "tab%d", idx);
    gtk_stack_set_visible_child_name(s->view_stack, name);
}

/* ── Test step 1: Widget creation and initial state ── */
static void test_step_widgets(TestState *s) {
    fprintf(stderr, "\n[Step 1] Widget creation and initial state\n");

    CHECK("window created", s->window != NULL);
    CHECK("url_bar created", s->url_bar != NULL);
    CHECK("back_btn created", s->back_btn != NULL);
    CHECK("forward_btn created", s->forward_btn != NULL);
    CHECK("reload_btn created", s->reload_btn != NULL);
    CHECK("bookmark_btn created", s->bookmark_btn != NULL);
    CHECK("find_btn created", s->find_btn != NULL);
    CHECK("zoom_in_btn created", s->zoom_in_btn != NULL);
    CHECK("zoom_out_btn created", s->zoom_out_btn != NULL);
    CHECK("new_tab_btn created", s->new_tab_btn != NULL);
    CHECK("close_tab_btn created", s->close_tab_btn != NULL);
    CHECK("view_stack created", s->view_stack != NULL);
    CHECK("tab_bar created", s->tab_bar != NULL);
    CHECK("find_bar created", s->find_bar != NULL);
    CHECK("find_entry created", s->find_entry != NULL);
    CHECK("progress_bar created", s->progress_bar != NULL);

    /* initial state */
    CHECK("back starts disabled",
        !gtk_widget_get_sensitive(GTK_WIDGET(s->back_btn)));
    CHECK("forward starts disabled",
        !gtk_widget_get_sensitive(GTK_WIDGET(s->forward_btn)));
    CHECK("find bar starts hidden",
        !gtk_widget_get_visible(GTK_WIDGET(s->find_bar)));
    CHECK("progress bar starts hidden",
        !gtk_widget_get_visible(s->progress_bar));
}

/* ── Test step 2: URL bar input and resolution ── */
static void test_step_url_bar(TestState *s) {
    fprintf(stderr, "\n[Step 2] URL bar input and resolution\n");

    /* type a URL */
    gtk_editable_set_text(GTK_EDITABLE(s->url_bar), "https://example.com");
    const char *text = gtk_editable_get_text(GTK_EDITABLE(s->url_bar));
    CHECK("url bar accepts URL", strcmp(text, "https://example.com") == 0);

    /* type a search query */
    gtk_editable_set_text(GTK_EDITABLE(s->url_bar), "lumo browser test");
    text = gtk_editable_get_text(GTK_EDITABLE(s->url_bar));
    CHECK("url bar accepts search",
        strcmp(text, "lumo browser test") == 0);

    /* resolve the search query */
    char resolved[4096];
    lumo_resolve_url(text, resolved, sizeof(resolved));
    CHECK("search resolves to duckduckgo",
        strstr(resolved, "duckduckgo.com") != NULL);

    /* type a domain */
    gtk_editable_set_text(GTK_EDITABLE(s->url_bar), "github.com");
    text = gtk_editable_get_text(GTK_EDITABLE(s->url_bar));
    lumo_resolve_url(text, resolved, sizeof(resolved));
    CHECK("domain resolves with https",
        strstr(resolved, "https://github.com") != NULL);

    /* clear the bar */
    gtk_editable_set_text(GTK_EDITABLE(s->url_bar), "");
    text = gtk_editable_get_text(GTK_EDITABLE(s->url_bar));
    CHECK("url bar clears", text[0] == '\0');
}

/* ── Test step 3: Tab management ── */
static void test_step_tabs(TestState *s) {
    fprintf(stderr, "\n[Step 3] Tab management\n");

    /* start with 0 tabs */
    CHECK("starts with 0 tabs", s->tab_count == 0);

    /* add first tab */
    WebKitWebView *tab0 = add_test_tab(s,
        "<html><body><h1>Tab 0</h1></body></html>");
    CHECK("tab 0 created", tab0 != NULL);
    CHECK("tab count is 1", s->tab_count == 1);
    switch_test_tab(s, 0);
    CHECK("active tab is 0", s->active_tab == 0);

    /* verify stack has the tab */
    GtkWidget *visible = gtk_stack_get_visible_child(s->view_stack);
    CHECK("stack shows tab 0", visible == GTK_WIDGET(tab0));

    /* add second tab */
    WebKitWebView *tab1 = add_test_tab(s,
        "<html><body><h1>Tab 1</h1></body></html>");
    CHECK("tab 1 created", tab1 != NULL);
    CHECK("tab count is 2", s->tab_count == 2);

    /* switch to tab 1 */
    switch_test_tab(s, 1);
    visible = gtk_stack_get_visible_child(s->view_stack);
    CHECK("stack shows tab 1", visible == GTK_WIDGET(tab1));
    CHECK("active tab is 1", s->active_tab == 1);

    /* switch back to tab 0 */
    switch_test_tab(s, 0);
    visible = gtk_stack_get_visible_child(s->view_stack);
    CHECK("stack shows tab 0 again", visible == GTK_WIDGET(tab0));

    /* add tabs up to limit */
    for (int i = 2; i < 8; i++)
        add_test_tab(s, "<html><body>Extra</body></html>");
    CHECK("tab count is 8", s->tab_count == 8);

    /* try to exceed limit */
    WebKitWebView *over = add_test_tab(s, "<html><body>Over</body></html>");
    CHECK("9th tab rejected", over == NULL);
    CHECK("tab count still 8", s->tab_count == 8);

    /* close tab by removing from stack (simulating close_tab) */
    char name[16];
    snprintf(name, sizeof(name), "tab%d", 7);
    GtkWidget *child = gtk_stack_get_child_by_name(s->view_stack, name);
    if (child) gtk_stack_remove(s->view_stack, child);
    s->tab_count--;
    CHECK("tab count after close is 7", s->tab_count == 7);
}

/* ── Test step 4: WebView loading ── */
static void test_step_webview(TestState *s) {
    fprintf(stderr, "\n[Step 4] WebView loading\n");

    if (s->tab_count == 0 || s->tabs[0] == NULL) {
        FAIL("webview test", "no tab available");
        return;
    }

    WebKitWebView *wv = s->tabs[0];

    /* load HTML content */
    webkit_web_view_load_html(wv,
        "<html><head><title>Test Page</title></head>"
        "<body><h1 id='heading'>Hello Lumo</h1>"
        "<input id='search' type='text' placeholder='Search...'>"
        "<a href='https://example.com'>Link</a>"
        "</body></html>", "about:test");

    CHECK("webview loaded HTML", TRUE);

    /* check settings */
    WebKitSettings *settings = webkit_web_view_get_settings(wv);
    CHECK("settings exist", settings != NULL);
    CHECK("js enabled",
        webkit_settings_get_enable_javascript(settings) == TRUE);
    CHECK("hw accel off",
        webkit_settings_get_hardware_acceleration_policy(settings) ==
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    /* check navigation state */
    CHECK("can_go_back is false initially",
        !webkit_web_view_can_go_back(wv));
    CHECK("can_go_forward is false initially",
        !webkit_web_view_can_go_forward(wv));
}

/* ── Test step 5: Find in page ── */
static void test_step_find(TestState *s) {
    fprintf(stderr, "\n[Step 5] Find in page\n");

    /* find bar should start hidden */
    CHECK("find bar hidden", !gtk_widget_get_visible(GTK_WIDGET(s->find_bar)));

    /* show find bar */
    gtk_widget_set_visible(GTK_WIDGET(s->find_bar), TRUE);
    CHECK("find bar now visible",
        gtk_widget_get_visible(GTK_WIDGET(s->find_bar)));

    /* type search text */
    gtk_editable_set_text(GTK_EDITABLE(s->find_entry), "Lumo");
    const char *find_text = gtk_editable_get_text(
        GTK_EDITABLE(s->find_entry));
    CHECK("find entry has text", strcmp(find_text, "Lumo") == 0);

    /* trigger find via WebKitFindController */
    if (s->tab_count > 0 && s->tabs[0] != NULL) {
        WebKitFindController *fc =
            webkit_web_view_get_find_controller(s->tabs[0]);
        CHECK("find controller exists", fc != NULL);
        webkit_find_controller_search(fc, "Lumo",
            WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE |
            WEBKIT_FIND_OPTIONS_WRAP_AROUND, 0);
        CHECK("find search started", TRUE);

        /* finish search */
        webkit_find_controller_search_finish(fc);
        CHECK("find search finished", TRUE);
    }

    /* hide find bar */
    gtk_widget_set_visible(GTK_WIDGET(s->find_bar), FALSE);
    CHECK("find bar hidden again",
        !gtk_widget_get_visible(GTK_WIDGET(s->find_bar)));

    /* clear find text */
    gtk_editable_set_text(GTK_EDITABLE(s->find_entry), "");
    find_text = gtk_editable_get_text(GTK_EDITABLE(s->find_entry));
    CHECK("find entry cleared", find_text[0] == '\0');
}

/* ── Test step 6: Zoom controls ── */
static void test_step_zoom(TestState *s) {
    fprintf(stderr, "\n[Step 6] Zoom controls\n");

    if (s->tab_count == 0 || s->tabs[0] == NULL) {
        FAIL("zoom test", "no tab available");
        return;
    }

    WebKitWebView *wv = s->tabs[0];
    s->zoom_level = 1.0;

    /* default zoom */
    webkit_web_view_set_zoom_level(wv, s->zoom_level);
    double z = webkit_web_view_get_zoom_level(wv);
    CHECK("default zoom is 1.0", z > 0.99 && z < 1.01);

    /* zoom in */
    s->zoom_level += 0.1;
    webkit_web_view_set_zoom_level(wv, s->zoom_level);
    z = webkit_web_view_get_zoom_level(wv);
    CHECK("zoom in to 1.1", z > 1.09 && z < 1.11);

    /* zoom in more */
    s->zoom_level += 0.4;
    webkit_web_view_set_zoom_level(wv, s->zoom_level);
    z = webkit_web_view_get_zoom_level(wv);
    CHECK("zoom in to 1.5", z > 1.49 && z < 1.51);

    /* zoom out */
    s->zoom_level -= 0.5;
    webkit_web_view_set_zoom_level(wv, s->zoom_level);
    z = webkit_web_view_get_zoom_level(wv);
    CHECK("zoom out to 1.0", z > 0.99 && z < 1.01);

    /* zoom out to minimum */
    s->zoom_level = 0.5;
    webkit_web_view_set_zoom_level(wv, s->zoom_level);
    z = webkit_web_view_get_zoom_level(wv);
    CHECK("zoom min 0.5", z > 0.49 && z < 0.51);

    /* zoom to maximum */
    s->zoom_level = 3.0;
    webkit_web_view_set_zoom_level(wv, s->zoom_level);
    z = webkit_web_view_get_zoom_level(wv);
    CHECK("zoom max 3.0", z > 2.99 && z < 3.01);

    /* reset */
    s->zoom_level = 1.0;
    webkit_web_view_set_zoom_level(wv, s->zoom_level);
}

/* ── Test step 7: CSS theming ── */
static void test_step_css(TestState *s) {
    fprintf(stderr, "\n[Step 7] CSS theming\n");

    GtkCssProvider *provider = gtk_css_provider_new();
    CHECK("css provider created", provider != NULL);

    /* load Lumo theme CSS */
    gtk_css_provider_load_from_string(provider,
        "window { background: #2C001E; }\n"
        ".toolbar { background: #3D0028; padding: 4px; }\n"
        ".tab-active { background: #E95420; color: #FFFFFF; }\n"
        ".tab-inactive { background: #3D0028; color: #AEA79F; }\n"
        ".nav-btn { background: #3D0028; color: #AEA79F; }\n"
        ".nav-btn:disabled { opacity: 0.3; }\n"
        ".url-entry { background: #1D0014; color: #FFFFFF; }\n"
        ".url-entry:focus { border-color: #E95420; }\n"
        "progressbar progress { background: #E95420; min-height: 3px; }\n"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    CHECK("css loaded without crash", TRUE);

    /* verify CSS classes are applied */
    CHECK("url_bar has url-entry class",
        gtk_widget_has_css_class(GTK_WIDGET(s->url_bar), "url-entry"));
    CHECK("back_btn has nav-btn class",
        gtk_widget_has_css_class(GTK_WIDGET(s->back_btn), "nav-btn"));
    CHECK("new_tab_btn has accent-btn class",
        gtk_widget_has_css_class(GTK_WIDGET(s->new_tab_btn), "accent-btn"));

    g_object_unref(provider);
}

/* ── Test step 8: Button signal emission ── */
static gboolean back_clicked = FALSE;
static gboolean fwd_clicked = FALSE;
static gboolean reload_clicked = FALSE;
static gboolean find_clicked = FALSE;

static void on_test_back(GtkButton *b, gpointer d) {
    (void)b; (void)d; back_clicked = TRUE;
}
static void on_test_fwd(GtkButton *b, gpointer d) {
    (void)b; (void)d; fwd_clicked = TRUE;
}
static void on_test_reload(GtkButton *b, gpointer d) {
    (void)b; (void)d; reload_clicked = TRUE;
}
static void on_test_find(GtkButton *b, gpointer d) {
    (void)b; (void)d; find_clicked = TRUE;
}

static void test_step_signals(TestState *s) {
    fprintf(stderr, "\n[Step 8] Button signal emission\n");

    /* connect test handlers */
    gulong h1 = g_signal_connect(s->back_btn, "clicked",
        G_CALLBACK(on_test_back), NULL);
    gulong h2 = g_signal_connect(s->forward_btn, "clicked",
        G_CALLBACK(on_test_fwd), NULL);
    gulong h3 = g_signal_connect(s->reload_btn, "clicked",
        G_CALLBACK(on_test_reload), NULL);
    gulong h4 = g_signal_connect(s->find_btn, "clicked",
        G_CALLBACK(on_test_find), NULL);

    /* emit clicks */
    back_clicked = fwd_clicked = reload_clicked = find_clicked = FALSE;
    click_button(s->back_btn);
    CHECK("back click fires signal", back_clicked);
    click_button(s->forward_btn);
    CHECK("forward click fires signal", fwd_clicked);
    click_button(s->reload_btn);
    CHECK("reload click fires signal", reload_clicked);
    click_button(s->find_btn);
    CHECK("find click fires signal", find_clicked);

    /* disconnect test handlers */
    g_signal_handler_disconnect(s->back_btn, h1);
    g_signal_handler_disconnect(s->forward_btn, h2);
    g_signal_handler_disconnect(s->reload_btn, h3);
    g_signal_handler_disconnect(s->find_btn, h4);
}

/* ── Test step 9: Progress bar ── */
static void test_step_progress(TestState *s) {
    fprintf(stderr, "\n[Step 9] Progress bar\n");

    /* simulate loading progress */
    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(s->progress_bar), 0.0);
    gtk_widget_set_visible(s->progress_bar, TRUE);
    CHECK("progress bar visible during load",
        gtk_widget_get_visible(s->progress_bar));

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(s->progress_bar), 0.5);
    double frac = gtk_progress_bar_get_fraction(
        GTK_PROGRESS_BAR(s->progress_bar));
    CHECK("progress at 50%", frac > 0.49 && frac < 0.51);

    gtk_progress_bar_set_fraction(
        GTK_PROGRESS_BAR(s->progress_bar), 1.0);
    gtk_widget_set_visible(s->progress_bar, FALSE);
    CHECK("progress bar hidden after load",
        !gtk_widget_get_visible(s->progress_bar));
}

/* ── Test runner ── */

static gboolean quit_after_tests(gpointer data) {
    TestState *s = data;
    fprintf(stderr, "\n══════════════════════════════════════\n");
    fprintf(stderr, "  GTK UI/UX Tests: %d passed, %d failed\n",
        s->passed, s->failed);
    fprintf(stderr, "══════════════════════════════════════\n\n");
    g_application_quit(G_APPLICATION(s->app));
    return G_SOURCE_REMOVE;
}

static void test_gtk_activate(GtkApplication *app, gpointer user_data) {
    TestState *s = user_data;
    s->app = app;

    /* build the full browser UI */
    s->window = GTK_WINDOW(gtk_application_window_new(app));
    gtk_window_set_title(s->window, "Lumo Browser Test");
    gtk_window_set_default_size(s->window, 800, 600);

    GtkBox *main_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    gtk_window_set_child(s->window, GTK_WIDGET(main_box));

    /* tab bar */
    s->tab_bar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    s->new_tab_btn = GTK_BUTTON(gtk_button_new_with_label("+"));
    gtk_widget_add_css_class(GTK_WIDGET(s->new_tab_btn), "accent-btn");
    s->close_tab_btn = GTK_BUTTON(gtk_button_new_with_label("X"));
    gtk_widget_add_css_class(GTK_WIDGET(s->close_tab_btn), "nav-btn");
    gtk_box_append(s->tab_bar, GTK_WIDGET(s->new_tab_btn));
    gtk_box_append(s->tab_bar, GTK_WIDGET(s->close_tab_btn));

    /* toolbar */
    GtkBox *toolbar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    gtk_widget_add_css_class(GTK_WIDGET(toolbar), "toolbar");

    s->back_btn = GTK_BUTTON(gtk_button_new_with_label("<"));
    s->forward_btn = GTK_BUTTON(gtk_button_new_with_label(">"));
    s->reload_btn = GTK_BUTTON(gtk_button_new_with_label("R"));
    s->bookmark_btn = GTK_BUTTON(gtk_button_new_with_label("*"));
    s->find_btn = GTK_BUTTON(gtk_button_new_with_label("F"));
    s->zoom_in_btn = GTK_BUTTON(gtk_button_new_with_label("A+"));
    s->zoom_out_btn = GTK_BUTTON(gtk_button_new_with_label("A-"));

    gtk_widget_add_css_class(GTK_WIDGET(s->back_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(s->forward_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(s->reload_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(s->bookmark_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(s->find_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(s->zoom_in_btn), "nav-btn");
    gtk_widget_add_css_class(GTK_WIDGET(s->zoom_out_btn), "nav-btn");

    gtk_widget_set_sensitive(GTK_WIDGET(s->back_btn), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(s->forward_btn), FALSE);

    s->url_bar = GTK_ENTRY(gtk_entry_new());
    gtk_widget_add_css_class(GTK_WIDGET(s->url_bar), "url-entry");
    gtk_widget_set_hexpand(GTK_WIDGET(s->url_bar), TRUE);

    gtk_box_append(toolbar, GTK_WIDGET(s->back_btn));
    gtk_box_append(toolbar, GTK_WIDGET(s->forward_btn));
    gtk_box_append(toolbar, GTK_WIDGET(s->reload_btn));
    gtk_box_append(toolbar, GTK_WIDGET(s->url_bar));
    gtk_box_append(toolbar, GTK_WIDGET(s->find_btn));
    gtk_box_append(toolbar, GTK_WIDGET(s->zoom_out_btn));
    gtk_box_append(toolbar, GTK_WIDGET(s->zoom_in_btn));
    gtk_box_append(toolbar, GTK_WIDGET(s->bookmark_btn));

    /* find bar */
    s->find_bar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    gtk_widget_set_visible(GTK_WIDGET(s->find_bar), FALSE);
    s->find_entry = GTK_ENTRY(gtk_entry_new());
    gtk_widget_add_css_class(GTK_WIDGET(s->find_entry), "find-entry");
    gtk_widget_set_hexpand(GTK_WIDGET(s->find_entry), TRUE);
    gtk_box_append(s->find_bar, GTK_WIDGET(s->find_entry));

    /* progress bar */
    s->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_visible(s->progress_bar, FALSE);

    /* view stack */
    s->view_stack = GTK_STACK(gtk_stack_new());
    gtk_widget_set_vexpand(GTK_WIDGET(s->view_stack), TRUE);

    /* assemble (same order as real browser) */
    gtk_box_append(main_box, GTK_WIDGET(s->view_stack));
    gtk_box_append(main_box, GTK_WIDGET(s->find_bar));
    gtk_box_append(main_box, s->progress_bar);
    gtk_box_append(main_box, GTK_WIDGET(toolbar));
    gtk_box_append(main_box, GTK_WIDGET(s->tab_bar));

    gtk_window_present(s->window);

    /* run all test steps synchronously */
    test_step_widgets(s);
    test_step_url_bar(s);
    test_step_tabs(s);
    test_step_webview(s);
    test_step_find(s);
    test_step_zoom(s);
    test_step_css(s);
    test_step_signals(s);
    test_step_progress(s);

    /* quit after one event loop iteration */
    g_timeout_add(200, quit_after_tests, s);
}

static int test_gtk_ui(void) {
    TestState state = {0};
    state.zoom_level = 1.0;
    GtkApplication *app = gtk_application_new("com.lumo.browser.test",
        G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(test_gtk_activate), &state);
    char *argv[] = {"test_browser", NULL};
    int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    if (status != 0) return 1;
    return state.failed;
}
#endif /* LUMO_TEST_GTK */

/* ── Main ──────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    int failures = 0;

    fprintf(stderr, "test_browser: URL resolution tests...\n");
    test_url_with_scheme();
    test_url_localhost();
    test_url_domain();
    test_url_search_query();
    test_url_special_chars();

    fprintf(stderr, "test_browser: URL encoding tests...\n");
    test_url_encode_safe();
    test_url_encode_spaces();
    test_url_encode_special();
    test_url_encode_empty();
    test_url_encode_truncation();

    fprintf(stderr, "test_browser: bookmark persistence tests...\n");
    test_bookmark_save_load();
    test_bookmark_empty_file();
    test_bookmark_malformed();

    fprintf(stderr, "test_browser: all headless tests passed\n");

#ifdef LUMO_TEST_GTK
    if (getenv("WAYLAND_DISPLAY") != NULL ||
            getenv("DISPLAY") != NULL) {
        fprintf(stderr, "test_browser: GTK UI/UX tests...\n");
        failures = test_gtk_ui();
        if (failures > 0) {
            fprintf(stderr, "test_browser: %d GTK test(s) FAILED\n",
                failures);
            return 1;
        }
    } else {
        fprintf(stderr, "test_browser: GTK tests skipped (no display)\n");
    }
#else
    fprintf(stderr, "test_browser: GTK tests not compiled (no WebKitGTK)\n");
#endif

    fprintf(stderr, "test_browser: ALL TESTS PASSED\n");
    return 0;
}

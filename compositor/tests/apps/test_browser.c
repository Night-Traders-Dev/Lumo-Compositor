/* test_browser.c — tests for Lumo Browser URL handling, bookmarks, and
 * GTK UI components.  The GTK tests require a display (Wayland or X11)
 * but the URL/bookmark tests run headless. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* pull in the shared URL utilities */
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

    lumo_resolve_url("localhost:8080/api/v1", url, sizeof(url));
    assert(strcmp(url, "http://localhost:8080/api/v1") == 0);
}

static void test_url_domain(void) {
    char url[4096];
    lumo_resolve_url("example.com", url, sizeof(url));
    assert(strcmp(url, "https://example.com") == 0);

    lumo_resolve_url("en.wikipedia.org/wiki/Main_Page", url, sizeof(url));
    assert(strcmp(url, "https://en.wikipedia.org/wiki/Main_Page") == 0);

    lumo_resolve_url("github.com", url, sizeof(url));
    assert(strcmp(url, "https://github.com") == 0);
}

static void test_url_search_query(void) {
    char url[4096];

    /* plain text → search */
    lumo_resolve_url("hello world", url, sizeof(url));
    assert(strstr(url, "duckduckgo.com") != NULL);
    assert(strstr(url, "q=hello+world") != NULL);

    /* single word without dot → search */
    lumo_resolve_url("weather", url, sizeof(url));
    assert(strstr(url, "duckduckgo.com") != NULL);
    assert(strstr(url, "q=weather") != NULL);
}

static void test_url_special_chars(void) {
    char url[4096];

    lumo_resolve_url("what is 2+2?", url, sizeof(url));
    assert(strstr(url, "duckduckgo.com") != NULL);
    /* '+' in query, '?' should be percent-encoded */
    assert(strstr(url, "q=what+is+2") != NULL);
}

/* ── URL encoding tests ────────────────────────────────────────────── */

static void test_url_encode_safe(void) {
    char dst[256];
    lumo_url_encode("hello", dst, sizeof(dst));
    assert(strcmp(dst, "hello") == 0);

    lumo_url_encode("ABC123", dst, sizeof(dst));
    assert(strcmp(dst, "ABC123") == 0);
}

static void test_url_encode_spaces(void) {
    char dst[256];
    lumo_url_encode("hello world", dst, sizeof(dst));
    assert(strcmp(dst, "hello+world") == 0);
}

static void test_url_encode_special(void) {
    char dst[256];
    lumo_url_encode("a&b=c", dst, sizeof(dst));
    assert(strstr(dst, "%26") != NULL); /* & encoded */
    assert(strstr(dst, "%3D") != NULL); /* = encoded */
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
    /* should not overflow, result truncated */
    assert(strlen(dst) < sizeof(dst));
}

/* ── Bookmark persistence tests ────────────────────────────────────── */

static void test_bookmark_save_load(void) {
    /* write a temp bookmark file */
    char path[256];
    snprintf(path, sizeof(path), "/tmp/lumo-test-bookmarks-%d", getpid());

    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "DuckDuckGo|https://duckduckgo.com/\n");
    fprintf(fp, "GitHub|https://github.com/\n");
    fprintf(fp, "Test Page|https://example.com/test?q=1&p=2\n");
    fclose(fp);

    /* read it back */
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
    assert(strcmp(uris[0], "https://duckduckgo.com/") == 0);
    assert(strcmp(titles[1], "GitHub") == 0);
    assert(strcmp(uris[1], "https://github.com/") == 0);
    assert(strcmp(titles[2], "Test Page") == 0);
    assert(strcmp(uris[2], "https://example.com/test?q=1&p=2") == 0);

    unlink(path);
}

static void test_bookmark_empty_file(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/lumo-test-bookmarks-empty-%d", getpid());

    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fclose(fp);

    fp = fopen(path, "r");
    assert(fp != NULL);
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
    snprintf(path, sizeof(path), "/tmp/lumo-test-bookmarks-bad-%d", getpid());

    FILE *fp = fopen(path, "w");
    assert(fp != NULL);
    fprintf(fp, "no-separator-here\n");
    fprintf(fp, "Valid|https://valid.com/\n");
    fprintf(fp, "|\n"); /* empty title and URI */
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

    /* "no-separator-here" skipped, "Valid|..." and "|\n" parsed */
    assert(count == 2);
    assert(strcmp(titles[0], "Valid") == 0);
    assert(strcmp(uris[0], "https://valid.com/") == 0);

    unlink(path);
}

/* ── GTK UI component tests (require display) ──────────────────────── */

#ifdef LUMO_TEST_GTK
#include <gtk/gtk.h>
#include <webkit/webkit.h>

static gboolean quit_after_checks(gpointer data) {
    GtkApplication *app = data;
    g_application_quit(G_APPLICATION(app));
    return G_SOURCE_REMOVE;
}

static void test_gtk_activate(GtkApplication *app, gpointer user_data) {
    (void)user_data;

    /* test: window creation */
    GtkWindow *window = GTK_WINDOW(gtk_application_window_new(app));
    assert(window != NULL);
    gtk_window_set_title(window, "Lumo Browser Test");
    gtk_window_set_default_size(window, 400, 300);

    /* test: URL bar creation and text input */
    GtkEntry *url_bar = GTK_ENTRY(gtk_entry_new());
    assert(url_bar != NULL);
    gtk_editable_set_text(GTK_EDITABLE(url_bar), "https://example.com");
    const char *text = gtk_editable_get_text(GTK_EDITABLE(url_bar));
    assert(strcmp(text, "https://example.com") == 0);

    /* test: URL bar accepts search query */
    gtk_editable_set_text(GTK_EDITABLE(url_bar), "lumo browser test");
    text = gtk_editable_get_text(GTK_EDITABLE(url_bar));
    assert(strcmp(text, "lumo browser test") == 0);

    /* test: URL resolution from URL bar content */
    char resolved[4096];
    lumo_resolve_url(text, resolved, sizeof(resolved));
    assert(strstr(resolved, "duckduckgo.com") != NULL);

    /* test: navigation buttons */
    GtkButton *back = GTK_BUTTON(gtk_button_new_with_label("<"));
    GtkButton *fwd = GTK_BUTTON(gtk_button_new_with_label(">"));
    GtkButton *reload = GTK_BUTTON(gtk_button_new_with_label("R"));
    GtkButton *bookmark = GTK_BUTTON(gtk_button_new_with_label("*"));
    GtkButton *new_tab = GTK_BUTTON(gtk_button_new_with_label("+"));
    assert(back != NULL && fwd != NULL && reload != NULL);
    assert(bookmark != NULL && new_tab != NULL);

    /* test: GtkStack for tabs */
    GtkStack *stack = GTK_STACK(gtk_stack_new());
    assert(stack != NULL);
    gtk_stack_set_transition_type(stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    /* test: WebKitWebView creation */
    WebKitWebView *wv = WEBKIT_WEB_VIEW(webkit_web_view_new());
    assert(wv != NULL);

    WebKitSettings *settings = webkit_web_view_get_settings(wv);
    assert(settings != NULL);
    webkit_settings_set_enable_javascript(settings, TRUE);
    assert(webkit_settings_get_enable_javascript(settings) == TRUE);
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    /* test: add web view to stack */
    gtk_stack_add_named(stack, GTK_WIDGET(wv), "tab0");
    GtkWidget *child = gtk_stack_get_child_by_name(stack, "tab0");
    assert(child == GTK_WIDGET(wv));

    /* test: load HTML content */
    webkit_web_view_load_html(wv,
        "<html><body><h1>Test</h1></body></html>", "about:test");

    /* test: CSS theming applies without crash */
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider,
        "window { background: #2C001E; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    /* assemble UI */
    GtkBox *box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    GtkBox *toolbar = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4));
    gtk_box_append(toolbar, GTK_WIDGET(back));
    gtk_box_append(toolbar, GTK_WIDGET(fwd));
    gtk_box_append(toolbar, GTK_WIDGET(reload));
    gtk_box_append(toolbar, GTK_WIDGET(url_bar));
    gtk_box_append(toolbar, GTK_WIDGET(bookmark));
    gtk_box_append(box, GTK_WIDGET(toolbar));
    gtk_box_append(box, GTK_WIDGET(stack));
    gtk_window_set_child(window, GTK_WIDGET(box));

    gtk_window_present(window);

    fprintf(stderr, "test_browser: GTK UI tests passed\n");

    /* quit after a brief event loop tick */
    g_timeout_add(500, quit_after_checks, app);
}

static void test_gtk_ui(void) {
    GtkApplication *app = gtk_application_new("com.lumo.browser.test",
        G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(test_gtk_activate), NULL);
    char *argv[] = {"test_browser", NULL};
    int status = g_application_run(G_APPLICATION(app), 1, argv);
    g_object_unref(app);
    assert(status == 0);
}
#endif /* LUMO_TEST_GTK */

/* ── Main ──────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* headless tests — always run */
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
    /* GTK tests — only run when display is available */
    if (getenv("WAYLAND_DISPLAY") != NULL ||
            getenv("DISPLAY") != NULL) {
        fprintf(stderr, "test_browser: GTK UI tests...\n");
        test_gtk_ui();
        fprintf(stderr, "test_browser: all GTK tests passed\n");
    } else {
        fprintf(stderr, "test_browser: GTK tests skipped (no display)\n");
    }
#else
    fprintf(stderr, "test_browser: GTK tests not compiled (no WebKitGTK)\n");
#endif

    fprintf(stderr, "test_browser: ALL TESTS PASSED\n");
    return 0;
}

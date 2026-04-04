/*
 * app_github.c — Lumo GitHub client (native SHM app)
 *
 * Connects to the GitHub REST API via libcurl to display repositories,
 * commits, issues, and profile info.  Uses a personal access token
 * stored in ~/.lumo-github-token for authentication.
 *
 * Views:
 *   0 = login/token entry
 *   1 = profile + repository list
 *   2 = repository detail (README, recent commits)
 *   3 = issues list
 */
#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

/* ── minimal JSON field extractor ──────────────────────────────── */

/* extract a string value for a given key from JSON text.
 * returns pointer into buf (null-terminated), or NULL. */
static const char *json_get_string(const char *json, const char *key,
    char *buf, size_t buf_size)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return NULL;
    pos = strchr(pos + strlen(search), ':');
    if (!pos) return NULL;
    pos++;
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos == '"') {
        pos++;
        size_t i = 0;
        while (*pos && *pos != '"' && i < buf_size - 1) {
            if (*pos == '\\' && pos[1]) { pos++; }
            buf[i++] = *pos++;
        }
        buf[i] = '\0';
        return buf;
    }
    /* number or bool */
    size_t i = 0;
    while (*pos && *pos != ',' && *pos != '}' && *pos != ']' &&
            i < buf_size - 1) {
        buf[i++] = *pos++;
    }
    buf[i] = '\0';
    return buf;
}

/* extract array of objects — returns pointers to each '{' */
static int json_get_array_objects(const char *json,
    const char **out, int max_out)
{
    const char *p = strchr(json, '[');
    if (!p) return 0;
    p++;
    int count = 0;
    int depth = 0;
    while (*p && count < max_out) {
        if (*p == '{') {
            if (depth == 0) out[count++] = p;
            depth++;
        } else if (*p == '}') {
            depth--;
        } else if (*p == ']' && depth == 0) {
            break;
        }
        p++;
    }
    return count;
}

/* ── curl helpers ──────────────────────────────────────────────── */

struct curl_buf {
    char *data;
    size_t size;
    size_t capacity;
};

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb,
    void *userdata)
{
    struct curl_buf *buf = userdata;
    size_t total = size * nmemb;
    if (buf->size + total >= buf->capacity) {
        size_t new_cap = buf->capacity * 2;
        if (new_cap < buf->size + total + 1) new_cap = buf->size + total + 1;
        char *new_data = realloc(buf->data, new_cap);
        if (!new_data) return 0;
        buf->data = new_data;
        buf->capacity = new_cap;
    }
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static char *github_api_get(const char *token, const char *endpoint) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    char url[512];
    snprintf(url, sizeof(url), "https://api.github.com%s", endpoint);

    struct curl_buf buf = {0};
    buf.data = malloc(4096);
    buf.capacity = 4096;
    buf.data[0] = '\0';

    char auth[256];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", token);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: Lumo-OS/0.0.76");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

/* ── GitHub data model ─────────────────────────────────────────── */

#define GH_MAX_REPOS 20
#define GH_MAX_ITEMS 10

struct gh_repo {
    char name[64];
    char full_name[128];
    char description[256];
    char language[32];
    int stars;
    int forks;
    bool is_private;
};

struct gh_state {
    /* auth */
    char token[128];
    bool authenticated;
    char username[64];
    char avatar_url[256];
    int public_repos;
    int followers;

    /* data */
    struct gh_repo repos[GH_MAX_REPOS];
    int repo_count;

    /* UI state */
    int view;          /* 0=login, 1=repos, 2=detail, 3=issues */
    int selected_repo;
    int scroll_offset;

    /* async loading */
    bool loading;
    char status_msg[128];
};

static struct gh_state gh = {0};
static bool gh_initialized = false;

/* ── token persistence ─────────────────────────────────────────── */

static void gh_load_token(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/.lumo-github-token", home);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    if (fgets(gh.token, sizeof(gh.token), fp)) {
        char *nl = strchr(gh.token, '\n');
        if (nl) *nl = '\0';
        if (strlen(gh.token) > 10) {
            gh.authenticated = true;
        }
    }
    fclose(fp);
}

static void gh_save_token(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/.lumo-github-token", home);
    FILE *fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", gh.token);
    fclose(fp);
    /* restrict permissions */
    chmod(path, 0600);
}

/* ── API calls (run in background thread) ──────────────────────── */

static void gh_fetch_profile(void) {
    char *json = github_api_get(gh.token, "/user");
    if (!json) {
        snprintf(gh.status_msg, sizeof(gh.status_msg), "API ERROR");
        gh.loading = false;
        return;
    }

    char buf[256];
    if (json_get_string(json, "login", buf, sizeof(buf)))
        snprintf(gh.username, sizeof(gh.username), "%s", buf);
    if (json_get_string(json, "public_repos", buf, sizeof(buf)))
        gh.public_repos = atoi(buf);
    if (json_get_string(json, "followers", buf, sizeof(buf)))
        gh.followers = atoi(buf);
    if (json_get_string(json, "avatar_url", buf, sizeof(buf)))
        snprintf(gh.avatar_url, sizeof(gh.avatar_url), "%s", buf);

    gh.authenticated = (gh.username[0] != '\0');
    free(json);
}

static void gh_fetch_repos(void) {
    char *json = github_api_get(gh.token,
        "/user/repos?sort=updated&per_page=20");
    if (!json) {
        snprintf(gh.status_msg, sizeof(gh.status_msg), "REPOS ERROR");
        gh.loading = false;
        return;
    }

    const char *objects[GH_MAX_REPOS];
    int count = json_get_array_objects(json, objects, GH_MAX_REPOS);
    gh.repo_count = 0;

    for (int i = 0; i < count && i < GH_MAX_REPOS; i++) {
        struct gh_repo *r = &gh.repos[gh.repo_count];
        char buf[256];
        char chunk[4096];
        /* extract this object's JSON (up to next '}' at depth 0) */
        const char *start = objects[i];
        int depth = 0;
        size_t len = 0;
        for (const char *p = start; *p && len < sizeof(chunk) - 1; p++) {
            chunk[len++] = *p;
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) break; }
        }
        chunk[len] = '\0';

        if (json_get_string(chunk, "name", buf, sizeof(buf)))
            snprintf(r->name, sizeof(r->name), "%s", buf);
        if (json_get_string(chunk, "full_name", buf, sizeof(buf)))
            snprintf(r->full_name, sizeof(r->full_name), "%s", buf);
        if (json_get_string(chunk, "description", buf, sizeof(buf)))
            snprintf(r->description, sizeof(r->description), "%s", buf);
        if (json_get_string(chunk, "language", buf, sizeof(buf)))
            snprintf(r->language, sizeof(r->language), "%s", buf);
        if (json_get_string(chunk, "stargazers_count", buf, sizeof(buf)))
            r->stars = atoi(buf);
        if (json_get_string(chunk, "forks_count", buf, sizeof(buf)))
            r->forks = atoi(buf);
        if (json_get_string(chunk, "private", buf, sizeof(buf)))
            r->is_private = (strcmp(buf, "true") == 0);

        if (r->name[0] != '\0')
            gh.repo_count++;
    }

    free(json);
}

static void *gh_fetch_thread(void *arg) {
    (void)arg;
    gh.loading = true;
    snprintf(gh.status_msg, sizeof(gh.status_msg), "LOADING...");

    gh_fetch_profile();
    if (gh.authenticated) {
        gh_fetch_repos();
        gh.view = 1;
        snprintf(gh.status_msg, sizeof(gh.status_msg),
            "%d REPOS", gh.repo_count);
    } else {
        snprintf(gh.status_msg, sizeof(gh.status_msg), "AUTH FAILED");
        gh.view = 0;
    }

    gh.loading = false;
    return NULL;
}

/* ── render ────────────────────────────────────────────────────── */

void lumo_app_render_github(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    (void)ctx;
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);

    struct lumo_rect full = {0, 0, (int)width, (int)height};
    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full,
        theme.header_bg, theme.bg);

    int y = 16;
    int pad = 20;
    int col_w = (int)width - pad * 2;
    char buf[256];

    /* init on first render */
    if (!gh_initialized) {
        gh_initialized = true;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        gh_load_token();
        if (gh.authenticated && !gh.loading) {
            pthread_t tid;
            pthread_create(&tid, NULL, gh_fetch_thread, NULL);
            pthread_detach(tid);
        }
    }

    /* header */
    lumo_app_draw_text(pixels, width, height, pad, y, 2,
        theme.text_dim, "GITHUB");
    y += 20;
    lumo_app_draw_text(pixels, width, height, pad, y, 4,
        theme.accent, "LUMO GITHUB");
    y += 36;
    lumo_app_fill_rect(pixels, width, height, pad, y, col_w, 1,
        theme.separator);
    y += 12;

    /* loading indicator */
    if (gh.loading) {
        lumo_app_draw_text(pixels, width, height, pad, y, 2,
            theme.text_dim, gh.status_msg);
        return;
    }

    /* ── view 0: login / token entry ──────────────────────────── */
    if (gh.view == 0 || !gh.authenticated) {
        lumo_app_draw_text(pixels, width, height, pad, y, 3,
            theme.text, "SIGN IN");
        y += 28;

        lumo_app_draw_text(pixels, width, height, pad, y, 1,
            theme.text_dim,
            "CREATE A PERSONAL ACCESS TOKEN AT");
        y += 14;
        lumo_app_draw_text(pixels, width, height, pad, y, 1,
            theme.accent, "GITHUB.COM/SETTINGS/TOKENS");
        y += 14;
        lumo_app_draw_text(pixels, width, height, pad, y, 1,
            theme.text_dim,
            "SAVE IT TO ~/.lumo-github-token");
        y += 14;
        lumo_app_draw_text(pixels, width, height, pad, y, 1,
            theme.text_dim,
            "THEN REOPEN THIS APP");
        y += 24;

        if (gh.token[0] != '\0') {
            snprintf(buf, sizeof(buf), "TOKEN: %c%c%c%c...%c%c%c%c",
                gh.token[0], gh.token[1], gh.token[2], gh.token[3],
                gh.token[strlen(gh.token)-4], gh.token[strlen(gh.token)-3],
                gh.token[strlen(gh.token)-2], gh.token[strlen(gh.token)-1]);
            lumo_app_draw_text(pixels, width, height, pad, y, 1,
                theme.text_dim, buf);
            y += 14;
        }

        if (gh.status_msg[0] != '\0') {
            lumo_app_draw_text(pixels, width, height, pad, y, 2,
                lumo_app_argb(0xFF, 0xFF, 0x66, 0x66), gh.status_msg);
        }
        return;
    }

    /* ── view 1: profile + repos ──────────────────────────────── */
    if (gh.view == 1) {
        /* profile header */
        snprintf(buf, sizeof(buf), "@%s", gh.username);
        lumo_app_draw_text(pixels, width, height, pad, y, 3,
            theme.accent, buf);
        y += 26;
        snprintf(buf, sizeof(buf), "%d REPOS  %d FOLLOWERS",
            gh.public_repos, gh.followers);
        lumo_app_draw_text(pixels, width, height, pad, y, 1,
            theme.text_dim, buf);
        y += 16;
        lumo_app_fill_rect(pixels, width, height, pad, y, col_w, 1,
            theme.separator);
        y += 12;

        /* repo list */
        int row_h = 56;
        int scroll = gh.scroll_offset;
        int max_visible = ((int)height - y - 20) / row_h;
        if (max_visible < 1) max_visible = 1;

        for (int i = scroll; i < gh.repo_count && i < scroll + max_visible;
                i++) {
            struct gh_repo *r = &gh.repos[i];
            struct lumo_rect row = {pad, y, col_w, row_h - 4};

            lumo_app_fill_rounded_rect(pixels, width, height, &row,
                10, theme.card_bg);

            /* repo name */
            lumo_app_draw_text(pixels, width, height,
                row.x + 12, row.y + 8, 2, theme.text, r->name);

            /* description (truncated) */
            if (r->description[0] != '\0') {
                char desc[60];
                snprintf(desc, sizeof(desc), "%s", r->description);
                lumo_app_draw_text(pixels, width, height,
                    row.x + 12, row.y + 26, 1, theme.text_dim, desc);
            }

            /* language + stars + forks */
            snprintf(buf, sizeof(buf), "%s  * %d  Y %d",
                r->language[0] ? r->language : "—",
                r->stars, r->forks);
            lumo_app_draw_text(pixels, width, height,
                row.x + 12, row.y + 38, 1,
                theme.text_dim, buf);

            /* private badge */
            if (r->is_private) {
                lumo_app_draw_text(pixels, width, height,
                    row.x + row.width - 60, row.y + 8, 1,
                    lumo_app_argb(0xFF, 0xFF, 0xAA, 0x44), "PRIVATE");
            }

            y += row_h;
        }

        /* scroll indicator */
        if (gh.repo_count > max_visible) {
            snprintf(buf, sizeof(buf), "%d-%d / %d",
                scroll + 1,
                scroll + max_visible > gh.repo_count
                    ? gh.repo_count : scroll + max_visible,
                gh.repo_count);
            lumo_app_draw_text(pixels, width, height,
                (int)width - 120, (int)height - 20, 1,
                theme.text_dim, buf);
        }
        return;
    }
}

/* ── touch handling (called from app_client.c) ─────────────────── */

int lumo_app_github_button_at(
    uint32_t width, uint32_t height, double x, double y
) {
    int pad = 20;
    int header_h = 100;

    /* repo rows */
    if (gh.view == 1 && y > header_h) {
        int row_h = 56;
        int row = (int)(y - header_h) / row_h;
        return 100 + row + gh.scroll_offset;
    }

    /* login view — tap anywhere to retry */
    if (gh.view == 0) {
        return 0;
    }

    (void)width;
    (void)pad;
    return -1;
}

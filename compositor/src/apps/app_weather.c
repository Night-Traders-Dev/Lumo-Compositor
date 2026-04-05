/*
 * app_weather.c — Weather app for Lumo OS.
 * Fetches current conditions from wttr.in (same zip code as shell panel).
 * Updates every 5 minutes via background thread.
 */
#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* cached weather data */
static struct {
    char temp[16];
    char condition[64];
    char humidity[16];
    char wind[32];
    char feels_like[16];
    bool loaded;
    time_t last_fetch;
    bool fetching;
} weather_cache;

static size_t weather_write_cb(void *ptr, size_t size, size_t nmemb,
    void *userdata)
{
    char *buf = userdata;
    size_t total = size * nmemb;
    size_t cur = strlen(buf);
    if (cur + total >= 512) total = 511 - cur;
    memcpy(buf + cur, ptr, total);
    buf[cur + total] = '\0';
    return size * nmemb;
}

static void weather_parse(const char *data) {
    if (!data || !data[0]) return;

    /* format: "+72°F Sunny 45% 5mph" from wttr.in/?format=%t+%C+%h+%w&u */
    const char *p = data;
    while (*p == ' ') p++;

    /* temperature */
    int temp = 0;
    if (sscanf(p, "%d", &temp) >= 1)
        snprintf(weather_cache.temp, sizeof(weather_cache.temp), "%d", temp);

    /* skip past temperature and degree symbol */
    while (*p && *p != ' ' && *p != '\xc2') p++;
    if (*p == '\xc2') p += 2; /* skip °F/°C */
    while (*p == ' ') p++;

    /* condition (until next field that starts with digit or %) */
    {
        char cond[64] = "";
        int ci = 0;
        while (*p && ci < 62) {
            /* stop at humidity (digit followed by %) or wind (digit followed by letter) */
            if (*p >= '0' && *p <= '9') {
                /* peek ahead for % or mph/km */
                const char *q = p + 1;
                while (*q >= '0' && *q <= '9') q++;
                if (*q == '%' || *q == 'm' || *q == 'k') break;
            }
            cond[ci++] = *p++;
        }
        cond[ci] = '\0';
        /* trim trailing spaces */
        while (ci > 0 && cond[ci - 1] == ' ') cond[--ci] = '\0';
        if (ci > 0)
            snprintf(weather_cache.condition, sizeof(weather_cache.condition),
                "%s", cond);
    }

    /* humidity (NN%) */
    while (*p == ' ') p++;
    {
        int hum = 0;
        if (sscanf(p, "%d%%", &hum) >= 1)
            snprintf(weather_cache.humidity, sizeof(weather_cache.humidity),
                "%d%%", hum);
        while (*p && *p != ' ') p++;
    }

    /* wind */
    while (*p == ' ') p++;
    if (*p) {
        char wind[32] = "";
        int wi = 0;
        while (*p && *p != '\n' && *p != '\r' && wi < 30) {
            wind[wi++] = *p++;
        }
        wind[wi] = '\0';
        if (wi > 0)
            snprintf(weather_cache.wind, sizeof(weather_cache.wind),
                "%s", wind);
    }

    /* feels like = same as temp for now */
    snprintf(weather_cache.feels_like, sizeof(weather_cache.feels_like),
        "%s", weather_cache.temp);
    weather_cache.loaded = true;
    weather_cache.last_fetch = time(NULL);
}

static void *weather_fetch_thread(void *arg) {
    (void)arg;
    weather_cache.fetching = true;

    CURL *curl = curl_easy_init();
    if (!curl) {
        weather_cache.fetching = false;
        return NULL;
    }

    char buf[512] = "";
    curl_easy_setopt(curl, CURLOPT_URL,
        "https://wttr.in/41101?format=%t+%C+%h+%w&u");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, weather_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Lumo-OS/0.0.80");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && buf[0])
        weather_parse(buf);

    weather_cache.fetching = false;
    return NULL;
}

static void weather_ensure_fetched(void) {
    if (weather_cache.fetching) return;
    time_t now = time(NULL);
    if (!weather_cache.loaded || now - weather_cache.last_fetch > 300) {
        pthread_t tid;
        pthread_create(&tid, NULL, weather_fetch_thread, NULL);
        pthread_detach(tid);
    }
}

void lumo_app_render_weather(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);
    int w = (int)width, h = (int)height;
    (void)ctx;

    weather_ensure_fetched();

    lumo_app_draw_background(pixels, width, height);

    /* header */
    lumo_app_fill_rect(pixels, width, height, 0, 0, w, 48, theme.header_bg);
    lumo_app_draw_text(pixels, width, height, 16, 14, 3, theme.accent,
        "WEATHER");
    lumo_app_draw_text(pixels, width, height, w - 80, 18, 1,
        theme.text_dim, "41101");
    lumo_app_fill_rect(pixels, width, height, 8, 48, w - 16, 1,
        theme.separator);

    int y = 64;
    int pad = 16;

    if (!weather_cache.loaded) {
        lumo_app_draw_text(pixels, width, height, pad, y + 20, 2,
            theme.text_dim,
            weather_cache.fetching ? "FETCHING WEATHER..." : "NO DATA");
        return;
    }

    /* large temperature */
    {
        char display[32];
        snprintf(display, sizeof(display), "%sF", weather_cache.temp);
        int tw = (int)strlen(display) * 6 * 6;
        lumo_app_draw_text(pixels, width, height,
            (w - tw) / 2, y, 6, theme.text, display);
        y += 50;
    }

    /* condition */
    {
        const char *cond = weather_cache.condition[0]
            ? weather_cache.condition : "UNKNOWN";
        int tw = (int)strlen(cond) * 2 * 6;
        lumo_app_draw_text(pixels, width, height,
            (w - tw) / 2, y, 2, theme.text_dim, cond);
        y += 28;
    }

    lumo_app_fill_rect(pixels, width, height, pad, y, w - pad * 2, 1,
        theme.separator);
    y += 16;

    /* detail cards */
    int card_w = (w - pad * 3) / 2;
    int card_h = 80;

    /* humidity */
    {
        struct lumo_rect card = {pad, y, card_w, card_h};
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 12,
            theme.card_bg);
        lumo_app_draw_text(pixels, width, height,
            pad + 12, y + 8, 1, theme.text_dim, "HUMIDITY");
        lumo_app_draw_text(pixels, width, height,
            pad + 12, y + 28, 3, theme.text,
            weather_cache.humidity[0] ? weather_cache.humidity : "--");
    }

    /* wind */
    {
        struct lumo_rect card = {pad * 2 + card_w, y, card_w, card_h};
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 12,
            theme.card_bg);
        lumo_app_draw_text(pixels, width, height,
            pad * 2 + card_w + 12, y + 8, 1, theme.text_dim, "WIND");
        lumo_app_draw_text(pixels, width, height,
            pad * 2 + card_w + 12, y + 28, 3, theme.text,
            weather_cache.wind[0] ? weather_cache.wind : "--");
    }
    y += card_h + 12;

    /* feels like + UV */
    {
        struct lumo_rect card = {pad, y, card_w, card_h};
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 12,
            theme.card_bg);
        lumo_app_draw_text(pixels, width, height,
            pad + 12, y + 8, 1, theme.text_dim, "FEELS LIKE");
        char fl[16];
        snprintf(fl, sizeof(fl), "%sF", weather_cache.feels_like);
        lumo_app_draw_text(pixels, width, height,
            pad + 12, y + 28, 3, theme.text, fl);
    }
    {
        struct lumo_rect card = {pad * 2 + card_w, y, card_w, card_h};
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 12,
            theme.card_bg);
        lumo_app_draw_text(pixels, width, height,
            pad * 2 + card_w + 12, y + 8, 1, theme.text_dim, "UV INDEX");
        lumo_app_draw_text(pixels, width, height,
            pad * 2 + card_w + 12, y + 28, 3, theme.text, "LOW");
    }
    y += card_h + 16;

    /* last updated */
    {
        struct tm *tm = localtime(&weather_cache.last_fetch);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "UPDATED %H:%M", tm);
        lumo_app_draw_text(pixels, width, height,
            pad, y, 1, theme.text_dim, time_str);
        y += 16;
        lumo_app_draw_text(pixels, width, height,
            pad, y, 1, theme.text_dim, "SOURCE: WTTR.IN");
    }
    (void)h;
}

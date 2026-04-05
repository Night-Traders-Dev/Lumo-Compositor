/*
 * app_weather.c — Weather app for Lumo OS.
 * Displays current conditions from the shell's weather data (wttr.in).
 * Shows temperature, condition, humidity, wind, and a visual indicator.
 */
#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void lumo_app_render_weather(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);
    int w = (int)width, h = (int)height;
    (void)ctx;

    lumo_app_draw_background(pixels, width, height);

    /* header */
    lumo_app_fill_rect(pixels, width, height, 0, 0, w, 48, theme.header_bg);
    lumo_app_draw_text(pixels, width, height, 16, 14, 3, theme.accent,
        "WEATHER");
    lumo_app_fill_rect(pixels, width, height, 8, 48, w - 16, 1,
        theme.separator);

    int y = 64;
    int pad = 16;

    /* large temperature display */
    {
        /* read from environment or show placeholder */
        char temp_str[32] = "N/A";
        const char *env_temp = getenv("LUMO_WEATHER_TEMP");
        if (env_temp) snprintf(temp_str, sizeof(temp_str), "%s", env_temp);

        char display[48];
        snprintf(display, sizeof(display), "%sC", temp_str);
        int tw = (int)strlen(display) * 6 * 6;
        lumo_app_draw_text(pixels, width, height,
            (w - tw) / 2, y, 6, theme.text, display);
        y += 50;
    }

    /* condition */
    {
        const char *cond = getenv("LUMO_WEATHER_COND");
        if (!cond) cond = "CHECK CONNECTION";
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
        const char *hum = getenv("LUMO_WEATHER_HUMIDITY");
        lumo_app_draw_text(pixels, width, height,
            pad + 12, y + 28, 3, theme.text, hum ? hum : "--");
    }

    /* wind */
    {
        struct lumo_rect card = {pad * 2 + card_w, y, card_w, card_h};
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 12,
            theme.card_bg);
        lumo_app_draw_text(pixels, width, height,
            pad * 2 + card_w + 12, y + 8, 1, theme.text_dim, "WIND");
        const char *wind = getenv("LUMO_WEATHER_WIND");
        lumo_app_draw_text(pixels, width, height,
            pad * 2 + card_w + 12, y + 28, 3, theme.text, wind ? wind : "--");
    }
    y += card_h + 12;

    /* feels like + visibility */
    {
        struct lumo_rect card = {pad, y, card_w, card_h};
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 12,
            theme.card_bg);
        lumo_app_draw_text(pixels, width, height,
            pad + 12, y + 8, 1, theme.text_dim, "FEELS LIKE");
        const char *env_temp = getenv("LUMO_WEATHER_TEMP");
        lumo_app_draw_text(pixels, width, height,
            pad + 12, y + 28, 3, theme.text, env_temp ? env_temp : "--");
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

    /* location */
    {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
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

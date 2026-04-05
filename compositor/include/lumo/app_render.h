#ifndef LUMO_APP_RENDER_H
#define LUMO_APP_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lumo/app.h"

uint32_t lumo_app_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b);
void lumo_app_fill_rect(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int rect_width, int rect_height, uint32_t color
);
void lumo_app_fill_gradient(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, uint32_t top_color, uint32_t bottom_color
);
void lumo_app_fill_rounded_rect(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, uint32_t radius, uint32_t color
);
void lumo_app_draw_outline(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, int thickness, uint32_t color
);
void lumo_app_draw_text(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int scale, uint32_t color, const char *text
);
void lumo_app_draw_text_centered(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, int scale, uint32_t color, const char *text
);
void lumo_app_draw_close_button(
    uint32_t *pixels, uint32_t width, uint32_t height,
    bool close_active
);
void lumo_app_draw_background(
    uint32_t *pixels, uint32_t width, uint32_t height
);

/* dynamic theme colors based on time of day */
struct lumo_app_theme {
    uint32_t bg;          /* app background */
    uint32_t header_bg;   /* header/toolbar background */
    uint32_t card_bg;     /* card/container fill */
    uint32_t card_stroke; /* card border */
    uint32_t accent;      /* always orange */
    uint32_t text;        /* primary text (white) */
    uint32_t text_dim;    /* secondary text */
    uint32_t separator;   /* dividers */
};
void lumo_app_theme_get(struct lumo_app_theme *theme);

void lumo_app_render_music(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_photos(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_videos(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_clock(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_files(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_settings(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_notes(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_terminal(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_phone(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_camera(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_maps(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_browser(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_github(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_sysmon(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_stub(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_calculator(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_calendar(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_weather(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_contacts(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_recorder(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_tasks(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_downloads(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_package(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);
void lumo_app_render_syslog(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);

#endif

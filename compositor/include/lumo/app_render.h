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
void lumo_app_render_stub(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
);

#endif

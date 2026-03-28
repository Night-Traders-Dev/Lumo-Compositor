#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void lumo_app_render_terminal(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);

    uint32_t bg = theme.bg;
    uint32_t text_color = theme.text;
    uint32_t prompt_color = theme.accent;
    uint32_t dim = theme.separator;
    uint32_t cursor_color = theme.accent;
    uint32_t header_bg = theme.header_bg;
    int line_h = 18;
    int y = 48;
    int line_count = ctx != NULL ? ctx->term_line_count : 0;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, (int)height, bg);

    /* header bar */
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, 38, header_bg);
    lumo_app_draw_text(pixels, width, height, 16, 12, 2, prompt_color,
        "LUMO TERMINAL");
    lumo_app_fill_rect(pixels, width, height, 12, 38, (int)width - 24, 1, dim);

    /* scrollback output lines */
    for (int i = 0; i < line_count && i < 16; i++) {
        const char *line = ctx != NULL ? ctx->term_lines[i] : "";
        if (y + line_h > (int)height - 40) break;
        lumo_app_draw_text(pixels, width, height, 12, y, 2, text_color, line);
        y += line_h;
    }

    /* cursor blink: visible for 500ms, hidden for 500ms */
    bool cursor_visible;
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        cursor_visible = (ts.tv_nsec / 500000000) == 0;
    }

    /* current line (prompt or partial output from PTY) */
    if (ctx != NULL && ctx->term_input_len > 0 &&
            y + line_h <= (int)height - 20) {
        lumo_app_draw_text(pixels, width, height, 12, y, 2, prompt_color,
            ctx->term_input);

        if (cursor_visible) {
            int pw = ctx->term_input_len * 2 * 6;
            lumo_app_fill_rect(pixels, width, height, 12 + pw + 2, y,
                2, 14, cursor_color);
        }
    } else if (y + line_h <= (int)height - 20 && cursor_visible) {
        lumo_app_fill_rect(pixels, width, height, 12, y, 2, 14, cursor_color);
    }
}

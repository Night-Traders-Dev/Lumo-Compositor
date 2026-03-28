#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void lumo_app_render_terminal(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    bool close_active = ctx != NULL ? ctx->close_active : false;
    uint32_t bg = lumo_app_argb(0xFF, 0x2C, 0x00, 0x1E);
    uint32_t text_color = lumo_app_argb(0xFF, 0xE8, 0xE3, 0xDF);
    uint32_t prompt_color = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t dim = lumo_app_argb(0xFF, 0x77, 0x21, 0x6F);
    uint32_t cursor_color = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t header_bg = lumo_app_argb(0xFF, 0x1A, 0x00, 0x14);
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

    /* current line (prompt or partial output from PTY) */
    if (ctx != NULL && ctx->term_input_len > 0 &&
            y + line_h <= (int)height - 20) {
        lumo_app_draw_text(pixels, width, height, 12, y, 2, prompt_color,
            ctx->term_input);

        /* blinking cursor */
        {
            int pw = ctx->term_input_len * 2 * 6;
            lumo_app_fill_rect(pixels, width, height, 12 + pw + 2, y,
                2, 14, cursor_color);
        }
    } else if (y + line_h <= (int)height - 20) {
        /* no current line - just show cursor */
        lumo_app_fill_rect(pixels, width, height, 12, y, 2, 14, cursor_color);
    }

    lumo_app_draw_close_button(pixels, width, height, close_active);
}

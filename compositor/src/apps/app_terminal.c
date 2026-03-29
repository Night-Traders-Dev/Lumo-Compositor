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
    bool menu_open = ctx != NULL ? ctx->term_menu_open : false;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, (int)height, bg);

    /* header bar */
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, 38, header_bg);
    lumo_app_draw_text(pixels, width, height, 16, 12, 2, prompt_color,
        "LUMO TERMINAL");

    /* menu indicator */
    lumo_app_draw_text(pixels, width, height, (int)width - 40, 12, 2,
        theme.text_dim, "...");

    lumo_app_fill_rect(pixels, width, height, 12, 38, (int)width - 24, 1, dim);

    /* dropdown menu */
    if (menu_open) {
        struct lumo_rect menu_bg = {8, 40, 176, 84};
        uint32_t menu_fill = lumo_app_argb(0xF0, 0x30, 0x30, 0x34);
        uint32_t menu_hl = theme.accent;
        lumo_app_fill_rounded_rect(pixels, width, height, &menu_bg, 10,
            menu_fill);
        lumo_app_draw_outline(pixels, width, height, &menu_bg, 1,
            theme.card_stroke);

        lumo_app_draw_text(pixels, width, height, 20, 48, 2,
            theme.text, "NEW");
        lumo_app_draw_text(pixels, width, height, 20, 68, 2,
            menu_hl, "KEYBOARD");
        lumo_app_draw_text(pixels, width, height, 20, 88, 2,
            theme.text, "SETTINGS");
        lumo_app_draw_text(pixels, width, height, 20, 108, 2,
            theme.text_dim, "ABOUT");
    }

    /* scrollback output lines */
    if (!menu_open) {
        for (int i = 0; i < line_count && i < 16; i++) {
            const char *line = ctx != NULL ? ctx->term_lines[i] : "";
            if (y + line_h > (int)height - 40) break;
            lumo_app_draw_text(pixels, width, height, 12, y, 2,
                text_color, line);
            y += line_h;
        }

        /* cursor blink: visible for 500ms, hidden for 500ms */
        bool cursor_visible;
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            cursor_visible = (ts.tv_nsec / 500000000) == 0;
        }

        /* current line */
        if (ctx != NULL && ctx->term_input_len > 0 &&
                y + line_h <= (int)height - 20) {
            lumo_app_draw_text(pixels, width, height, 12, y, 2,
                prompt_color, ctx->term_input);
            if (cursor_visible) {
                int pw = ctx->term_input_len * 2 * 6;
                lumo_app_fill_rect(pixels, width, height, 12 + pw + 2, y,
                    2, 14, cursor_color);
            }
        } else if (y + line_h <= (int)height - 20 && cursor_visible) {
            lumo_app_fill_rect(pixels, width, height, 12, y, 2, 14,
                cursor_color);
        }
    }
}

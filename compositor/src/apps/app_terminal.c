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

    /* centered fullscreen menu overlay */
    if (menu_open) {
        /* dim background — halve each RGB channel using bit mask.
         * (c >> 1) & 0x7F7F7F7F clears the top bit of each byte
         * and shifts down, equivalent to dividing each channel by 2.
         * Then we force alpha back to 0xFF. */
        for (uint32_t py = 38; py < height; py++) {
            uint32_t *row = pixels + py * width;
            for (uint32_t px = 0; px < width; px++) {
                row[px] = ((row[px] >> 1) & 0x007F7F7F) | 0xFF000000;
            }
        }

        int menu_w = (int)width * 2 / 3;
        int menu_h = 220;
        if (menu_w < 240) menu_w = 240;
        int menu_x = ((int)width - menu_w) / 2;
        int menu_y = ((int)height - menu_h) / 2;
        int item_h = 44;
        int pad = 24;

        struct lumo_rect menu_bg_rect = {menu_x, menu_y, menu_w, menu_h};
        uint32_t menu_fill = lumo_app_argb(0xF4, 0x28, 0x28, 0x2E);
        lumo_app_fill_rounded_rect(pixels, width, height, &menu_bg_rect,
            18, menu_fill);
        lumo_app_draw_outline(pixels, width, height, &menu_bg_rect, 1,
            theme.card_stroke);

        /* title */
        lumo_app_draw_text(pixels, width, height,
            menu_x + pad, menu_y + 16, 2, theme.text_dim, "TERMINAL MENU");

        /* separator */
        lumo_app_fill_rect(pixels, width, height,
            menu_x + pad, menu_y + 36, menu_w - pad * 2, 1,
            theme.separator);

        /* menu items — large scale 3 */
        int iy = menu_y + 46;
        lumo_app_draw_text(pixels, width, height,
            menu_x + pad, iy, 3, theme.text, "NEW");
        iy += item_h;
        lumo_app_draw_text(pixels, width, height,
            menu_x + pad, iy, 3, theme.accent, "KEYBOARD");
        iy += item_h;
        lumo_app_draw_text(pixels, width, height,
            menu_x + pad, iy, 3, theme.text, "SETTINGS");
        iy += item_h;
        lumo_app_draw_text(pixels, width, height,
            menu_x + pad, iy, 3, theme.text_dim, "ABOUT");
    }

    /* scrollback output lines */
    if (!menu_open) {
        int prompt_y = (int)height - 40;

        for (int i = 0; i < line_count && i < 16; i++) {
            const char *line = ctx != NULL ? ctx->term_lines[i] : "";
            if (y + line_h > prompt_y - line_h) break;
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

        /* current line — always at the bottom */
        if (ctx != NULL && ctx->term_input_len > 0) {
            lumo_app_draw_text(pixels, width, height, 12, prompt_y, 2,
                prompt_color, ctx->term_input);
            if (cursor_visible) {
                int pw = ctx->term_input_len * 2 * 6;
                lumo_app_fill_rect(pixels, width, height, 12 + pw + 2,
                    prompt_y, 2, 14, cursor_color);
            }
        } else if (cursor_visible) {
            lumo_app_fill_rect(pixels, width, height, 12, prompt_y, 2, 14,
                cursor_color);
        }
    }
}

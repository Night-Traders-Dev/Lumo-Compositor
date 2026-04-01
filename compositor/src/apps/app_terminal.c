#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* helper: calculate how many visual lines a string takes with wrapping */
static int lumo_term_wrapped_lines(const char *text, int chars_per_line) {
    if (text == NULL || text[0] == '\0' || chars_per_line <= 0)
        return 1;
    int len = (int)strlen(text);
    return (len + chars_per_line - 1) / chars_per_line;
}

/* helper: draw a wrapped line, returns number of visual lines used */
static int lumo_term_draw_wrapped(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int scale, uint32_t color,
    const char *text, int chars_per_line, int line_h
) {
    if (text == NULL || text[0] == '\0') return 1;
    int len = (int)strlen(text);
    int lines = 0;
    int offset = 0;

    while (offset < len) {
        int chunk = len - offset;
        if (chunk > chars_per_line) chunk = chars_per_line;
        char buf[128];
        if (chunk > (int)sizeof(buf) - 1) chunk = (int)sizeof(buf) - 1;
        memcpy(buf, text + offset, (size_t)chunk);
        buf[chunk] = '\0';
        lumo_app_draw_text(pixels, width, height, x, y + lines * line_h,
            scale, color, buf);
        lines++;
        offset += chunk;
    }
    return lines > 0 ? lines : 1;
}

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

    int scale = 2;
    int char_w = scale * 6; /* bitmap font: 6px per char at scale 1 */
    int line_h = scale * 6 + 6; /* line height with spacing */
    int margin = 12;
    int header_h = 38;
    int prompt_margin = 8;

    int content_width = (int)width - margin * 2;
    int chars_per_line = content_width / char_w;
    if (chars_per_line < 10) chars_per_line = 10;

    int line_count = ctx != NULL ? ctx->term_line_count : 0;
    bool menu_open = ctx != NULL ? ctx->term_menu_open : false;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, (int)height, bg);

    /* header bar */
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, header_h,
        header_bg);
    lumo_app_draw_text(pixels, width, height, 16, 12, 2, prompt_color,
        "LUMO TERMINAL");
    lumo_app_draw_text(pixels, width, height, (int)width - 40, 12, 2,
        theme.text_dim, "...");
    lumo_app_fill_rect(pixels, width, height, margin, header_h,
        (int)width - margin * 2, 1, dim);

    if (menu_open) {
        /* dim background */
        for (uint32_t py = (uint32_t)header_h; py < height; py++) {
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
        lumo_app_fill_rounded_rect(pixels, width, height, &menu_bg_rect, 18,
            lumo_app_argb(0xF4, 0x28, 0x28, 0x2E));
        lumo_app_draw_outline(pixels, width, height, &menu_bg_rect, 1,
            theme.card_stroke);
        lumo_app_draw_text(pixels, width, height,
            menu_x + pad, menu_y + 16, 2, theme.text_dim, "TERMINAL MENU");
        lumo_app_fill_rect(pixels, width, height,
            menu_x + pad, menu_y + 36, menu_w - pad * 2, 1, theme.separator);
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
        return;
    }

    /* ── terminal content: top-down layout ──
     * Scrollback lines render top-down from below the header.
     * The prompt and cursor follow immediately after the last
     * output line, scrolling upward as output accumulates. */

    int y = header_h + 4;
    int area_bottom = (int)height - margin;

    /* 1. Draw scrollback lines top-down */
    for (int i = 0; i < line_count && i < 16; i++) {
        const char *line = ctx != NULL ? ctx->term_lines[i] : "";
        int wrapped = lumo_term_wrapped_lines(line, chars_per_line);
        int block_h = wrapped * line_h;

        if (y + block_h > area_bottom) break;
        lumo_term_draw_wrapped(pixels, width, height, margin, y, scale,
            text_color, line, chars_per_line, line_h);
        y += block_h;
    }

    /* 2. Draw prompt + cursor right after the last output line */
    bool cursor_visible;
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        cursor_visible = (ts.tv_nsec / 500000000) == 0;
    }

    if (y + line_h <= area_bottom) {
        if (ctx != NULL && ctx->term_input_len > 0) {
            int input_lines = lumo_term_draw_wrapped(pixels, width, height,
                margin, y, scale, prompt_color, ctx->term_input,
                chars_per_line, line_h);
            if (cursor_visible) {
                int cursor_x = (ctx->term_input_len % chars_per_line) * char_w;
                int cursor_line = ctx->term_input_len / chars_per_line;
                lumo_app_fill_rect(pixels, width, height,
                    margin + cursor_x + 2, y + cursor_line * line_h,
                    2, line_h - 4, cursor_color);
            }
            (void)input_lines;
        } else if (cursor_visible) {
            lumo_app_fill_rect(pixels, width, height, margin, y,
                2, line_h - 4, cursor_color);
        }
    }
}

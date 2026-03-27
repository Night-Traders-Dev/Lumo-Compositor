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
    uint32_t bg = lumo_app_argb(0xFF, 0x1A, 0x00, 0x14);
    uint32_t text_color = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t input_color = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t dim = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    uint32_t cursor_color = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    int line_h = 18;
    int y = 48;
    char prompt[96];
    const char *user = getenv("USER");
    char hostname[32] = "lumo";
    int line_count = ctx != NULL ? ctx->term_line_count : 0;

    gethostname(hostname, sizeof(hostname) - 1);

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, (int)height, bg);

    lumo_app_draw_text(pixels, width, height, 16, 12, 2, dim, "LUMO TERMINAL");
    lumo_app_draw_text(pixels, width, height, (int)width / 2, 12, 2, dim,
        "USE OSK TO TYPE");
    lumo_app_fill_rect(pixels, width, height, 12, 36, (int)width - 24, 1, dim);

    for (int i = 0; i < line_count && i < 16; i++) {
        const char *line = ctx != NULL ? ctx->term_lines[i] : "";
        if (y + line_h > (int)height - 40) break;
        lumo_app_draw_text(pixels, width, height, 16, y, 2, text_color, line);
        y += line_h;
    }

    snprintf(prompt, sizeof(prompt), "%s@%s:~$ %s",
        user != NULL ? user : "user", hostname,
        ctx != NULL ? ctx->term_input : "");

    if (y + line_h <= (int)height - 20) {
        lumo_app_draw_text(pixels, width, height, 16, y, 2, input_color, prompt);

        {
            int pw = (int)strlen(prompt) * 2 * 6;
            lumo_app_fill_rect(pixels, width, height, 16 + pw + 2, y,
                2, 14, cursor_color);
        }
    }

    lumo_app_draw_close_button(pixels, width, height, close_active);
}

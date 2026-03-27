#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TERM_MAX_LINES 32
#define TERM_LINE_LEN 80

void lumo_app_render_terminal(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    bool close_active = ctx != NULL ? ctx->close_active : false;
    uint32_t bg = lumo_app_argb(0xFF, 0x1A, 0x00, 0x14);
    uint32_t text_color = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    uint32_t cursor_color = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t dim = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    int line_h = 18;
    int y_start = 60;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, (int)height, bg);

    lumo_app_draw_text(pixels, width, height, 16, 16, 2, dim, "LUMO TERMINAL");
    lumo_app_draw_text(pixels, width, height, 16, 36, 2, text_color,
        "TAP TO FOCUS  /  TYPE WITH OSK");

    lumo_app_fill_rect(pixels, width, height, 12, y_start - 4,
        (int)width - 24, 1, dim);

    {
        char prompt[96];
        const char *user = getenv("USER");
        char hostname[32] = "lumo";
        gethostname(hostname, sizeof(hostname) - 1);

        snprintf(prompt, sizeof(prompt), "%s@%s:~$",
            user != NULL ? user : "user", hostname);
        lumo_app_draw_text(pixels, width, height, 16, y_start, 2,
            text_color, prompt);

        {
            int pw = (int)strlen(prompt) * 2 * 6;
            lumo_app_fill_rect(pixels, width, height, 16 + pw + 4, y_start,
                2, 14, cursor_color);
        }
    }

    {
        int info_y = y_start + line_h * 2;
        lumo_app_draw_text(pixels, width, height, 16, info_y, 2,
            dim, "KEYBOARD INPUT WILL APPEAR HERE");
        info_y += line_h;
        lumo_app_draw_text(pixels, width, height, 16, info_y, 2,
            dim, "TAP THE SCREEN TO ACTIVATE OSK");
        info_y += line_h * 2;
        lumo_app_draw_text(pixels, width, height, 16, info_y, 2,
            dim, "THIS IS A PLACEHOLDER TERMINAL");
        info_y += line_h;
        lumo_app_draw_text(pixels, width, height, 16, info_y, 2,
            dim, "FULL PTY SUPPORT COMING SOON");
    }

    lumo_app_draw_close_button(pixels, width, height, close_active);
}

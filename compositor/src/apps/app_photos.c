#define _DEFAULT_SOURCE
#include "lumo/app_render.h"
#include <stdio.h>
#include <string.h>

void lumo_app_render_photos(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);

    int count = ctx != NULL ? ctx->media_file_count : 0;
    int selected = ctx != NULL ? ctx->media_selected : -1;
    int scroll = ctx != NULL ? ctx->scroll_offset : 0;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_draw_background(pixels, width, height);

    /* header */
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, 48,
        theme.header_bg);
    lumo_app_draw_text(pixels, width, height, 16, 16, 3, theme.accent,
        "PHOTOS");
    lumo_app_fill_rect(pixels, width, height, 12, 48, (int)width - 24, 1,
        theme.separator);

    if (count == 0) {
        lumo_app_draw_text(pixels, width, height, 16, 80, 2,
            theme.text_dim, "NO IMAGES FOUND");
        lumo_app_draw_text(pixels, width, height, 16, 102, 2,
            theme.text_dim, "PLACE .JPG OR .PNG IN ~/PICTURES");
        return;
    }

    /* photo grid - 3 columns */
    int cols = 3;
    int pad = 8;
    int cell_w = ((int)width - pad * (cols + 1)) / cols;
    int cell_h = cell_w * 3 / 4; /* 4:3 aspect */
    int grid_y = 56;

    for (int i = scroll; i < count; i++) {
        int col = (i - scroll) % cols;
        int row = (i - scroll) / cols;
        int cx = pad + col * (cell_w + pad);
        int cy = grid_y + row * (cell_h + pad);

        if (cy + cell_h > (int)height) break;

        bool is_sel = (i == selected);
        struct lumo_rect cell = {cx, cy, cell_w, cell_h};

        /* thumbnail placeholder — colored rect based on filename hash */
        uint32_t hash = 0;
        for (int j = 0; ctx->media_files[i][j]; j++)
            hash = hash * 31 + (uint32_t)ctx->media_files[i][j];
        uint32_t thumb_r = 0x20 + (hash & 0x3F);
        uint32_t thumb_g = 0x18 + ((hash >> 6) & 0x3F);
        uint32_t thumb_b = 0x28 + ((hash >> 12) & 0x3F);
        uint32_t thumb_color = lumo_app_argb(0xFF,
            (uint8_t)thumb_r, (uint8_t)thumb_g, (uint8_t)thumb_b);

        lumo_app_fill_rounded_rect(pixels, width, height, &cell, 10,
            thumb_color);

        if (is_sel) {
            lumo_app_draw_outline(pixels, width, height, &cell, 2,
                theme.accent);
        }

        /* filename label at bottom of cell */
        char label[20];
        strncpy(label, ctx->media_files[i], sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        if (strlen(label) > 14) {
            label[12] = '.';
            label[13] = '.';
            label[14] = '\0';
        }
        lumo_app_draw_text(pixels, width, height, cx + 4,
            cy + cell_h - 16, 1, theme.text, label);
    }

    /* selected image info */
    if (selected >= 0 && selected < count) {
        struct lumo_rect info = {8, (int)height - 36, (int)width - 16, 28};
        lumo_app_fill_rounded_rect(pixels, width, height, &info, 10,
            theme.card_bg);
        lumo_app_draw_text(pixels, width, height, 20,
            (int)height - 28, 2, theme.text,
            ctx->media_files[selected]);
    }
}

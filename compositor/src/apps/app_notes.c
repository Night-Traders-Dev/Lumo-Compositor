#include "lumo/app_render.h"
#include <stdio.h>

int lumo_app_notes_row_at(
    uint32_t width, uint32_t height, double x, double y
) {
    int header_y = 130;
    int row_h = 44;
    int max_rows = ((int)height - header_y - 80) / row_h;
    (void)width;
    if (x < 28.0 || x > (double)width - 28.0) return -1;
    if (y >= (double)((int)height - 60) && y < (double)height - 20) return -2;
    if (y < header_y) return -1;
    int idx = (int)(y - header_y) / row_h;
    if (idx < 0 || idx >= max_rows) return -1;
    return idx;
}

void lumo_app_render_notes(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    bool close_active = ctx != NULL ? ctx->close_active : false;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    int note_count = ctx != NULL ? ctx->note_count : 0;
    uint32_t bg_top = lumo_app_argb(0xFF, 0x2C, 0x00, 0x1E);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x1D, 0x11, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    uint32_t accent = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    int row_y;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    lumo_app_draw_text(pixels, width, height, 28, 28, 2,
        text_secondary, "NOTES");
    lumo_app_draw_text(pixels, width, height, 28, 60, 4, text_primary,
        "Notes");
    lumo_app_draw_text(pixels, width, height, 28, 108, 2, text_secondary,
        "TAP A NOTE TO SELECT  /  TAP + TO ADD");

    row_y = 130;
    for (int i = 0; i < note_count && i < 8; i++) {
        struct lumo_rect row = {28, row_y, (int)width - 56, 40};
        bool is_sel = (selected == i);
        const char *text = (ctx != NULL && ctx->notes[i][0] != '\0')
            ? ctx->notes[i] : "EMPTY NOTE";

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 10,
            is_sel ? lumo_app_argb(0xFF, 0x3B, 0x1F, 0x34) : panel_fill);
        if (is_sel)
            lumo_app_draw_outline(pixels, width, height, &row, 1, accent);
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 12,
            2, is_sel ? text_primary : text_secondary, text);
        row_y += 44;
    }

    if (note_count == 0)
        lumo_app_draw_text(pixels, width, height, 28, row_y + 20, 2,
            text_secondary, "NO NOTES YET");

    {
        struct lumo_rect add_btn = {28, (int)height - 60, (int)width - 56, 36};
        lumo_app_fill_rounded_rect(pixels, width, height, &add_btn, 12, accent);
        lumo_app_draw_text_centered(pixels, width, height, &add_btn, 2,
            text_primary, "+ ADD NOTE");
    }

    /* close button removed — use bottom-edge swipe */
}

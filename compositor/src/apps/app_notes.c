#include "lumo/app_render.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

int lumo_app_notes_row_at(
    uint32_t width, uint32_t height, double x, double y
) {
    int header_y = 96;
    int row_h = 48;
    int max_rows = ((int)height - header_y - 70) / row_h;
    (void)width;
    /* add button at bottom */
    if (y >= (double)((int)height - 60) && y < (double)height - 20) return -2;
    if (x < 16.0 || x > (double)width - 16.0) return -1;
    if (y < header_y) return -1;
    int idx = (int)(y - header_y) / row_h;
    if (idx < 0 || idx >= max_rows) return -1;
    return idx;
}

void lumo_app_render_notes(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_app_theme theme;
    lumo_app_theme_get(&theme);

    int selected = ctx != NULL ? ctx->selected_row : -1;
    int editing = ctx != NULL ? ctx->note_editing : -1;
    int note_count = ctx != NULL ? ctx->note_count : 0;
    int row_y;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_draw_background(pixels, width, height);

    /* header */
    lumo_app_fill_rect(pixels, width, height, 0, 0, (int)width, 48,
        theme.header_bg);
    lumo_app_draw_text(pixels, width, height, 16, 16, 3, theme.accent,
        "NOTES");
    lumo_app_fill_rect(pixels, width, height, 0, 47, (int)width, 1,
        theme.separator);

    /* instructions */
    if (editing >= 0) {
        lumo_app_draw_text(pixels, width, height, 16, 60, 2,
            theme.accent, "EDITING - TAP KEYS ON KEYBOARD");
    } else {
        lumo_app_draw_text(pixels, width, height, 16, 60, 2,
            theme.text_dim, "TAP TO SELECT / TAP AGAIN TO EDIT");
    }
    lumo_app_fill_rect(pixels, width, height, 12, 88, (int)width - 24, 1,
        theme.separator);

    /* note list */
    row_y = 96;
    for (int i = 0; i < note_count && i < 8; i++) {
        int row_h = 48;
        struct lumo_rect row = {12, row_y, (int)width - 24, row_h - 4};
        bool is_sel = (selected == i);
        bool is_edit = (editing == i);
        const char *text = (ctx != NULL && ctx->notes[i][0] != '\0')
            ? ctx->notes[i] : "EMPTY NOTE";

        uint32_t fill = is_edit ? theme.card_bg :
            is_sel ? theme.card_bg : theme.bg;
        lumo_app_fill_rounded_rect(pixels, width, height, &row, 12, fill);

        if (is_edit) {
            lumo_app_draw_outline(pixels, width, height, &row, 2,
                theme.accent);
        } else if (is_sel) {
            lumo_app_draw_outline(pixels, width, height, &row, 1,
                theme.card_stroke);
        }

        /* note number */
        char num[4];
        snprintf(num, sizeof(num), "%d", i + 1);
        lumo_app_draw_text(pixels, width, height, row.x + 12, row.y + 14,
            2, is_edit ? theme.accent : theme.text_dim, num);

        /* note text */
        lumo_app_draw_text(pixels, width, height, row.x + 36, row.y + 14,
            2, is_edit ? theme.text : (is_sel ? theme.text : theme.text_dim),
            text);

        /* blinking cursor for editing note */
        if (is_edit) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            if ((ts.tv_nsec / 500000000) == 0) {
                int tw = (int)strlen(text) * 2 * 6;
                lumo_app_fill_rect(pixels, width, height,
                    row.x + 36 + tw + 2, row.y + 12, 2, 18, theme.accent);
            }
        }

        row_y += row_h;
    }

    if (note_count == 0) {
        lumo_app_draw_text(pixels, width, height, 16, row_y + 20, 2,
            theme.text_dim, "NO NOTES YET");
        lumo_app_draw_text(pixels, width, height, 16, row_y + 42, 2,
            theme.text_dim, "TAP + TO CREATE ONE");
    }

    /* add button */
    {
        struct lumo_rect add_btn = {12, (int)height - 56, (int)width - 24, 40};
        lumo_app_fill_rounded_rect(pixels, width, height, &add_btn, 14,
            theme.accent);
        lumo_app_draw_text_centered(pixels, width, height, &add_btn, 2,
            theme.text, "+ ADD NOTE");
    }
}

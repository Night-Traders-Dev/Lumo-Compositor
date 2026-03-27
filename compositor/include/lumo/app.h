#ifndef LUMO_APP_H
#define LUMO_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lumo/shell.h"

enum lumo_app_id {
    LUMO_APP_PHONE = 0,
    LUMO_APP_MESSAGES,
    LUMO_APP_BROWSER,
    LUMO_APP_CAMERA,
    LUMO_APP_MAPS,
    LUMO_APP_MUSIC,
    LUMO_APP_PHOTOS,
    LUMO_APP_VIDEOS,
    LUMO_APP_CLOCK,
    LUMO_APP_NOTES,
    LUMO_APP_FILES,
    LUMO_APP_SETTINGS,
};

size_t lumo_app_count(void);
const char *lumo_app_id_name(enum lumo_app_id app_id);
bool lumo_app_id_parse(const char *value, enum lumo_app_id *app_id);
bool lumo_app_id_for_launcher_tile(uint32_t tile_index, enum lumo_app_id *app_id);
const char *lumo_app_title(enum lumo_app_id app_id);
const char *lumo_app_subtitle(enum lumo_app_id app_id);
uint32_t lumo_app_accent_argb(enum lumo_app_id app_id);
bool lumo_app_close_rect(
    uint32_t width,
    uint32_t height,
    struct lumo_rect *rect
);
struct lumo_app_render_context {
    enum lumo_app_id app_id;
    bool close_active;
    const char *browse_path;
    int scroll_offset;
    bool stopwatch_running;
    uint64_t stopwatch_elapsed_ms;
    int selected_row;
    char notes[8][128];
    int note_count;
    int note_editing;
    char term_lines[16][82];
    int term_line_count;
    char term_input[82];
    int term_input_len;
};

void lumo_app_render(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
);
int lumo_app_files_entry_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
);
int lumo_app_settings_row_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
);
int lumo_app_clock_card_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
);
int lumo_app_notes_row_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
);

#endif

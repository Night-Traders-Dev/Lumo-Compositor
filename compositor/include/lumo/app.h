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
void lumo_app_render(
    enum lumo_app_id app_id,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    bool close_active
);

#endif

#include "lumo/shell.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const uint32_t lumo_shell_launcher_columns = 3;
static const uint32_t lumo_shell_launcher_rows = 4;
static const uint32_t lumo_shell_osk_rows = 3;
static const uint32_t lumo_shell_osk_row_columns[] = {
    10,
    10,
    6,
};
static const char *const lumo_shell_osk_key_texts[] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "y", "z",
};

static bool lumo_shell_launcher_geometry(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t tile_index,
    struct lumo_rect *rect
) {
    uint32_t gap;
    uint32_t header_height;
    uint32_t grid_top;
    uint32_t tile_width;
    uint32_t tile_height;
    uint32_t row;
    uint32_t col;

    if (rect == NULL || output_width == 0 || output_height == 0 ||
            tile_index >= lumo_shell_launcher_columns * lumo_shell_launcher_rows) {
        return false;
    }

    row = tile_index / lumo_shell_launcher_columns;
    col = tile_index % lumo_shell_launcher_columns;
    gap = 18;
    header_height = output_height / 5;
    grid_top = header_height + 24;
    tile_width = (output_width > gap * (lumo_shell_launcher_columns + 1))
        ? (output_width - gap * (lumo_shell_launcher_columns + 1)) /
            lumo_shell_launcher_columns
        : output_width / lumo_shell_launcher_columns;
    tile_height = (output_height > grid_top + gap * (lumo_shell_launcher_rows + 1))
        ? (output_height - grid_top - gap * (lumo_shell_launcher_rows + 1)) /
            lumo_shell_launcher_rows
        : output_height / lumo_shell_launcher_rows;

    if (tile_width < 64) {
        tile_width = 64;
    }
    if (tile_height < 64) {
        tile_height = 64;
    }

    rect->x = (int)(gap + col * (tile_width + gap));
    rect->y = (int)(grid_top + row * (tile_height + gap));
    rect->width = (int)tile_width;
    rect->height = (int)tile_height;
    return true;
}

static bool lumo_shell_osk_geometry(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t key_index,
    struct lumo_rect *rect
) {
    uint32_t row;
    uint32_t col;
    uint32_t gap;
    uint32_t top_bar;
    uint32_t row_height;
    uint32_t columns;
    uint32_t key_width;
    uint32_t key_x;
    uint32_t key_y;

    if (rect == NULL || output_width == 0 || output_height == 0) {
        return false;
    }

    row = 0;
    columns = 0;
    for (uint32_t candidate_row = 0; candidate_row < lumo_shell_osk_rows;
            candidate_row++) {
        uint32_t row_columns = lumo_shell_osk_row_columns[candidate_row];
        if (key_index < columns + row_columns) {
            row = candidate_row;
            col = key_index - columns;
            break;
        }
        columns += row_columns;
        row = lumo_shell_osk_rows;
    }

    if (row >= lumo_shell_osk_rows) {
        return false;
    }

    gap = 8;
    top_bar = 42;
    row_height = (output_height > top_bar + gap * 4)
        ? (output_height - top_bar - gap * 4) / 3
        : output_height / 3;

    if (row_height < 40) {
        row_height = 40;
    }

    columns = lumo_shell_osk_row_columns[row];
    key_width = (output_width > gap * (columns + 1))
        ? (output_width - gap * (columns + 1)) / columns
        : output_width / columns;
    if (key_width < 28) {
        key_width = 28;
    }

    key_x = gap + col * (key_width + gap);
    key_y = top_bar + gap + row * (row_height + gap);
    if (row == 2 && col == 2) {
        rect->x = (int)key_x;
        rect->y = (int)key_y;
        rect->width = (int)(key_width * 3 + gap * 2);
        rect->height = (int)row_height;
        return true;
    }

    if (row == 2 && col > 2) {
        key_x += key_width * 2 + gap * 2;
    }

    rect->x = (int)key_x;
    rect->y = (int)key_y;
    rect->width = (int)key_width;
    rect->height = (int)row_height;
    return true;
}

const char *lumo_shell_mode_name(enum lumo_shell_mode mode) {
    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        return "launcher";
    case LUMO_SHELL_MODE_OSK:
        return "osk";
    case LUMO_SHELL_MODE_GESTURE:
        return "gesture";
    default:
        return "unknown";
    }
}

const char *lumo_shell_target_kind_name(enum lumo_shell_target_kind kind) {
    switch (kind) {
    case LUMO_SHELL_TARGET_LAUNCHER_TILE:
        return "launcher-tile";
    case LUMO_SHELL_TARGET_OSK_KEY:
        return "osk-key";
    case LUMO_SHELL_TARGET_GESTURE_HANDLE:
        return "gesture-handle";
    case LUMO_SHELL_TARGET_NONE:
    default:
        return "none";
    }
}

const char *lumo_shell_osk_key_text(uint32_t key_index) {
    if (key_index >= sizeof(lumo_shell_osk_key_texts) /
            sizeof(lumo_shell_osk_key_texts[0])) {
        return NULL;
    }

    return lumo_shell_osk_key_texts[key_index];
}

size_t lumo_shell_launcher_tile_count(void) {
    return lumo_shell_launcher_columns * lumo_shell_launcher_rows;
}

size_t lumo_shell_osk_key_count(void) {
    size_t count = 0;

    for (uint32_t row = 0; row < lumo_shell_osk_rows; row++) {
        count += lumo_shell_osk_row_columns[row];
    }

    return count;
}

bool lumo_shell_launcher_tile_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t tile_index,
    struct lumo_rect *rect
) {
    return lumo_shell_launcher_geometry(output_width, output_height,
        tile_index, rect);
}

bool lumo_shell_osk_key_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t key_index,
    struct lumo_rect *rect
) {
    return lumo_shell_osk_geometry(output_width, output_height, key_index, rect);
}

bool lumo_shell_gesture_handle_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    if (rect == NULL || output_width == 0 || output_height == 0) {
        return false;
    }

    rect->x = 0;
    rect->y = 0;
    rect->width = (int)output_width;
    rect->height = (int)output_height;
    return true;
}

bool lumo_shell_target_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    double x,
    double y,
    struct lumo_shell_target *target
) {
    struct lumo_rect rect;
    uint32_t count;

    if (target == NULL || output_width == 0 || output_height == 0) {
        return false;
    }

    memset(target, 0, sizeof(*target));

    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        count = (uint32_t)lumo_shell_launcher_tile_count();
        for (uint32_t i = 0; i < count; i++) {
            if (!lumo_shell_launcher_geometry(output_width, output_height, i,
                    &rect)) {
                continue;
            }
            if (lumo_rect_contains(&rect, x, y)) {
                target->kind = LUMO_SHELL_TARGET_LAUNCHER_TILE;
                target->index = i;
                target->rect = rect;
                return true;
            }
        }
        return false;
    case LUMO_SHELL_MODE_OSK:
        count = (uint32_t)lumo_shell_osk_key_count();
        for (uint32_t i = 0; i < count; i++) {
            if (!lumo_shell_osk_geometry(output_width, output_height, i,
                    &rect)) {
                continue;
            }
            if (lumo_rect_contains(&rect, x, y)) {
                target->kind = LUMO_SHELL_TARGET_OSK_KEY;
                target->index = i;
                target->rect = rect;
                return true;
            }
        }
        return false;
    case LUMO_SHELL_MODE_GESTURE:
        if (!lumo_shell_gesture_handle_rect(output_width, output_height,
                &rect)) {
            return false;
        }
        if (!lumo_rect_contains(&rect, x, y)) {
            return false;
        }
        target->kind = LUMO_SHELL_TARGET_GESTURE_HANDLE;
        target->index = 0;
        target->rect = rect;
        return true;
    default:
        return false;
    }
}

bool lumo_shell_surface_config_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_shell_surface_config *config
) {
    uint32_t osk_height;
    uint32_t gesture_height;

    if (config == NULL || output_width == 0 || output_height == 0) {
        return false;
    }

    memset(config, 0, sizeof(*config));
    config->mode = mode;
    config->width = output_width;
    config->keyboard_interactive = false;

    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        config->name = "launcher";
        config->height = output_height;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = 0;
        config->keyboard_interactive = true;
        config->background_rgba = 0xFF172033;
        break;
    case LUMO_SHELL_MODE_OSK:
        config->name = "osk";
        osk_height = output_height / 3;
        if (osk_height < 240) {
            osk_height = 240;
        }
        if (osk_height > output_height) {
            osk_height = output_height;
        }
        config->height = osk_height;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = (int32_t)osk_height;
        config->keyboard_interactive = true;
        config->background_rgba = 0xFF222222;
        break;
    case LUMO_SHELL_MODE_GESTURE:
        config->name = "gesture";
        gesture_height = output_height / 24;
        if (gesture_height < 24) {
            gesture_height = 24;
        }
        if (gesture_height > output_height) {
            gesture_height = output_height;
        }
        config->height = gesture_height;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = (int32_t)gesture_height;
        config->keyboard_interactive = false;
        config->background_rgba = 0xCC0F1115;
        break;
    default:
        return false;
    }

    return true;
}

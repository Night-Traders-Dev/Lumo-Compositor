#include "lumo/shell.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const uint32_t lumo_shell_launcher_columns = 3;
static const uint32_t lumo_shell_launcher_rows = 4;
static const char *const lumo_shell_launcher_labels[] = {
    "PHONE",
    "MESSAGES",
    "BROWSER",
    "CAMERA",
    "MAPS",
    "MUSIC",
    "PHOTOS",
    "VIDEOS",
    "CLOCK",
    "NOTES",
    "FILES",
    "SETTINGS",
};

static uint32_t lumo_shell_max_u32(uint32_t lhs, uint32_t rhs) {
    return lhs > rhs ? lhs : rhs;
}

static uint32_t lumo_shell_clamp_u32(
    uint32_t value,
    uint32_t minimum,
    uint32_t maximum
) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static bool lumo_shell_launcher_geometry(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t tile_index,
    struct lumo_rect *rect
) {
    uint32_t row;
    uint32_t col;
    uint32_t gap;
    uint32_t side_inset;
    uint32_t top_inset;
    uint32_t bottom_inset;
    uint32_t header_height;
    uint32_t panel_width;
    uint32_t grid_top;
    uint32_t grid_height;
    uint32_t tile_width;
    uint32_t tile_height;

    if (rect == NULL || output_width == 0 || output_height == 0 ||
            tile_index >= lumo_shell_launcher_columns * lumo_shell_launcher_rows) {
        return false;
    }

    row = tile_index / lumo_shell_launcher_columns;
    col = tile_index % lumo_shell_launcher_columns;

    gap = lumo_shell_clamp_u32(output_width / 64, 14, 28);
    side_inset = lumo_shell_clamp_u32(output_width / 18, 24, 56);
    top_inset = lumo_shell_clamp_u32(output_height / 10, 52, 112);
    bottom_inset = lumo_shell_clamp_u32(output_height / 24, 24, 52);
    header_height = lumo_shell_clamp_u32(output_height / 7, 68, 132);

    if (output_width <= side_inset * 2 + gap * (lumo_shell_launcher_columns + 1) ||
            output_height <= top_inset + bottom_inset + header_height +
                gap * (lumo_shell_launcher_rows + 2)) {
        return false;
    }

    panel_width = output_width - side_inset * 2;
    grid_top = top_inset + header_height;
    grid_height = output_height - grid_top - bottom_inset;
    tile_width = (panel_width - gap * (lumo_shell_launcher_columns + 1)) /
        lumo_shell_launcher_columns;
    tile_height = (grid_height - gap * (lumo_shell_launcher_rows + 1)) /
        lumo_shell_launcher_rows;

    tile_width = lumo_shell_max_u32(tile_width, 88);
    tile_height = lumo_shell_max_u32(tile_height, 88);

    rect->x = (int)(side_inset + gap + col * (tile_width + gap));
    rect->y = (int)(grid_top + gap + row * (tile_height + gap));
    rect->width = (int)tile_width;
    rect->height = (int)tile_height;
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

const char *lumo_shell_launcher_tile_label(uint32_t tile_index) {
    if (tile_index >= sizeof(lumo_shell_launcher_labels) /
            sizeof(lumo_shell_launcher_labels[0])) {
        return NULL;
    }

    return lumo_shell_launcher_labels[tile_index];
}

size_t lumo_shell_launcher_tile_count(void) {
    return lumo_shell_launcher_columns * lumo_shell_launcher_rows;
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

bool lumo_shell_gesture_handle_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    uint32_t handle_width;
    uint32_t handle_height;

    if (rect == NULL || output_width == 0 || output_height == 0) {
        return false;
    }

    handle_width = lumo_shell_clamp_u32(output_width / 5, 112, 220);
    handle_height = lumo_shell_clamp_u32(output_height / 3, 8, 14);

    rect->width = (int)handle_width;
    rect->height = (int)handle_height;
    rect->x = (int)((output_width - handle_width) / 2);
    rect->y = (int)((output_height - handle_height) / 2);
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
            if (!lumo_shell_osk_key_rect(output_width, output_height, i,
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
        config->background_rgba = 0x00101822;
        break;
    case LUMO_SHELL_MODE_OSK:
        config->name = "osk";
        osk_height = (output_height * 21) / 50;
        osk_height = lumo_shell_clamp_u32(osk_height, 280, output_height);
        config->height = osk_height;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = (int32_t)osk_height;
        config->keyboard_interactive = true;
        config->background_rgba = 0x0012161C;
        break;
    case LUMO_SHELL_MODE_GESTURE:
        config->name = "gesture";
        gesture_height = lumo_shell_clamp_u32(output_height / 24, 28, 52);
        config->height = gesture_height;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = (int32_t)gesture_height;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        break;
    default:
        return false;
    }

    return true;
}

bool lumo_shell_surface_bootstrap_config(
    enum lumo_shell_mode mode,
    struct lumo_shell_surface_config *config
) {
    if (config == NULL) {
        return false;
    }

    memset(config, 0, sizeof(*config));
    config->mode = mode;

    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        config->name = "launcher";
        config->width = 1;
        config->height = 1;
        config->anchor = LUMO_SHELL_ANCHOR_TOP | LUMO_SHELL_ANCHOR_LEFT;
        config->exclusive_zone = 0;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_OSK:
        config->name = "osk";
        config->width = 0;
        config->height = 1;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = 0;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_GESTURE:
        config->name = "gesture";
        config->width = 0;
        config->height = 40;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = 40;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        return true;
    default:
        return false;
    }
}

#include "lumo/shell.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const uint32_t lumo_shell_launcher_columns = 3;
static const uint32_t lumo_shell_launcher_rows = 4;
static const char *const lumo_shell_launcher_labels[] = {
    "PHONE",
    "TERMINAL",
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
static const char *const lumo_shell_launcher_commands[] = {
    "lumo-app:phone",
    "lumo-app:messages",
    "lumo-browser",
    "lumo-app:camera",
    "lumo-app:maps",
    "lumo-app:music",
    "lumo-app:photos",
    "lumo-app:videos",
    "lumo-app:clock",
    "lumo-app:notes",
    "lumo-app:files",
    "lumo-app:settings",
};
static const char *const lumo_shell_touch_audit_names[] = {
    "top-left",
    "top-center",
    "top-right",
    "left-center",
    "right-center",
    "bottom-left",
    "bottom-center",
    "bottom-right",
};
static const char *const lumo_shell_touch_audit_labels[] = {
    "TOP LEFT",
    "TOP",
    "TOP RIGHT",
    "LEFT",
    "RIGHT",
    "BOTTOM LEFT",
    "BOTTOM",
    "BOTTOM RIGHT",
};

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

static bool lumo_shell_launcher_panel_geometry(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    uint32_t inset_x;
    uint32_t inset_bottom;
    uint32_t top_y;

    if (rect == NULL || output_width == 0 || output_height == 0) {
        return false;
    }

    inset_x = lumo_shell_clamp_u32(output_width / 24, 24, 64);
    inset_bottom = lumo_shell_clamp_u32(output_height / 28, 18, 40);
    top_y = lumo_shell_clamp_u32(output_height / 18, 36, 84);

    if (output_width <= inset_x * 2 || output_height <= top_y + inset_bottom) {
        return false;
    }

    rect->x = (int)inset_x;
    rect->y = (int)top_y;
    rect->width = (int)(output_width - inset_x * 2);
    rect->height = (int)(output_height - top_y - inset_bottom);
    return rect->width > 0 && rect->height > 0;
}

static bool lumo_shell_launcher_close_geometry(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    struct lumo_rect panel;
    uint32_t size;

    if (rect == NULL ||
            !lumo_shell_launcher_panel_geometry(output_width, output_height,
                &panel)) {
        return false;
    }

    size = lumo_shell_clamp_u32(output_width / 20, 44, 60);
    rect->width = (int)size;
    rect->height = (int)size;
    rect->x = panel.x + panel.width - (int)size - 24;
    rect->y = panel.y + 18;
    return true;
}

static bool lumo_shell_launcher_geometry(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t tile_index,
    struct lumo_rect *rect
) {
    /* GNOME 3.x style centered grid — must match shell_client.c */
    uint32_t cols = 4;
    uint32_t icon_size = 56;
    uint32_t gap_x = 24;
    uint32_t gap_y = 20;
    uint32_t cell_w = icon_size + 24;
    uint32_t cell_h = icon_size + 30;
    uint32_t grid_w, grid_h, grid_x, grid_y;
    uint32_t total_rows, row, col, cx;

    if (rect == NULL || output_width == 0 || output_height == 0 ||
            tile_index >= lumo_shell_launcher_columns * lumo_shell_launcher_rows) {
        return false;
    }
    total_rows = (lumo_shell_launcher_columns * lumo_shell_launcher_rows +
        cols - 1) / cols;
    grid_w = cols * cell_w + (cols - 1) * gap_x;
    grid_h = total_rows * (cell_h + gap_y) - gap_y;
    grid_x = (output_width - grid_w) / 2;
    /* position in upper portion: search bar (40) + padding */
    grid_y = 56 + 40 + 24;
    (void)grid_h; /* grid_h used only for centering, not needed here */

    row = tile_index / cols;
    col = tile_index % cols;
    cx = grid_x + col * (cell_w + gap_x) + cell_w / 2;

    rect->x = (int)(cx - cell_w / 2);
    rect->y = (int)(grid_y + row * (cell_h + gap_y));
    rect->width = (int)cell_w;
    rect->height = (int)cell_h;
    return true;
}

static bool lumo_shell_quick_settings_panel_geometry(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    int panel_w;

    if (rect == NULL || output_width == 0 || output_height <= 56) {
        return false;
    }

    panel_w = (int)(output_width / 2);
    if (panel_w < 200) {
        panel_w = 200;
    }
    if (panel_w > (int)output_width - 16) {
        panel_w = (int)output_width - 16;
    }
    if (panel_w <= 0) {
        return false;
    }

    rect->x = (int)output_width - panel_w - 8;
    rect->y = 52;
    rect->width = panel_w;
    rect->height = (int)output_height - 56;
    return rect->height > 0;
}

static bool lumo_shell_quick_settings_button_geometry(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t button_index,
    struct lumo_rect *rect
) {
    struct lumo_rect panel;
    int half_button_width;
    int top_row_y;

    if (rect == NULL || button_index > 2 ||
            !lumo_shell_quick_settings_panel_geometry(output_width,
                output_height, &panel)) {
        return false;
    }

    half_button_width = (panel.width - 36) / 2;
    top_row_y = panel.y + 222;
    rect->height = 28;

    if (button_index == 0) {
        rect->x = panel.x + 12;
        rect->y = top_row_y;
        rect->width = half_button_width;
        return rect->width > 0;
    }

    if (button_index == 1) {
        rect->x = panel.x + 12 + half_button_width + 12;
        rect->y = top_row_y;
        rect->width = half_button_width;
        return rect->width > 0;
    }

    rect->x = panel.x + 12;
    rect->y = top_row_y + rect->height + 8;
    rect->width = half_button_width;
    return rect->width > 0;
}

const char *lumo_shell_launcher_tile_command(uint32_t tile_index) {
    if (tile_index >= sizeof(lumo_shell_launcher_commands) /
            sizeof(lumo_shell_launcher_commands[0])) {
        return NULL;
    }

    return lumo_shell_launcher_commands[tile_index];
}

const char *lumo_shell_mode_name(enum lumo_shell_mode mode) {
    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        return "launcher";
    case LUMO_SHELL_MODE_OSK:
        return "osk";
    case LUMO_SHELL_MODE_GESTURE:
        return "gesture";
    case LUMO_SHELL_MODE_STATUS:
        return "status";
    case LUMO_SHELL_MODE_BACKGROUND:
        return "background";
    default:
        return "unknown";
    }
}

size_t lumo_shell_mode_count(void) {
    return 5;
}

bool lumo_shell_mode_index(
    enum lumo_shell_mode mode,
    size_t *index
) {
    size_t resolved_index;

    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        resolved_index = 0;
        break;
    case LUMO_SHELL_MODE_OSK:
        resolved_index = 1;
        break;
    case LUMO_SHELL_MODE_GESTURE:
        resolved_index = 2;
        break;
    case LUMO_SHELL_MODE_STATUS:
        resolved_index = 3;
        break;
    case LUMO_SHELL_MODE_BACKGROUND:
        resolved_index = 4;
        break;
    default:
        return false;
    }

    if (index != NULL) {
        *index = resolved_index;
    }
    return true;
}

const char *lumo_shell_target_kind_name(enum lumo_shell_target_kind kind) {
    switch (kind) {
    case LUMO_SHELL_TARGET_LAUNCHER_TILE:
        return "launcher-tile";
    case LUMO_SHELL_TARGET_LAUNCHER_CLOSE:
        return "launcher-close";
    case LUMO_SHELL_TARGET_OSK_KEY:
        return "osk-key";
    case LUMO_SHELL_TARGET_GESTURE_HANDLE:
        return "gesture-handle";
    case LUMO_SHELL_TARGET_NONE:
    default:
        return "none";
    }
}

const char *lumo_shell_touch_debug_phase_name(
    enum lumo_shell_touch_debug_phase phase
) {
    switch (phase) {
    case LUMO_SHELL_TOUCH_DEBUG_DOWN:
        return "down";
    case LUMO_SHELL_TOUCH_DEBUG_MOTION:
        return "motion";
    case LUMO_SHELL_TOUCH_DEBUG_UP:
        return "up";
    case LUMO_SHELL_TOUCH_DEBUG_CANCEL:
        return "cancel";
    case LUMO_SHELL_TOUCH_DEBUG_NONE:
    default:
        return "none";
    }
}

bool lumo_shell_touch_debug_phase_parse(
    const char *value,
    enum lumo_shell_touch_debug_phase *phase
) {
    if (value == NULL || phase == NULL) {
        return false;
    }

    if (strcmp(value, "down") == 0) {
        *phase = LUMO_SHELL_TOUCH_DEBUG_DOWN;
        return true;
    }
    if (strcmp(value, "motion") == 0) {
        *phase = LUMO_SHELL_TOUCH_DEBUG_MOTION;
        return true;
    }
    if (strcmp(value, "up") == 0) {
        *phase = LUMO_SHELL_TOUCH_DEBUG_UP;
        return true;
    }
    if (strcmp(value, "cancel") == 0) {
        *phase = LUMO_SHELL_TOUCH_DEBUG_CANCEL;
        return true;
    }
    if (strcmp(value, "none") == 0) {
        *phase = LUMO_SHELL_TOUCH_DEBUG_NONE;
        return true;
    }

    return false;
}

const char *lumo_shell_touch_debug_target_name(
    enum lumo_shell_touch_debug_target target
) {
    switch (target) {
    case LUMO_SHELL_TOUCH_DEBUG_TARGET_HITBOX:
        return "hitbox";
    case LUMO_SHELL_TOUCH_DEBUG_TARGET_SURFACE:
        return "surface";
    case LUMO_SHELL_TOUCH_DEBUG_TARGET_NONE:
    default:
        return "none";
    }
}

bool lumo_shell_touch_debug_target_parse(
    const char *value,
    enum lumo_shell_touch_debug_target *target
) {
    if (value == NULL || target == NULL) {
        return false;
    }

    if (strcmp(value, "hitbox") == 0) {
        *target = LUMO_SHELL_TOUCH_DEBUG_TARGET_HITBOX;
        return true;
    }
    if (strcmp(value, "surface") == 0) {
        *target = LUMO_SHELL_TOUCH_DEBUG_TARGET_SURFACE;
        return true;
    }
    if (strcmp(value, "none") == 0) {
        *target = LUMO_SHELL_TOUCH_DEBUG_TARGET_NONE;
        return true;
    }

    return false;
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

size_t lumo_shell_touch_audit_point_count(void) {
    return sizeof(lumo_shell_touch_audit_names) /
        sizeof(lumo_shell_touch_audit_names[0]);
}

const char *lumo_shell_touch_audit_point_name(uint32_t point_index) {
    if (point_index >= lumo_shell_touch_audit_point_count()) {
        return NULL;
    }

    return lumo_shell_touch_audit_names[point_index];
}

const char *lumo_shell_touch_audit_point_label(uint32_t point_index) {
    if (point_index >= lumo_shell_touch_audit_point_count()) {
        return NULL;
    }

    return lumo_shell_touch_audit_labels[point_index];
}

bool lumo_shell_touch_audit_point_for_region(
    const char *region,
    uint32_t *point_index
) {
    if (region == NULL || point_index == NULL) {
        return false;
    }

    for (uint32_t i = 0; i < lumo_shell_touch_audit_point_count(); i++) {
        if (strcmp(region, lumo_shell_touch_audit_names[i]) == 0) {
            *point_index = i;
            return true;
        }
    }

    return false;
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

bool lumo_shell_launcher_panel_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    return lumo_shell_launcher_panel_geometry(output_width, output_height,
        rect);
}

bool lumo_shell_launcher_close_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    return lumo_shell_launcher_close_geometry(output_width, output_height,
        rect);
}

bool lumo_shell_quick_settings_button_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t button_index,
    struct lumo_rect *rect
) {
    return lumo_shell_quick_settings_button_geometry(output_width,
        output_height, button_index, rect);
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

uint32_t lumo_shell_transition_duration_ms(
    enum lumo_shell_mode mode,
    bool visible
) {
    /* Material Design durations: 300-500ms for panels, 250-350ms for dismiss */
    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        return visible ? 350u : 250u;
    case LUMO_SHELL_MODE_OSK:
        return visible ? 300u : 200u;
    case LUMO_SHELL_MODE_GESTURE:
    case LUMO_SHELL_MODE_STATUS:
    case LUMO_SHELL_MODE_BACKGROUND:
    default:
        return 0u;
    }
}

bool lumo_shell_touch_audit_point_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t point_index,
    struct lumo_rect *rect
) {
    uint32_t marker_width;
    uint32_t marker_height;
    uint32_t inset_x;
    uint32_t inset_y;
    int center_x;
    int center_y;

    if (rect == NULL || output_width == 0 || output_height == 0 ||
            point_index >= lumo_shell_touch_audit_point_count()) {
        return false;
    }

    marker_width = lumo_shell_clamp_u32(output_width / 8, 96, 160);
    marker_height = lumo_shell_clamp_u32(output_height / 12, 54, 92);
    inset_x = lumo_shell_clamp_u32(output_width / 12, 68, 132);
    inset_y = lumo_shell_clamp_u32(output_height / 10, 54, 116);

    switch ((enum lumo_shell_touch_audit_point)point_index) {
    case LUMO_SHELL_TOUCH_AUDIT_TOP_LEFT:
        center_x = (int)inset_x;
        center_y = (int)inset_y;
        break;
    case LUMO_SHELL_TOUCH_AUDIT_TOP_CENTER:
        center_x = (int)(output_width / 2);
        center_y = (int)inset_y;
        break;
    case LUMO_SHELL_TOUCH_AUDIT_TOP_RIGHT:
        center_x = (int)(output_width - inset_x);
        center_y = (int)inset_y;
        break;
    case LUMO_SHELL_TOUCH_AUDIT_LEFT_CENTER:
        center_x = (int)inset_x;
        center_y = (int)(output_height / 2);
        break;
    case LUMO_SHELL_TOUCH_AUDIT_RIGHT_CENTER:
        center_x = (int)(output_width - inset_x);
        center_y = (int)(output_height / 2);
        break;
    case LUMO_SHELL_TOUCH_AUDIT_BOTTOM_LEFT:
        center_x = (int)inset_x;
        center_y = (int)(output_height - inset_y);
        break;
    case LUMO_SHELL_TOUCH_AUDIT_BOTTOM_CENTER:
        center_x = (int)(output_width / 2);
        center_y = (int)(output_height - inset_y);
        break;
    case LUMO_SHELL_TOUCH_AUDIT_BOTTOM_RIGHT:
        center_x = (int)(output_width - inset_x);
        center_y = (int)(output_height - inset_y);
        break;
    default:
        return false;
    }

    rect->width = (int)marker_width;
    rect->height = (int)marker_height;
    rect->x = center_x - rect->width / 2;
    rect->y = center_y - rect->height / 2;
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
        if (lumo_shell_launcher_close_geometry(output_width, output_height,
                &rect) &&
                lumo_rect_contains(&rect, x, y)) {
            target->kind = LUMO_SHELL_TARGET_LAUNCHER_CLOSE;
            target->index = 0;
            target->rect = rect;
            return true;
        }

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
    case LUMO_SHELL_MODE_STATUS:
    case LUMO_SHELL_MODE_BACKGROUND:
        return false;
    default:
        return false;
    }
}

bool lumo_shell_surface_local_coords(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    uint32_t surface_width,
    uint32_t surface_height,
    double global_x,
    double global_y,
    double *local_x,
    double *local_y
) {
    double origin_x = 0.0;
    double origin_y = 0.0;
    double x;
    double y;

    if (output_width == 0 || output_height == 0 || surface_width == 0 ||
            surface_height == 0 || local_x == NULL || local_y == NULL) {
        return false;
    }

    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        origin_x = 0.0;
        origin_y = 0.0;
        break;
    case LUMO_SHELL_MODE_OSK:
    case LUMO_SHELL_MODE_GESTURE:
        origin_x = 0.0;
        origin_y = (double)output_height - (double)surface_height;
        break;
    case LUMO_SHELL_MODE_STATUS:
    case LUMO_SHELL_MODE_BACKGROUND:
        origin_x = 0.0;
        origin_y = 0.0;
        break;
    default:
        return false;
    }

    x = global_x - origin_x;
    y = global_y - origin_y;
    if (x < 0.0 || y < 0.0 || x >= (double)surface_width ||
            y >= (double)surface_height) {
        return false;
    }

    *local_x = x;
    *local_y = y;
    return true;
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
        osk_height = (output_height * 2) / 5;
        osk_height = lumo_shell_clamp_u32(osk_height, 260, output_height);
        config->height = osk_height;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = (int32_t)osk_height;
        config->keyboard_interactive = true;
        config->background_rgba = 0x002A2A2E;
        break;
    case LUMO_SHELL_MODE_GESTURE:
        config->name = "gesture";
        /* wider gesture zone for better swipe-to-close reliability.
         * Android uses 48dp, iOS ~34pt. We use ~80px which is ~6%
         * of a 1280px display — enough for reliable bottom-edge swipes */
        gesture_height = lumo_shell_clamp_u32(output_height / 16, 48, 80);
        config->height = gesture_height;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = (int32_t)gesture_height;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        break;
    case LUMO_SHELL_MODE_STATUS: {
        uint32_t status_height = lumo_shell_clamp_u32(output_height / 18, 32, 48);
        config->name = "status";
        config->height = status_height;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = (int32_t)status_height;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        break;
    }
    case LUMO_SHELL_MODE_BACKGROUND:
        config->name = "background";
        config->width = output_width;
        config->height = output_height;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = 0;
        config->keyboard_interactive = false;
        config->background_rgba = 0xFF2C001E;
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
        config->width = 0;
        config->height = 0;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
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
        config->height = 60;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = 60;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_STATUS:
        config->name = "status";
        config->width = 0;
        config->height = 36;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = 36;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_BACKGROUND:
        config->name = "background";
        config->width = 0;
        config->height = 0;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->exclusive_zone = 0;
        config->keyboard_interactive = false;
        config->background_rgba = 0xFF2C001E;
        return true;
    default:
        return false;
    }
}

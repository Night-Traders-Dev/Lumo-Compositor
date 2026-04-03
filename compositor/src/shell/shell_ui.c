#include "lumo/shell.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const uint32_t lumo_shell_launcher_columns = 4;
static const uint32_t lumo_shell_launcher_rows = 3;
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
    "lumo-app:browser",
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
    rect->x = panel.x + panel.width - (int)size - 16;
    rect->y = panel.y + 12;
    return true;
}

static bool lumo_shell_launcher_search_bar_geometry(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    struct lumo_rect panel;
    int bar_w;

    if (rect == NULL ||
            !lumo_shell_launcher_panel_geometry(output_width, output_height,
                &panel)) {
        return false;
    }

    bar_w = (int)(panel.width * 2 / 5);
    if (bar_w < 280) {
        bar_w = 280;
    }
    if (bar_w > 500) {
        bar_w = 500;
    }
    if (bar_w > panel.width - 32) {
        bar_w = panel.width - 32;
    }
    if (bar_w <= 0) {
        return false;
    }

    rect->x = panel.x + (panel.width - bar_w) / 2;
    rect->y = panel.y + 18;
    rect->width = bar_w;
    rect->height = 40;
    return true;
}

static bool lumo_shell_launcher_visible_tile_geometry(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t visible_index,
    struct lumo_rect *rect
) {
    struct lumo_rect panel;
    struct lumo_rect search_bar;
    struct lumo_rect grid_rect;
    int bottom_gutter;
    int grid_width;
    int grid_top;
    int grid_bottom;
    int available_height;
    int grid_height;
    int cell_width;
    int cell_height;

    if (rect == NULL || visible_index >= lumo_shell_launcher_columns *
            lumo_shell_launcher_rows ||
            !lumo_shell_launcher_panel_geometry(output_width, output_height,
                &panel) ||
            !lumo_shell_launcher_search_bar_geometry(output_width,
                output_height, &search_bar)) {
        return false;
    }

    grid_width = panel.width - 32;
    if (grid_width <= 0) {
        return false;
    }

    cell_width = grid_width / (int)lumo_shell_launcher_columns;
    if (cell_width <= 0) {
        return false;
    }

    grid_width = cell_width * (int)lumo_shell_launcher_columns;
    grid_top = search_bar.y + search_bar.height + 28;
    bottom_gutter = (int)lumo_shell_clamp_u32(output_height / 24, 20, 40);
    grid_bottom = panel.y + panel.height - bottom_gutter;
    if (grid_bottom <= grid_top) {
        return false;
    }

    available_height = grid_bottom - grid_top;
    cell_height = available_height / (int)lumo_shell_launcher_rows;
    if (cell_height <= 0) {
        return false;
    }
    cell_height = (int)lumo_shell_clamp_u32((uint32_t)cell_height, 112, 240);
    grid_height = cell_height * (int)lumo_shell_launcher_rows;
    if (grid_height > available_height) {
        cell_height = available_height / (int)lumo_shell_launcher_rows;
        if (cell_height <= 0) {
            return false;
        }
        grid_height = cell_height * (int)lumo_shell_launcher_rows;
    }

    grid_rect.x = panel.x + (panel.width - grid_width) / 2;
    grid_rect.y = grid_top + (available_height - grid_height) / 2;

    rect->x = grid_rect.x +
        (int)(visible_index % lumo_shell_launcher_columns) * cell_width;
    rect->y = grid_rect.y +
        (int)(visible_index / lumo_shell_launcher_columns) * cell_height;
    rect->width = cell_width;
    rect->height = cell_height;
    return true;
}

static bool lumo_shell_launcher_query_active(const char *query) {
    return query != NULL && query[0] != '\0' && strcmp(query, "-") != 0;
}

static bool lumo_shell_launcher_query_matches(
    const char *label,
    const char *query
) {
    if (label == NULL) {
        return false;
    }
    if (!lumo_shell_launcher_query_active(query)) {
        return true;
    }

    for (size_t i = 0; label[i] != '\0'; i++) {
        size_t j = 0;

        while (query[j] != '\0') {
            unsigned char lhs = (unsigned char)label[i + j];
            unsigned char rhs = (unsigned char)query[j];

            if (lhs == '\0' || tolower(lhs) != tolower(rhs)) {
                break;
            }
            j++;
        }

        if (query[j] == '\0') {
            return true;
        }
    }

    return false;
}

static size_t lumo_shell_launcher_collect_filtered_tiles(
    const char *query,
    uint32_t *indices,
    size_t capacity
) {
    size_t count = 0;

    for (uint32_t i = 0; i < lumo_shell_launcher_tile_count(); i++) {
        if (!lumo_shell_launcher_query_matches(
                lumo_shell_launcher_tile_label(i), query)) {
            continue;
        }

        if (indices != NULL && count < capacity) {
            indices[count] = i;
        }
        count++;
    }

    return count;
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

static bool lumo_shell_time_panel_geometry(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    int panel_w;

    if (rect == NULL || output_width == 0 || output_height <= 56) {
        return false;
    }

    panel_w = (int)(output_width / 2);
    if (panel_w < 240) {
        panel_w = 240;
    }
    if (panel_w > (int)output_width - 16) {
        panel_w = (int)output_width - 16;
    }
    if (panel_w <= 0) {
        return false;
    }

    /* center the panel horizontally under the center-third trigger zone */
    rect->x = ((int)output_width - panel_w) / 2;
    rect->y = 52;
    rect->width = panel_w;
    rect->height = 220;
    return true;
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

    if (rect == NULL || button_index > 3 ||
            !lumo_shell_quick_settings_panel_geometry(output_width,
                output_height, &panel)) {
        return false;
    }

    half_button_width = (panel.width - 36) / 2;
    top_row_y = panel.y + 222;
    rect->height = 28;

    /* row 0: buttons 0 (RELOAD) and 1 (ROTATE)
     * row 1: buttons 2 (SCREENSHOT) and 3 (SETTINGS) */
    int row = (int)button_index / 2;
    int col = (int)button_index % 2;
    rect->x = panel.x + 12 + col * (half_button_width + 12);
    rect->y = top_row_y + row * (rect->height + 8);
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
    case LUMO_SHELL_MODE_SIDEBAR:
        return "sidebar";
    default:
        return "unknown";
    }
}

size_t lumo_shell_mode_count(void) {
    return 6;
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
    case LUMO_SHELL_MODE_SIDEBAR:
        resolved_index = 5;
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
    case LUMO_SHELL_TARGET_SIDEBAR_APP:
        return "sidebar-app";
    case LUMO_SHELL_TARGET_SIDEBAR_DRAWER_BTN:
        return "sidebar-drawer-btn";
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
    return lumo_shell_launcher_visible_tile_geometry(output_width,
        output_height, tile_index, rect);
}

bool lumo_shell_launcher_search_bar_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    return lumo_shell_launcher_search_bar_geometry(output_width,
        output_height, rect);
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

bool lumo_shell_quick_settings_panel_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    return lumo_shell_quick_settings_panel_geometry(output_width,
        output_height, rect);
}

bool lumo_shell_time_panel_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    return lumo_shell_time_panel_geometry(output_width, output_height, rect);
}

bool lumo_shell_notification_panel_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
) {
    int panel_w;
    if (rect == NULL || output_width == 0 || output_height <= 56) {
        return false;
    }
    panel_w = (int)(output_width / 3);
    if (panel_w < 220) panel_w = 220;
    if (panel_w > (int)output_width - 16) panel_w = (int)output_width - 16;
    if (panel_w <= 0) return false;
    rect->x = 8;
    rect->y = 52;
    rect->width = panel_w;
    rect->height = (int)output_height - 56;
    return rect->height > 0;
}

size_t lumo_shell_launcher_filtered_tile_count(const char *query) {
    return lumo_shell_launcher_collect_filtered_tiles(query, NULL, 0);
}

bool lumo_shell_launcher_filtered_tile_rect(
    uint32_t output_width,
    uint32_t output_height,
    const char *query,
    uint32_t visible_index,
    uint32_t *tile_index,
    struct lumo_rect *rect
) {
    uint32_t visible_tiles[12] = {0};
    size_t visible_count = lumo_shell_launcher_collect_filtered_tiles(query,
        visible_tiles, sizeof(visible_tiles) / sizeof(visible_tiles[0]));

    if (visible_index >= visible_count ||
            !lumo_shell_launcher_visible_tile_geometry(output_width,
                output_height, visible_index, rect)) {
        return false;
    }

    if (tile_index != NULL) {
        *tile_index = visible_tiles[visible_index];
    }

    return true;
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
        return visible ? 200u : 150u;
    case LUMO_SHELL_MODE_OSK:
        return visible ? 300u : 200u;
    case LUMO_SHELL_MODE_SIDEBAR:
        return visible ? 200u : 150u;
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

bool lumo_shell_target_for_mode_with_query(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    const char *launcher_query,
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

        count = (uint32_t)lumo_shell_launcher_filtered_tile_count(
            launcher_query);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t tile_index = 0;

            if (!lumo_shell_launcher_filtered_tile_rect(output_width,
                    output_height, launcher_query, i, &tile_index, &rect)) {
                continue;
            }
            if (lumo_rect_contains(&rect, x, y)) {
                target->kind = LUMO_SHELL_TARGET_LAUNCHER_TILE;
                target->index = tile_index;
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
    case LUMO_SHELL_MODE_SIDEBAR:
        /* check drawer button first (at bottom) */
        if (lumo_shell_sidebar_drawer_button_rect(output_width,
                output_height, &rect) &&
                lumo_rect_contains(&rect, x, y)) {
            target->kind = LUMO_SHELL_TARGET_SIDEBAR_DRAWER_BTN;
            target->index = 0;
            target->rect = rect;
            return true;
        }
        /* check app icons */
        for (uint32_t i = 0; i < 16; i++) {
            if (!lumo_shell_sidebar_app_rect(output_width, output_height,
                    i, &rect))
                break;
            if (lumo_rect_contains(&rect, x, y)) {
                target->kind = LUMO_SHELL_TARGET_SIDEBAR_APP;
                target->index = i;
                target->rect = rect;
                return true;
            }
        }
        return false;
    case LUMO_SHELL_MODE_STATUS:
    case LUMO_SHELL_MODE_BACKGROUND:
        return false;
    default:
        return false;
    }
}

bool lumo_shell_target_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    double x,
    double y,
    struct lumo_shell_target *target
) {
    return lumo_shell_target_for_mode_with_query(mode, output_width,
        output_height, NULL, x, y, target);
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
    case LUMO_SHELL_MODE_SIDEBAR:
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
    case LUMO_SHELL_MODE_SIDEBAR: {
        /* wide sidebar for large app icons — ~20% of screen width.
         * Height excludes the status bar area so they don't overlap.
         * Anchored to bottom-left so it sits below the status bar. */
        uint32_t sidebar_w = lumo_shell_clamp_u32(output_width / 5, 160, 220);
        uint32_t status_h = lumo_shell_clamp_u32(output_height / 18, 32, 48);
        uint32_t sidebar_h = output_height > status_h
            ? output_height - status_h : output_height;
        config->name = "sidebar";
        config->width = sidebar_w;
        config->height = sidebar_h;
        config->anchor = LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT;
        config->exclusive_zone = 0;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        break;
    }
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
    case LUMO_SHELL_MODE_SIDEBAR:
        config->name = "sidebar";
        config->width = 64;
        config->height = 0;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT;
        config->exclusive_zone = 0;
        config->keyboard_interactive = false;
        config->background_rgba = 0x00000000;
        return true;
    default:
        return false;
    }
}

/* ── sidebar layout ──────────────────────────────────────────────── */

/* App icon rect within the sidebar surface (64×h or 80×h).
 * Icons are 48×48 squares centered horizontally, stacked vertically
 * starting below the status bar area (56px from top). */
bool lumo_shell_sidebar_app_rect(
    uint32_t surface_width,
    uint32_t surface_height,
    uint32_t app_index,
    struct lumo_rect *rect
) {
    int icon_size = 64;
    int spacing = 12;
    /* start below status bar area */
    int status_h = (int)surface_height / 18;
    if (status_h < 32) status_h = 32;
    if (status_h > 48) status_h = 48;
    int top_margin = status_h + 12;
    int y;

    if (rect == NULL || surface_width == 0 || surface_height == 0)
        return false;

    y = top_margin + (int)app_index * (icon_size + spacing);
    if (y + icon_size > (int)surface_height - 60) /* leave room for drawer btn */
        return false;

    rect->x = ((int)surface_width - icon_size) / 2;
    rect->y = y;
    rect->width = icon_size;
    rect->height = icon_size;
    return true;
}

/* Drawer button rect — at the bottom of the sidebar */
bool lumo_shell_sidebar_drawer_button_rect(
    uint32_t surface_width,
    uint32_t surface_height,
    struct lumo_rect *rect
) {
    int btn_size = 64;

    if (rect == NULL || surface_width == 0 || surface_height < 80)
        return false;

    rect->x = ((int)surface_width - btn_size) / 2;
    rect->y = (int)surface_height - btn_size - 12;
    rect->width = btn_size;
    rect->height = btn_size;
    return true;
}

#ifndef LUMO_SHELL_H
#define LUMO_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum lumo_shell_mode {
    LUMO_SHELL_MODE_LAUNCHER = 0,
    LUMO_SHELL_MODE_OSK,
    LUMO_SHELL_MODE_GESTURE,
    LUMO_SHELL_MODE_STATUS,
    LUMO_SHELL_MODE_BACKGROUND,
};

enum lumo_shell_anchor {
    LUMO_SHELL_ANCHOR_TOP = 1u << 0,
    LUMO_SHELL_ANCHOR_BOTTOM = 1u << 1,
    LUMO_SHELL_ANCHOR_LEFT = 1u << 2,
    LUMO_SHELL_ANCHOR_RIGHT = 1u << 3,
};

struct lumo_rect {
    int x;
    int y;
    int width;
    int height;
};

/* Inclusive top-left, exclusive bottom-right hit-testing */
static inline bool lumo_rect_contains(
    const struct lumo_rect *rect,
    double x,
    double y
) {
    return rect != NULL &&
        x >= (double)rect->x &&
        y >= (double)rect->y &&
        x < (double)rect->x + (double)rect->width &&
        y < (double)rect->y + (double)rect->height;
}

struct lumo_shell_surface_config {
    enum lumo_shell_mode mode;
    const char *name;
    uint32_t width;
    uint32_t height;
    uint32_t anchor;
    int32_t exclusive_zone;
    int32_t margin_top;
    int32_t margin_right;
    int32_t margin_bottom;
    int32_t margin_left;
    bool keyboard_interactive;
    uint32_t background_rgba;
};

enum lumo_shell_target_kind {
    LUMO_SHELL_TARGET_NONE = 0,
    LUMO_SHELL_TARGET_LAUNCHER_TILE,
    LUMO_SHELL_TARGET_LAUNCHER_CLOSE,
    LUMO_SHELL_TARGET_OSK_KEY,
    LUMO_SHELL_TARGET_GESTURE_HANDLE,
};

enum lumo_shell_touch_debug_phase {
    LUMO_SHELL_TOUCH_DEBUG_NONE = 0,
    LUMO_SHELL_TOUCH_DEBUG_DOWN,
    LUMO_SHELL_TOUCH_DEBUG_MOTION,
    LUMO_SHELL_TOUCH_DEBUG_UP,
    LUMO_SHELL_TOUCH_DEBUG_CANCEL,
};

enum lumo_shell_touch_debug_target {
    LUMO_SHELL_TOUCH_DEBUG_TARGET_NONE = 0,
    LUMO_SHELL_TOUCH_DEBUG_TARGET_HITBOX,
    LUMO_SHELL_TOUCH_DEBUG_TARGET_SURFACE,
};

enum lumo_shell_touch_audit_point {
    LUMO_SHELL_TOUCH_AUDIT_TOP_LEFT = 0,
    LUMO_SHELL_TOUCH_AUDIT_TOP_CENTER,
    LUMO_SHELL_TOUCH_AUDIT_TOP_RIGHT,
    LUMO_SHELL_TOUCH_AUDIT_LEFT_CENTER,
    LUMO_SHELL_TOUCH_AUDIT_RIGHT_CENTER,
    LUMO_SHELL_TOUCH_AUDIT_BOTTOM_LEFT,
    LUMO_SHELL_TOUCH_AUDIT_BOTTOM_CENTER,
    LUMO_SHELL_TOUCH_AUDIT_BOTTOM_RIGHT,
};

struct lumo_shell_target {
    enum lumo_shell_target_kind kind;
    uint32_t index;
    struct lumo_rect rect;
};

const char *lumo_shell_mode_name(enum lumo_shell_mode mode);
size_t lumo_shell_mode_count(void);
bool lumo_shell_mode_index(
    enum lumo_shell_mode mode,
    size_t *index
);
const char *lumo_shell_target_kind_name(enum lumo_shell_target_kind kind);
const char *lumo_shell_touch_debug_phase_name(
    enum lumo_shell_touch_debug_phase phase
);
bool lumo_shell_touch_debug_phase_parse(
    const char *value,
    enum lumo_shell_touch_debug_phase *phase
);
const char *lumo_shell_touch_debug_target_name(
    enum lumo_shell_touch_debug_target target
);
bool lumo_shell_touch_debug_target_parse(
    const char *value,
    enum lumo_shell_touch_debug_target *target
);
const char *lumo_shell_launcher_tile_label(uint32_t tile_index);
const char *lumo_shell_launcher_tile_command(uint32_t tile_index);
const char *lumo_shell_osk_key_label(uint32_t key_index);
const char *lumo_shell_osk_key_text(uint32_t key_index);
size_t lumo_shell_touch_audit_point_count(void);
const char *lumo_shell_touch_audit_point_name(uint32_t point_index);
const char *lumo_shell_touch_audit_point_label(uint32_t point_index);
bool lumo_shell_touch_audit_point_for_region(
    const char *region,
    uint32_t *point_index
);
static inline bool lumo_shell_target_kind_parse(
    const char *value,
    enum lumo_shell_target_kind *kind
) {
    if (value == NULL || kind == NULL) {
        return false;
    }

    if (strcmp(value, "launcher-tile") == 0 || strcmp(value, "launcher_tile") == 0) {
        *kind = LUMO_SHELL_TARGET_LAUNCHER_TILE;
        return true;
    }
    if (strcmp(value, "launcher-close") == 0 || strcmp(value, "launcher_close") == 0) {
        *kind = LUMO_SHELL_TARGET_LAUNCHER_CLOSE;
        return true;
    }
    if (strcmp(value, "osk-key") == 0 || strcmp(value, "osk_key") == 0) {
        *kind = LUMO_SHELL_TARGET_OSK_KEY;
        return true;
    }
    if (strcmp(value, "gesture-handle") == 0 || strcmp(value, "gesture_handle") == 0) {
        *kind = LUMO_SHELL_TARGET_GESTURE_HANDLE;
        return true;
    }
    if (strcmp(value, "none") == 0) {
        *kind = LUMO_SHELL_TARGET_NONE;
        return true;
    }

    return false;
}
bool lumo_shell_surface_config_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_shell_surface_config *config
);
bool lumo_shell_surface_bootstrap_config(
    enum lumo_shell_mode mode,
    struct lumo_shell_surface_config *config
);
size_t lumo_shell_launcher_tile_count(void);
size_t lumo_shell_osk_key_count(void);
bool lumo_shell_launcher_tile_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t tile_index,
    struct lumo_rect *rect
);
bool lumo_shell_launcher_panel_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
);
bool lumo_shell_launcher_close_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
);
bool lumo_shell_osk_key_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t key_index,
    struct lumo_rect *rect
);
bool lumo_shell_gesture_handle_rect(
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_rect *rect
);
uint32_t lumo_shell_transition_duration_ms(
    enum lumo_shell_mode mode,
    bool visible
);
bool lumo_shell_touch_audit_point_rect(
    uint32_t output_width,
    uint32_t output_height,
    uint32_t point_index,
    struct lumo_rect *rect
);
bool lumo_shell_target_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    double x,
    double y,
    struct lumo_shell_target *target
);
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
);

#endif

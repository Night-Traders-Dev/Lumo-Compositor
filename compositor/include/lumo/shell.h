#ifndef LUMO_SHELL_H
#define LUMO_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum lumo_shell_mode {
    LUMO_SHELL_MODE_LAUNCHER = 0,
    LUMO_SHELL_MODE_OSK,
    LUMO_SHELL_MODE_GESTURE,
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

static inline bool lumo_rect_contains(
    const struct lumo_rect *rect,
    double x,
    double y
) {
    return rect != NULL &&
        x >= rect->x &&
        y >= rect->y &&
        x < rect->x + rect->width &&
        y < rect->y + rect->height;
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
    LUMO_SHELL_TARGET_OSK_KEY,
    LUMO_SHELL_TARGET_GESTURE_HANDLE,
};

struct lumo_shell_target {
    enum lumo_shell_target_kind kind;
    uint32_t index;
    struct lumo_rect rect;
};

const char *lumo_shell_mode_name(enum lumo_shell_mode mode);
const char *lumo_shell_target_kind_name(enum lumo_shell_target_kind kind);
bool lumo_shell_surface_config_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
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
bool lumo_shell_target_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    double x,
    double y,
    struct lumo_shell_target *target
);

#endif

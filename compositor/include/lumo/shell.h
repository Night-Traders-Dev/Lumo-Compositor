#ifndef LUMO_SHELL_H
#define LUMO_SHELL_H

#include <stdbool.h>
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

const char *lumo_shell_mode_name(enum lumo_shell_mode mode);
bool lumo_shell_surface_config_for_mode(
    enum lumo_shell_mode mode,
    uint32_t output_width,
    uint32_t output_height,
    struct lumo_shell_surface_config *config
);

#endif

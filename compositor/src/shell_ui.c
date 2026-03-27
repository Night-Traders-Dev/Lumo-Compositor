#include "lumo/shell.h"

#include <string.h>

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

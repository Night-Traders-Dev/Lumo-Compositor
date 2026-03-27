#include "lumo/compositor.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

static void lumo_print_usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [--debug] [--session NAME] [--socket NAME] [--shell PATH] [--rotation normal|90|180|270]\n",
        argv0);
}

static bool lumo_parse_rotation(const char *value, enum lumo_rotation *rotation) {
    if (value == NULL || rotation == NULL) {
        return false;
    }

    if (strcmp(value, "normal") == 0 || strcmp(value, "0") == 0) {
        *rotation = LUMO_ROTATION_NORMAL;
        return true;
    }
    if (strcmp(value, "90") == 0) {
        *rotation = LUMO_ROTATION_90;
        return true;
    }
    if (strcmp(value, "180") == 0) {
        *rotation = LUMO_ROTATION_180;
        return true;
    }
    if (strcmp(value, "270") == 0) {
        *rotation = LUMO_ROTATION_270;
        return true;
    }

    return false;
}

int main(int argc, char **argv) {
    struct lumo_compositor_config config = {
        .session_name = "lumo",
        .socket_name = "lumo-shell",
        .executable_path = argv[0],
        .shell_path = NULL,
        .initial_rotation = LUMO_ROTATION_NORMAL,
        .debug = false,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            config.debug = true;
            continue;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            lumo_print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--session") == 0 && i + 1 < argc) {
            config.session_name = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            config.socket_name = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--shell") == 0 && i + 1 < argc) {
            config.shell_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--rotation") == 0 && i + 1 < argc) {
            if (!lumo_parse_rotation(argv[++i], &config.initial_rotation)) {
                fprintf(stderr, "invalid rotation value: %s\n", argv[i]);
                lumo_print_usage(argv[0]);
                return 1;
            }
            continue;
        }

        fprintf(stderr, "unknown argument: %s\n", argv[i]);
        lumo_print_usage(argv[0]);
        return 1;
    }

    wlr_log_init(config.debug ? WLR_DEBUG : WLR_INFO, NULL);

    struct lumo_compositor *compositor = lumo_compositor_create(&config);
    if (compositor == NULL) {
        fputs("failed to allocate compositor\n", stderr);
        return 1;
    }

    if (lumo_compositor_run(compositor) != 0) {
        lumo_compositor_destroy(compositor);
        return 1;
    }

    lumo_compositor_destroy(compositor);
    return 0;
}

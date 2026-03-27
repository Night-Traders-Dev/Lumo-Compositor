#include "lumo/compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *lumo_default_session_name(void) {
    return "lumo";
}

static const char *lumo_default_socket_name(void) {
    return "lumo-shell";
}

struct lumo_compositor *lumo_compositor_create(
    const struct lumo_compositor_config *config
) {
    struct lumo_compositor *compositor = calloc(1, sizeof(*compositor));
    if (compositor == NULL) {
        return NULL;
    }

    compositor->config = config;
    compositor->display = wl_display_create();
    if (compositor->display == NULL) {
        free(compositor);
        return NULL;
    }

    compositor->event_loop = wl_display_get_event_loop(compositor->display);
    compositor->running = false;
    compositor->keyboard_visible = false;
    compositor->launcher_visible = false;
    compositor->active_rotation = config != NULL
        ? config->initial_rotation
        : LUMO_ROTATION_NORMAL;
    compositor->scrim_state = LUMO_SCRIM_HIDDEN;
    compositor->gesture_threshold = 32.0;
    compositor->gesture_timeout_ms = 180;
    compositor->keyboard_resize_serial = 0;
    compositor->keyboard_resize_pending = false;
    compositor->keyboard_resize_acked = true;
    compositor->input_state = NULL;
    compositor->protocol_state = NULL;
    compositor->xwayland = NULL;

    wl_list_init(&compositor->outputs);
    wl_list_init(&compositor->keyboards);
    wl_list_init(&compositor->toplevels);
    wl_list_init(&compositor->popups);
    wl_list_init(&compositor->layer_surfaces);
    wl_list_init(&compositor->hitboxes);
    wl_list_init(&compositor->touch_points);

    return compositor;
}

void lumo_compositor_stop(struct lumo_compositor *compositor) {
    if (compositor == NULL || compositor->display == NULL) {
        return;
    }

    compositor->running = false;
    wl_display_terminate(compositor->display);
}

static void lumo_compositor_cleanup(struct lumo_compositor *compositor) {
    if (compositor == NULL) {
        return;
    }

    lumo_protocol_stop(compositor);
    lumo_input_stop(compositor);
    lumo_output_stop(compositor);
    lumo_backend_stop(compositor);

    if (compositor->display != NULL) {
        wl_display_destroy_clients(compositor->display);
        wl_display_destroy(compositor->display);
        compositor->display = NULL;
    }
}

void lumo_compositor_destroy(struct lumo_compositor *compositor) {
    if (compositor == NULL) {
        return;
    }

    lumo_compositor_cleanup(compositor);
    free(compositor);
}

int lumo_compositor_run(struct lumo_compositor *compositor) {
    const char *session_name = lumo_default_session_name();
    const char *socket_name = lumo_default_socket_name();
    const char *socket = NULL;

    if (compositor == NULL || compositor->display == NULL) {
        return -1;
    }

    if (compositor->config != NULL) {
        if (compositor->config->session_name != NULL) {
            session_name = compositor->config->session_name;
        }
        if (compositor->config->socket_name != NULL) {
            socket_name = compositor->config->socket_name;
        }
    }

    if (lumo_backend_start(compositor) != 0) {
        return -1;
    }
    if (lumo_output_start(compositor) != 0) {
        return -1;
    }
    if (lumo_protocol_start(compositor) != 0) {
        return -1;
    }
    if (lumo_input_start(compositor) != 0) {
        return -1;
    }

    if (socket_name[0] != '\0') {
        if (wl_display_add_socket(compositor->display, socket_name) != 0) {
            wlr_log_errno(WLR_ERROR,
                "failed to add Wayland socket '%s'", socket_name);
            return -1;
        }
        socket = socket_name;
    } else {
        socket = wl_display_add_socket_auto(compositor->display);
        if (socket == NULL) {
            wlr_log_errno(WLR_ERROR, "failed to add Wayland socket");
            return -1;
        }
    }

    if (!wlr_backend_start(compositor->backend)) {
        wlr_log(WLR_ERROR, "backend: failed to start wlroots backend");
        return -1;
    }

    if (setenv("WAYLAND_DISPLAY", socket, true) != 0) {
        wlr_log_errno(WLR_ERROR, "failed to export WAYLAND_DISPLAY");
        return -1;
    }

    compositor->running = true;
    wlr_log(WLR_INFO,
        "lumo compositor session=%s socket=%s rotation=%s",
        session_name,
        socket,
        lumo_rotation_name(compositor->active_rotation));

    wl_display_run(compositor->display);
    compositor->running = false;
    return 0;
}

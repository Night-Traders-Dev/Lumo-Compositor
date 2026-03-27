#include "lumo/compositor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void lumo_notify_ready(void) {
    const char *socket_path = getenv("NOTIFY_SOCKET");
    struct sockaddr_un addr;
    int fd;
    ssize_t sent;

    if (socket_path == NULL || socket_path[0] == '\0') {
        wlr_log(WLR_INFO, "sd_notify: NOTIFY_SOCKET not set, skipping");
        return;
    }

    wlr_log(WLR_INFO, "sd_notify: sending READY=1 to %s", socket_path);

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (socket_path[0] == '@') {
        addr.sun_path[0] = '\0';
        strncpy(addr.sun_path + 1, socket_path + 1, sizeof(addr.sun_path) - 2);
    } else {
        strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    }

    sent = sendto(fd, "READY=1", 7, MSG_NOSIGNAL,
        (const struct sockaddr *)&addr,
        offsetof(struct sockaddr_un, sun_path) + strlen(socket_path));
    if (sent < 0) {
        wlr_log_errno(WLR_ERROR, "sd_notify: sendto failed");
    } else {
        wlr_log(WLR_INFO, "sd_notify: sent %zd bytes", sent);
    }
    close(fd);
}

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
    compositor->touch_audit_active = false;
    compositor->touch_audit_saved = false;
    compositor->touch_audit_step = 0;
    compositor->touch_audit_completed_mask = 0;
    compositor->touch_audit_profile_name[0] = '\0';
    compositor->touch_audit_device_name[0] = '\0';
    compositor->touch_audit_device_vendor = 0;
    compositor->touch_audit_device_product = 0;
    compositor->touch_debug_active = false;
    compositor->touch_debug_id = -1;
    compositor->touch_debug_lx = 0.0;
    compositor->touch_debug_ly = 0.0;
    compositor->touch_debug_target = LUMO_TOUCH_TARGET_NONE;
    compositor->touch_debug_phase = LUMO_TOUCH_SAMPLE_CANCEL;
    compositor->touch_debug_hitbox_kind = LUMO_HITBOX_CUSTOM;
    compositor->layer_config_dirty = true;
    compositor->input_state = NULL;
    compositor->protocol_state = NULL;
    compositor->xwayland = NULL;
    compositor->xwayland_ready = false;
    compositor->xwayland_workarea_valid = false;
    compositor->shell_state = NULL;

    wl_list_init(&compositor->outputs);
    wl_list_init(&compositor->keyboards);
    wl_list_init(&compositor->toplevels);
    wl_list_init(&compositor->popups);
    wl_list_init(&compositor->layer_surfaces);
    wl_list_init(&compositor->hitboxes);
    wl_list_init(&compositor->input_devices);
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
    lumo_shell_autostart_stop(compositor);

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

    if (lumo_shell_autostart_start(compositor) != 0) {
        wlr_log(WLR_ERROR, "shell: failed to autostart shell clients");
        return -1;
    }

    compositor->running = true;
    lumo_notify_ready();
    wlr_log(WLR_INFO,
        "lumo compositor session=%s socket=%s rotation=%s",
        session_name,
        socket,
        lumo_rotation_name(compositor->active_rotation));

    wl_display_run(compositor->display);
    compositor->running = false;
    return 0;
}

/*
 * shell_protocol_client.c — protocol/state management for shell client.
 *
 * Extracted from shell_client.c. Handles the state socket connection,
 * protocol frame sending/receiving, and compositor state application.
 */
#include "shell_client_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* ── request senders ──────────────────────────────────────────────── */

bool lumo_shell_client_send_frame(
    struct lumo_shell_client *client,
    const struct lumo_shell_protocol_frame *frame
) {
    char buffer[1024];
    size_t length;
    size_t offset = 0;

    if (client == NULL || frame == NULL || client->state_fd < 0) {
        return false;
    }

    length = lumo_shell_protocol_frame_format(frame, buffer, sizeof(buffer));
    if (length == 0) {
        return false;
    }

    while (offset < length) {
        ssize_t bytes = send(client->state_fd, buffer + offset, length - offset,
            MSG_NOSIGNAL);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return false;
            }
            return false;
        }
        if (bytes == 0) {
            return false;
        }
        offset += (size_t)bytes;
    }

    return true;
}

void lumo_shell_client_send_cycle_rotation(
    struct lumo_shell_client *client
) {
    struct lumo_shell_protocol_frame frame;

    if (client == NULL) {
        return;
    }

    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "cycle_rotation",
            client->next_request_id++)) {
        return;
    }

    (void)lumo_shell_client_send_frame(client, &frame);
    fprintf(stderr, "lumo-shell: sent cycle_rotation request\n");
}

void lumo_shell_client_send_reload(struct lumo_shell_client *client) {
    struct lumo_shell_protocol_frame frame;

    if (client == NULL) {
        return;
    }

    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "reload_session",
            client->next_request_id++)) {
        return;
    }

    (void)lumo_shell_client_send_frame(client, &frame);
    fprintf(stderr, "lumo-shell: sent reload_session request\n");
}

void lumo_shell_client_send_capture_screenshot(
    struct lumo_shell_client *client
) {
    struct lumo_shell_protocol_frame frame;

    if (client == NULL) {
        return;
    }

    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "capture_screenshot",
            client->next_request_id++)) {
        return;
    }

    (void)lumo_shell_client_send_frame(client, &frame);
    fprintf(stderr, "lumo-shell: sent capture_screenshot request\n");
}

void lumo_shell_send_set_u32(
    struct lumo_shell_client *client,
    const char *name,
    const char *key,
    uint32_t value
) {
    struct lumo_shell_protocol_frame frame;
    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, name,
            client->next_request_id++)) {
        return;
    }
    lumo_shell_protocol_frame_add_u32(&frame, key, value);
    (void)lumo_shell_client_send_frame(client, &frame);
}

void lumo_shell_client_send_focus_app(
    struct lumo_shell_client *client, uint32_t index
) {
    struct lumo_shell_protocol_frame frame;
    if (client == NULL) return;
    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "focus_app",
            client->next_request_id++))
        return;
    lumo_shell_protocol_frame_add_u32(&frame, "index", index);
    (void)lumo_shell_client_send_frame(client, &frame);
}

void lumo_shell_client_send_close_app(
    struct lumo_shell_client *client, uint32_t index
) {
    struct lumo_shell_protocol_frame frame;
    if (client == NULL) return;
    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "close_app",
            client->next_request_id++))
        return;
    lumo_shell_protocol_frame_add_u32(&frame, "index", index);
    (void)lumo_shell_client_send_frame(client, &frame);
}

void lumo_shell_client_send_minimize_focused(
    struct lumo_shell_client *client
) {
    struct lumo_shell_protocol_frame frame;
    if (client == NULL) return;
    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "minimize_focused",
            client->next_request_id++))
        return;
    (void)lumo_shell_client_send_frame(client, &frame);
}

void lumo_shell_client_send_open_drawer(
    struct lumo_shell_client *client
) {
    struct lumo_shell_protocol_frame frame;
    if (client == NULL) return;
    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "open_drawer",
            client->next_request_id++))
        return;
    (void)lumo_shell_client_send_frame(client, &frame);
}

/* ── state parsing helpers (static) ───────────────────────────────── */

static bool lumo_shell_remote_scrim_parse(
    const char *value,
    enum lumo_shell_remote_scrim_state *state
) {
    if (value == NULL || state == NULL) {
        return false;
    }

    if (strcmp(value, "hidden") == 0) {
        *state = LUMO_SHELL_REMOTE_SCRIM_HIDDEN;
        return true;
    }
    if (strcmp(value, "dimmed") == 0) {
        *state = LUMO_SHELL_REMOTE_SCRIM_DIMMED;
        return true;
    }
    if (strcmp(value, "modal") == 0) {
        *state = LUMO_SHELL_REMOTE_SCRIM_MODAL;
        return true;
    }

    return false;
}

static bool lumo_shell_rotation_parse(const char *value, uint32_t *rotation) {
    if (value == NULL || rotation == NULL) {
        return false;
    }

    if (strcmp(value, "normal") == 0 || strcmp(value, "0") == 0) {
        *rotation = 0;
        return true;
    }
    if (strcmp(value, "90") == 0) {
        *rotation = 90;
        return true;
    }
    if (strcmp(value, "180") == 0) {
        *rotation = 180;
        return true;
    }
    if (strcmp(value, "270") == 0) {
        *rotation = 270;
        return true;
    }

    return false;
}

/* ── state application (static) ───────────────────────────────────── */

static void lumo_shell_client_apply_state_frame(
    struct lumo_shell_client *client,
    const struct lumo_shell_protocol_frame *frame
) {
    bool bool_value;
    uint32_t rotation_value;
    uint32_t output_size;
    double double_value;
    uint32_t timeout_value;
    bool changed = false;
    bool layout_changed = false;
    const char *value;

    if (client == NULL || frame == NULL) {
        return;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "launcher_visible",
            &bool_value)) {
        if (client->compositor_launcher_visible != bool_value) {
            client->compositor_launcher_visible = bool_value;
            /* clear search when drawer closes */
            if (!bool_value) {
                client->search_active = false;
                client->search_query[0] = '\0';
                client->search_len = 0;
            }
            fprintf(stderr, "lumo-shell: launcher visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "keyboard_visible",
            &bool_value)) {
        if (client->compositor_keyboard_visible != bool_value) {
            client->compositor_keyboard_visible = bool_value;
            fprintf(stderr, "lumo-shell: keyboard visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "osk_shift",
            &bool_value)) {
        if (client->compositor_osk_shift_active != bool_value) {
            client->compositor_osk_shift_active = bool_value;
            changed = true;
        }
    }

    {
        uint32_t osk_page_value;
        if (lumo_shell_protocol_frame_get_u32(frame, "osk_page",
                &osk_page_value)) {
            if (lumo_shell_osk_get_page() != osk_page_value) {
                lumo_shell_osk_set_page(osk_page_value);
                changed = true;
            }
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "quick_settings_visible",
            &bool_value)) {
        if (client->compositor_quick_settings_visible != bool_value) {
            client->compositor_quick_settings_visible = bool_value;
            fprintf(stderr, "lumo-shell: quick_settings visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
            if (client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND || client->mode == LUMO_SHELL_MODE_BACKGROUND) {
                layout_changed = true;
            }
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "time_panel_visible",
            &bool_value)) {
        if (client->compositor_time_panel_visible != bool_value) {
            client->compositor_time_panel_visible = bool_value;
            fprintf(stderr, "lumo-shell: time_panel visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
            if (client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND || client->mode == LUMO_SHELL_MODE_BACKGROUND) {
                layout_changed = true;
            }
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame,
            "notification_panel_visible", &bool_value)) {
        if (client->compositor_notification_panel_visible != bool_value) {
            client->compositor_notification_panel_visible = bool_value;
            changed = true;
            if (client->mode == LUMO_SHELL_MODE_STATUS ||
                    client->mode == LUMO_SHELL_MODE_LAUNCHER) {
                layout_changed = true;
            }
        }
    }

    {
        uint32_t nc = 0;
        if (lumo_shell_protocol_frame_get_u32(frame, "notification_count",
                &nc) && nc <= 8) {
            client->notification_count = (int)nc;
            for (int i = 0; i < (int)nc; i++) {
                char key[24];
                const char *nval = NULL;
                snprintf(key, sizeof(key), "notif_%d", i);
                if (lumo_shell_protocol_frame_get(frame, key, &nval) &&
                        nval != NULL) {
                    snprintf(client->notifications[i], 128, "%s", nval);
                }
            }
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "sidebar_visible",
            &bool_value)) {
        if (client->compositor_sidebar_visible != bool_value) {
            client->compositor_sidebar_visible = bool_value;
            fprintf(stderr, "lumo-shell: sidebar visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
        }
    }

    /* running apps list for sidebar */
    {
        uint32_t rc = 0;
        if (lumo_shell_protocol_frame_get_u32(frame, "running_app_count",
                &rc) && rc <= 16) {
            client->running_app_count = rc;
            for (uint32_t i = 0; i < rc; i++) {
                char key[32];
                const char *aval = NULL;
                snprintf(key, sizeof(key), "running_app_%u", i);
                if (lumo_shell_protocol_frame_get(frame, key, &aval) &&
                        aval != NULL) {
                    snprintf(client->running_app_ids[i], 64, "%s", aval);
                }
                snprintf(key, sizeof(key), "running_title_%u", i);
                if (lumo_shell_protocol_frame_get(frame, key, &aval) &&
                        aval != NULL) {
                    snprintf(client->running_app_titles[i], 64, "%s", aval);
                }
            }
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get(frame, "scrim_state", &value)) {
        if (lumo_shell_remote_scrim_parse(value,
                &client->compositor_scrim_state)) {
            fprintf(stderr, "lumo-shell: scrim state=%s\n", value);
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get(frame, "rotation", &value)) {
        if (lumo_shell_rotation_parse(value, &rotation_value)) {
            client->compositor_rotation_degrees = rotation_value;
            fprintf(stderr, "lumo-shell: compositor rotation=%u\n",
                client->compositor_rotation_degrees);
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_double(frame, "gesture_threshold",
            &double_value)) {
        client->compositor_gesture_threshold = double_value;
        fprintf(stderr, "lumo-shell: gesture threshold=%.2f\n", double_value);
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "gesture_timeout_ms",
            &timeout_value)) {
        client->compositor_gesture_timeout_ms = timeout_value;
        fprintf(stderr, "lumo-shell: gesture timeout_ms=%u\n", timeout_value);
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "keyboard_resize_pending",
            &bool_value)) {
        client->compositor_keyboard_resize_pending = bool_value;
        fprintf(stderr, "lumo-shell: keyboard resize pending=%s\n",
            bool_value ? "true" : "false");
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "keyboard_resize_acked",
            &bool_value)) {
        client->compositor_keyboard_resize_acked = bool_value;
        fprintf(stderr, "lumo-shell: keyboard resize acked=%s\n",
            bool_value ? "true" : "false");
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "keyboard_resize_serial",
            &timeout_value)) {
        client->compositor_keyboard_resize_serial = timeout_value;
        fprintf(stderr, "lumo-shell: keyboard resize serial=%u\n",
            timeout_value);
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "touch_audit_active",
            &bool_value) &&
            client->compositor_touch_audit_active != bool_value) {
        client->compositor_touch_audit_active = bool_value;
        fprintf(stderr, "lumo-shell: touch audit active=%s\n",
            bool_value ? "true" : "false");
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "touch_audit_saved",
            &bool_value) &&
            client->compositor_touch_audit_saved != bool_value) {
        client->compositor_touch_audit_saved = bool_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "touch_audit_step",
            &timeout_value) &&
            client->compositor_touch_audit_step != timeout_value) {
        client->compositor_touch_audit_step = timeout_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "touch_audit_completed_mask",
            &timeout_value) &&
            client->compositor_touch_audit_completed_mask != timeout_value) {
        client->compositor_touch_audit_completed_mask = timeout_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get(frame, "touch_audit_profile", &value) &&
            strcmp(client->compositor_touch_audit_profile, value) != 0) {
        snprintf(client->compositor_touch_audit_profile,
            sizeof(client->compositor_touch_audit_profile), "%s", value);
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "touch_debug_active",
            &bool_value) &&
            client->touch_debug_active != bool_value) {
        client->touch_debug_active = bool_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "touch_debug_id",
            &timeout_value) &&
            client->touch_debug_id != timeout_value) {
        client->touch_debug_id = timeout_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_double(frame, "touch_debug_x",
            &double_value) &&
            client->touch_debug_x != double_value) {
        client->touch_debug_x = double_value;
        client->touch_debug_seen = true;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_double(frame, "touch_debug_y",
            &double_value) &&
            client->touch_debug_y != double_value) {
        client->touch_debug_y = double_value;
        client->touch_debug_seen = true;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get(frame, "touch_debug_phase", &value)) {
        enum lumo_shell_touch_debug_phase phase =
            LUMO_SHELL_TOUCH_DEBUG_NONE;

        if (lumo_shell_touch_debug_phase_parse(value, &phase) &&
                client->touch_debug_phase != phase) {
            client->touch_debug_phase = phase;
            client->touch_debug_seen = true;
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get(frame, "touch_debug_target", &value)) {
        enum lumo_shell_touch_debug_target target =
            LUMO_SHELL_TOUCH_DEBUG_TARGET_NONE;

        if (lumo_shell_touch_debug_target_parse(value, &target) &&
                client->touch_debug_target != target) {
            client->touch_debug_target = target;
            client->touch_debug_seen = true;
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "output_width", &output_size) &&
            client->output_width_hint != output_size) {
        client->output_width_hint = output_size;
        layout_changed = true;
        fprintf(stderr, "lumo-shell: output width=%u\n", output_size);
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "output_height", &output_size) &&
            client->output_height_hint != output_size) {
        client->output_height_hint = output_size;
        layout_changed = true;
        fprintf(stderr, "lumo-shell: output height=%u\n", output_size);

        if (client->mode == LUMO_SHELL_MODE_BACKGROUND ||
                client->mode == LUMO_SHELL_MODE_GESTURE ||
                client->mode == LUMO_SHELL_MODE_STATUS) {
            if (client->layer_surface != NULL && client->surface != NULL) {
                wl_surface_commit(client->surface);
            }
        }
    }

    if (lumo_shell_protocol_frame_get(frame, "weather_condition", &value)) {
        if (strcmp(client->weather_condition, value) != 0) {
            strncpy(client->weather_condition, value,
                sizeof(client->weather_condition) - 1);
            changed = true;
        }
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "weather_code",
            &timeout_value)) {
        if (client->weather_code != (int)timeout_value) {
            client->weather_code = (int)timeout_value;
            changed = true;
        }
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "weather_temp",
            &timeout_value)) {
        int temp = (int)timeout_value - 100;
        if (client->weather_temp_c != temp) {
            client->weather_temp_c = temp;
            changed = true;
        }
    }
    if (lumo_shell_protocol_frame_get(frame, "weather_humidity", &value)) {
        strncpy(client->weather_humidity, value,
            sizeof(client->weather_humidity) - 1);
    }
    if (lumo_shell_protocol_frame_get(frame, "weather_wind", &value)) {
        strncpy(client->weather_wind, value,
            sizeof(client->weather_wind) - 1);
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "volume_pct",
            &timeout_value)) {
        client->volume_pct = timeout_value;
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "brightness_pct",
            &timeout_value)) {
        client->brightness_pct = timeout_value;
    }
    if (lumo_shell_protocol_frame_get(frame, "toast_msg", &value)) {
        strncpy(client->toast_message, value,
            sizeof(client->toast_message) - 1);
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "toast_time",
            &timeout_value)) {
        if (client->toast_time_low != timeout_value) {
            client->toast_time_low = timeout_value;
            changed = true; /* trigger redraw for new toast */
        }
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "toast_dur",
            &timeout_value)) {
        client->toast_duration_ms = timeout_value;
    }

    if (lumo_shell_protocol_frame_get(frame, "status", &value) &&
            strcmp(value, "ok") == 0) {
        fprintf(stderr, "lumo-shell: compositor response ok for %s\n",
            frame->name);
    }

    if (lumo_shell_protocol_frame_get(frame, "code", &value)) {
        fprintf(stderr, "lumo-shell: compositor response %s code=%s\n",
            frame->name, value);
    }

    if (changed || layout_changed) {
        if (client->unified) {
            for (int i = 0; i < client->surface_count; i++) {
                struct lumo_shell_surface_slot *slot = &client->slots[i];
                slot->dirty = true;
            }
            lumo_shell_client_redraw_unified(client);
        } else {
            lumo_shell_client_sync_surface_state(client, layout_changed);
            /* roundtrip to receive the layer surface configure response
             * before attempting redraw — without this, the surface may
             * not be configured at its new dimensions and the redraw
             * silently fails */
            if (client->display != NULL) {
                wl_display_roundtrip(client->display);
            }
            (void)lumo_shell_client_redraw(client);
            /* second redraw attempt after another dispatch in case the
             * first one was too early */
            if (client->display != NULL) {
                wl_display_flush(client->display);
                wl_display_dispatch_pending(client->display);
            }
            (void)lumo_shell_client_redraw(client);
        }
    }
}

/* ── protocol dispatching ─────────────────────────────────────────── */

static void lumo_shell_client_handle_protocol_frame(
    const struct lumo_shell_protocol_frame *frame,
    void *data
) {
    struct lumo_shell_client *client = data;
    const char *value = NULL;

    if (client == NULL || frame == NULL) {
        return;
    }

    if (frame->kind == LUMO_SHELL_PROTOCOL_FRAME_EVENT &&
            strcmp(frame->name, "state") == 0) {
        lumo_shell_client_apply_state_frame(client, frame);
        return;
    }

    if (frame->kind == LUMO_SHELL_PROTOCOL_FRAME_RESPONSE) {
        if (lumo_shell_protocol_frame_get(frame, "status", &value) &&
                strcmp(value, "ok") == 0) {
            fprintf(stderr, "lumo-shell: request %s acknowledged\n",
                frame->name);
        }
        return;
    }

    if (frame->kind == LUMO_SHELL_PROTOCOL_FRAME_ERROR) {
        const char *code = NULL;
        const char *reason = NULL;

        (void)lumo_shell_protocol_frame_get(frame, "code", &code);
        (void)lumo_shell_protocol_frame_get(frame, "reason", &reason);
        fprintf(stderr,
            "lumo-shell: compositor error for %s code=%s reason=%s\n",
            frame->name,
            code != NULL ? code : "unknown",
            reason != NULL ? reason : "unknown");
    }
}

bool lumo_shell_client_pump_protocol(struct lumo_shell_client *client) {
    char chunk[128];
    ssize_t bytes_read;

    if (client == NULL || client->state_fd < 0) {
        return false;
    }

    for (;;) {
        bytes_read = recv(client->state_fd, chunk, sizeof(chunk), 0);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return true;
            }
            return false;
        }

        if (bytes_read == 0) {
            return false;
        }

        if (!lumo_shell_protocol_stream_feed(&client->protocol_stream, chunk,
                (size_t)bytes_read, lumo_shell_client_handle_protocol_frame,
                client)) {
            fprintf(stderr, "lumo-shell: invalid protocol frame from compositor\n");
            return false;
        }
    }
}

/* ── socket connection ────────────────────────────────────────────── */

int lumo_shell_client_connect_state_socket(
    struct lumo_shell_client *client
) {
    const char *socket_path;
    struct sockaddr_un address;
    int fd;
    int flags;

    if (client == NULL) {
        return -1;
    }

    socket_path = getenv("LUMO_STATE_SOCKET");
    if (socket_path == NULL || socket_path[0] == '\0') {
        return -1;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s",
            socket_path) >= (int)sizeof(address.sun_path)) {
        close(fd);
        return -1;
    }

    /* retry connection up to 5 times with 200ms delays — the compositor
     * socket may not be ready immediately after shell client launch */
    for (int attempt = 0; attempt < 5; attempt++) {
        if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0) {
            break;
        }
        if (attempt == 4) {
            fprintf(stderr, "lumo-shell: state socket connect failed after "
                "5 attempts\n");
            close(fd);
            return -1;
        }
        {
            struct timespec ts = {0, 200000000};
            nanosleep(&ts, NULL);
        }
        /* need a new fd for each retry on UNIX sockets */
        close(fd);
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
    }

    flags = fcntl(fd, F_GETFD);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(fd, F_GETFL);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    snprintf(client->state_socket_path, sizeof(client->state_socket_path),
        "%s", socket_path);
    return fd;
}

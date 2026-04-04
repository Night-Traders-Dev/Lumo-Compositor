/*
 * shell_bridge.c — bridge transport and request dispatch for the
 * Lumo compositor shell.  Split from shell_launch.c for maintainability.
 */

#define _DEFAULT_SOURCE
#include "shell_launch_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static bool lumo_shell_bridge_write_all(
    int fd,
    const char *buffer,
    size_t length
) {
    size_t offset = 0;

    if (fd < 0 || buffer == NULL || length == 0) {
        return false;
    }

    while (offset < length) {
        ssize_t bytes = send(fd, buffer + offset, length - offset, MSG_NOSIGNAL);
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* non-blocking socket buffer full — wait briefly and retry
                 * rather than disconnecting the client */
                struct pollfd pfd = {fd, POLLOUT, 0};
                if (poll(&pfd, 1, 50) > 0) {
                    continue;
                }
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

static bool lumo_shell_bridge_send_frame(
    int fd,
    const struct lumo_shell_protocol_frame *frame
) {
    char buffer[1024];
    size_t length;

    if (fd < 0 || frame == NULL) {
        return false;
    }

    length = lumo_shell_protocol_frame_format(frame, buffer, sizeof(buffer));
    if (length == 0) {
        return false;
    }

    return lumo_shell_bridge_write_all(fd, buffer, length);
}

bool lumo_shell_bridge_output_size(
    struct lumo_compositor *compositor,
    uint32_t *width,
    uint32_t *height
) {
    struct lumo_output *output;
    int effective_width = 0;
    int effective_height = 0;

    if (compositor == NULL || width == NULL || height == NULL ||
            wl_list_empty(&compositor->outputs)) {
        return false;
    }

    output = wl_container_of(compositor->outputs.next, output, link);
    if (output == NULL || output->wlr_output == NULL) {
        return false;
    }

    wlr_output_effective_resolution(output->wlr_output, &effective_width,
        &effective_height);
    if (effective_width <= 0 || effective_height <= 0) {
        return false;
    }

    *width = (uint32_t)effective_width;
    *height = (uint32_t)effective_height;
    return true;
}

static bool lumo_shell_bridge_build_state_frame(
    struct lumo_compositor *compositor,
    struct lumo_shell_protocol_frame *frame
) {
    uint32_t output_width = 0;
    uint32_t output_height = 0;

    if (compositor == NULL || frame == NULL) {
        return false;
    }

    if (!lumo_shell_protocol_frame_init(frame,
            LUMO_SHELL_PROTOCOL_FRAME_EVENT, "state", 0)) {
        return false;
    }

    if (!lumo_shell_protocol_frame_add_bool(frame, "launcher_visible",
            compositor->launcher_visible)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "keyboard_visible",
            compositor->keyboard_visible)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "osk_shift",
            compositor->osk_shift_active)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_u32(frame, "osk_page",
            lumo_shell_osk_get_page())) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "quick_settings_visible",
            compositor->quick_settings_visible)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "time_panel_visible",
            compositor->time_panel_visible)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame,
            "notification_panel_visible",
            compositor->notification_panel_visible)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "sidebar_visible",
            compositor->sidebar_visible)) {
        return false;
    }
    /* running apps list for sidebar */
    {
        uint32_t app_count = 0;
        struct lumo_toplevel *tl;
        wl_list_for_each(tl, &compositor->toplevels, link) {
            if (app_count >= 16) break;
            const char *app_id = tl->xdg_toplevel->app_id;
            const char *title = tl->xdg_toplevel->title;
            char key[32];
            snprintf(key, sizeof(key), "running_app_%u", app_count);
            lumo_shell_protocol_frame_add_string(frame, key,
                app_id ? app_id : "unknown");
            snprintf(key, sizeof(key), "running_title_%u", app_count);
            lumo_shell_protocol_frame_add_string(frame, key,
                title ? title : "");
            app_count++;
        }
        lumo_shell_protocol_frame_add_u32(frame, "running_app_count",
            app_count);
    }
    {
        int nc = compositor->notification_count;
        if (nc > 8) nc = 8;
        if (!lumo_shell_protocol_frame_add_u32(frame, "notification_count",
                (uint32_t)nc)) {
            return false;
        }
        for (int i = 0; i < nc; i++) {
            char key[24];
            snprintf(key, sizeof(key), "notif_%d", i);
            if (!lumo_shell_protocol_frame_add_string(frame, key,
                    compositor->notifications[i])) {
                return false;
            }
        }
    }
    if (!lumo_shell_protocol_frame_add_string(frame, "scrim_state",
            lumo_scrim_state_name(compositor->scrim_state))) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_string(frame, "rotation",
            lumo_rotation_name(compositor->active_rotation))) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_double(frame, "gesture_threshold",
            compositor->gesture_threshold)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_u32(frame, "gesture_timeout_ms",
            compositor->gesture_timeout_ms)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "keyboard_resize_pending",
            compositor->keyboard_resize_pending)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_u32(frame, "keyboard_resize_serial",
            compositor->keyboard_resize_serial)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "keyboard_resize_acked",
            compositor->keyboard_resize_acked)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "touch_audit_active",
            compositor->touch_audit_active) ||
            !lumo_shell_protocol_frame_add_u32(frame, "touch_audit_step",
                compositor->touch_audit_step) ||
            !lumo_shell_protocol_frame_add_u32(frame,
                "touch_audit_completed_mask",
                compositor->touch_audit_completed_mask) ||
            !lumo_shell_protocol_frame_add_bool(frame, "touch_audit_saved",
                compositor->touch_audit_saved) ||
            !lumo_shell_protocol_frame_add_string(frame, "touch_audit_profile",
                compositor->touch_audit_profile_name[0] != '\0'
                    ? compositor->touch_audit_profile_name
                    : "none")) {
        return false;
    }
    if (lumo_shell_bridge_output_size(compositor, &output_width, &output_height)) {
        if (!lumo_shell_protocol_frame_add_u32(frame, "output_width",
                output_width) ||
                !lumo_shell_protocol_frame_add_u32(frame, "output_height",
                    output_height)) {
            return false;
        }
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "touch_debug_active",
            compositor->touch_debug_active) ||
            !lumo_shell_protocol_frame_add_u32(frame, "touch_debug_id",
                compositor->touch_debug_id >= 0
                    ? (uint32_t)compositor->touch_debug_id
                    : UINT32_MAX) ||
            !lumo_shell_protocol_frame_add_double(frame, "touch_debug_x",
                compositor->touch_debug_lx) ||
            !lumo_shell_protocol_frame_add_double(frame, "touch_debug_y",
                compositor->touch_debug_ly) ||
            !lumo_shell_protocol_frame_add_string(frame, "touch_debug_phase",
                lumo_touch_sample_type_name(compositor->touch_debug_phase)) ||
            !lumo_shell_protocol_frame_add_string(frame, "touch_debug_target",
                lumo_touch_target_kind_name(compositor->touch_debug_target)) ||
            !lumo_shell_protocol_frame_add_string(frame, "touch_debug_hitbox",
                compositor->touch_debug_target == LUMO_TOUCH_TARGET_HITBOX
                    ? lumo_hitbox_kind_name(compositor->touch_debug_hitbox_kind)
                    : "none")) {
        return false;
    }

    if (!lumo_shell_protocol_frame_add_string(frame, "weather_condition",
                compositor->weather_condition[0] != '\0'
                    ? compositor->weather_condition : "unknown") ||
            !lumo_shell_protocol_frame_add_u32(frame, "weather_code",
                (uint32_t)compositor->weather_code) ||
            !lumo_shell_protocol_frame_add_u32(frame, "weather_temp",
                (uint32_t)(compositor->weather_temp_c + 100)) ||
            !lumo_shell_protocol_frame_add_string(frame, "weather_humidity",
                compositor->weather_humidity[0] != '\0'
                    ? compositor->weather_humidity : "--") ||
            !lumo_shell_protocol_frame_add_string(frame, "weather_wind",
                compositor->weather_wind[0] != '\0'
                    ? compositor->weather_wind : "--") ||
            !lumo_shell_protocol_frame_add_u32(frame, "volume_pct",
                compositor->volume_pct) ||
            !lumo_shell_protocol_frame_add_u32(frame, "brightness_pct",
                compositor->brightness_pct) ||
            !lumo_shell_protocol_frame_add_string(frame, "toast_msg",
                compositor->toast_message[0] != '\0'
                    ? compositor->toast_message : "-") ||
            !lumo_shell_protocol_frame_add_u32(frame, "toast_time",
                (uint32_t)(compositor->toast_show_time_ms & 0xFFFFFFFF)) ||
            !lumo_shell_protocol_frame_add_u32(frame, "toast_dur",
                compositor->toast_duration_ms)) {
        return false;
    }

    return true;
}

static void lumo_shell_bridge_send_state_snapshot(
    struct lumo_compositor *compositor,
    int fd
) {
    struct lumo_shell_protocol_frame frame;

    if (!lumo_shell_bridge_build_state_frame(compositor, &frame)) {
        return;
    }

    (void)lumo_shell_bridge_send_frame(fd, &frame);
}

void lumo_shell_bridge_broadcast_state(
    struct lumo_compositor *compositor
) {
    struct lumo_shell_state *state;
    struct lumo_shell_bridge_client *client;
    struct lumo_shell_bridge_client *tmp;
    struct lumo_shell_protocol_frame frame;
    char formatted[2048];
    size_t formatted_len;

    if (compositor == NULL) {
        return;
    }

    state = compositor->shell_state;
    if (state == NULL) {
        return;
    }

    if (!lumo_shell_bridge_build_state_frame(compositor, &frame)) {
        return;
    }

    formatted_len = lumo_shell_protocol_frame_format(&frame, formatted,
        sizeof(formatted));
    if (formatted_len == 0) {
        return;
    }

    wl_list_for_each_safe(client, tmp, &state->bridge.clients, link) {
        if (!lumo_shell_bridge_write_all(client->fd, formatted,
                formatted_len)) {
            lumo_shell_bridge_remove_client(&state->bridge, client);
        }
    }

    state->state_broadcast_pending = false;
}

void lumo_shell_state_broadcast_idle(void *data) {
    struct lumo_compositor *compositor = data;
    struct lumo_shell_state *state;

    if (compositor == NULL) {
        return;
    }

    state = compositor->shell_state;
    if (state == NULL) {
        return;
    }

    state->state_broadcast_source = NULL;
    if (state->state_broadcast_pending) {
        lumo_shell_bridge_broadcast_state(compositor);
    }
}

void lumo_shell_mark_state_dirty(
    struct lumo_compositor *compositor
) {
    struct lumo_shell_state *state;

    if (compositor == NULL) {
        return;
    }

    state = compositor->shell_state;
    if (state != NULL) {
        state->state_broadcast_pending = true;
        if (state->state_broadcast_source == NULL &&
                compositor->event_loop != NULL) {
            state->state_broadcast_source = wl_event_loop_add_idle(
                compositor->event_loop, lumo_shell_state_broadcast_idle,
                compositor);
        }
    }
}

static bool lumo_shell_bridge_send_result(
    struct lumo_shell_bridge_client *client,
    const struct lumo_shell_protocol_frame *request,
    bool success,
    const char *code,
    const char *reason
) {
    struct lumo_shell_protocol_frame frame;

    if (client == NULL || request == NULL) {
        return false;
    }

    if (!lumo_shell_protocol_frame_init(&frame,
            success ? LUMO_SHELL_PROTOCOL_FRAME_RESPONSE
                    : LUMO_SHELL_PROTOCOL_FRAME_ERROR,
            request->name,
            request->id)) {
        return false;
    }

    if (success) {
        if (!lumo_shell_protocol_frame_add_field(&frame, "status", "ok")) {
            return false;
        }
    } else {
        if (!lumo_shell_protocol_frame_add_field(&frame, "status", "error") ||
                !lumo_shell_protocol_frame_add_field(&frame, "code",
                    code != NULL ? code : "error") ||
                !lumo_shell_protocol_frame_add_field(&frame, "reason",
                    reason != NULL ? reason : "unknown")) {
            return false;
        }
    }

    return lumo_shell_bridge_send_frame(client->fd, &frame);
}

static struct wlr_text_input_v3 *lumo_shell_bridge_focused_text_input(
    struct lumo_compositor *compositor
) {
    struct wl_resource *resource;
    struct wlr_surface *focused_surface;

    if (compositor == NULL || compositor->seat == NULL ||
            compositor->text_input_manager == NULL) {
        return NULL;
    }

    focused_surface = compositor->seat->keyboard_state.focused_surface;

    /* first try: exact focused_surface match */
    wl_list_for_each(resource, &compositor->text_input_manager->text_inputs,
            link) {
        struct wlr_text_input_v3 *text_input =
            wl_resource_get_user_data(resource);

        if (text_input == NULL || text_input->seat != compositor->seat) {
            continue;
        }

        if (text_input->focused_surface == focused_surface &&
                focused_surface != NULL) {
            return text_input;
        }
    }

    /* fallback: find any text-input from the same client as the
     * focused surface. This handles the race where enter hasn't
     * been processed but the keyboard was auto-shown by app_id */
    if (focused_surface != NULL) {
        wl_list_for_each(resource,
                &compositor->text_input_manager->text_inputs, link) {
            struct wlr_text_input_v3 *text_input =
                wl_resource_get_user_data(resource);
            if (text_input == NULL || text_input->seat != compositor->seat) {
                continue;
            }
            if (wl_resource_get_client(text_input->resource) ==
                    wl_resource_get_client(focused_surface->resource)) {
                /* force set focused_surface so future commits work */
                if (text_input->focused_surface == NULL) {
                    wlr_text_input_v3_send_enter(text_input, focused_surface);
                    wlr_text_input_v3_send_done(text_input);
                }
                return text_input;
            }
        }
    }

    return NULL;
}

static const char *lumo_shell_bridge_commit_osk_text(
    struct lumo_compositor *compositor,
    uint32_t index,
    const char **reason_out
) {
    struct wlr_text_input_v3 *text_input;
    const char *text;

    if (reason_out != NULL) {
        *reason_out = NULL;
    }

    text = lumo_shell_osk_key_text(index);
    if (text == NULL) {
        if (reason_out != NULL) {
            *reason_out = "osk_key";
        }
        wlr_log(WLR_ERROR, "shell: osk key index %u out of range", index);
        return "invalid_index";
    }

    /* shift key (empty text) - toggle shift state */
    if (text[0] == '\0') {
        compositor->osk_shift_active = !compositor->osk_shift_active;
        wlr_log(WLR_INFO, "shell: osk shift %s",
            compositor->osk_shift_active ? "on" : "off");
        lumo_shell_mark_state_dirty(compositor);
        return NULL;
    }

    /* close-OSK key */
    if (text[0] == '\x1b') {
        wlr_log(WLR_INFO, "shell: osk close requested");
        lumo_protocol_set_keyboard_visible(compositor, false);
        return NULL;
    }

    /* page toggle key (123/ABC) */
    if (text[0] == '\x01') {
        lumo_shell_osk_toggle_page();
        wlr_log(WLR_INFO, "shell: osk page toggled to %u",
            lumo_shell_osk_get_page());
        lumo_shell_mark_state_dirty(compositor);
        return NULL;
    }

    /* apply shift: uppercase the letter and auto-clear shift */
    char shifted_buf[2] = {0};
    if (compositor->osk_shift_active && text[0] >= 'a' && text[0] <= 'z') {
        shifted_buf[0] = text[0] - ('a' - 'A');
        text = shifted_buf;
        compositor->osk_shift_active = false;
        lumo_shell_mark_state_dirty(compositor);
    }

    text_input = lumo_shell_bridge_focused_text_input(compositor);
    if (text_input == NULL) {
        /* if launcher is showing, route key to search */
        if (compositor->launcher_visible) {
            if (text[0] == '\b') {
                /* backspace in search */
                if (compositor->toast_message[0] != '\0') {
                    size_t len = strlen(compositor->toast_message);
                    if (len > 0)
                        compositor->toast_message[len - 1] = '\0';
                }
            } else if (text[0] != '\n') {
                /* append to search query (reuse toast_message field) */
                size_t len = strlen(compositor->toast_message);
                if (len + 1 < sizeof(compositor->toast_message) - 1) {
                    compositor->toast_message[len] = text[0];
                    compositor->toast_message[len + 1] = '\0';
                }
            }
            lumo_shell_mark_state_dirty(compositor);
            wlr_log(WLR_INFO, "shell: osk key routed to search: '%s'",
                compositor->toast_message);
            return NULL;
        }
        /* fallback: if a toplevel is focused, send the key as a
         * virtual keyboard keypress via wlr_seat. This bypasses
         * text-input-v3 entirely — the app receives it as a normal
         * wl_keyboard key event, which the terminal and notes apps
         * already handle in their keyboard listener. */
        if (compositor->seat != NULL &&
                !wl_list_empty(&compositor->toplevels)) {
            struct wlr_keyboard *kbd =
                wlr_seat_get_keyboard(compositor->seat);
            if (kbd != NULL) {
                /* map OSK text to Linux keycode */
                static const struct { char ch; uint32_t code; bool shift; } map[] = {
                    {'a',30,false},{'b',48,false},{'c',46,false},
                    {'d',32,false},{'e',18,false},{'f',33,false},
                    {'g',34,false},{'h',35,false},{'i',23,false},
                    {'j',36,false},{'k',37,false},{'l',38,false},
                    {'m',50,false},{'n',49,false},{'o',24,false},
                    {'p',25,false},{'q',16,false},{'r',19,false},
                    {'s',31,false},{'t',20,false},{'u',22,false},
                    {'v',47,false},{'w',17,false},{'x',45,false},
                    {'y',21,false},{'z',44,false},{' ',57,false},
                    {'\n',28,false},{',',51,false},{'.',52,false},
                    {'/',53,false},{'1',2,false},{'2',3,false},
                    {'3',4,false},{'4',5,false},{'5',6,false},
                    {'6',7,false},{'7',8,false},{'8',9,false},
                    {'9',10,false},{'0',11,false},{'-',12,false},
                    {'=',13,false},
                    {'!',2,true},{'@',3,true},{'#',4,true},
                    {'$',5,true},{'%',6,true},{'^',7,true},
                    {'&',8,true},{'(',10,true},{')',11,true},
                    {'+',13,true},{'?',53,true},{';',39,false},
                    {'\'',40,false},{':',39,true},
                };
                uint32_t keycode = 0;
                char ch = text[0];
                char lower_ch = (ch >= 'A' && ch <= 'Z')
                    ? ch + ('a' - 'A') : ch;
                bool need_shift = ch >= 'A' && ch <= 'Z';
                if (ch == '\b') {
                    keycode = 14; /* KEY_BACKSPACE */
                } else {
                    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
                        if (map[i].ch == lower_ch || map[i].ch == ch) {
                            keycode = map[i].code;
                            if (map[i].shift) need_shift = true;
                            break;
                        }
                    }
                }
                if (keycode > 0) {
                    if (need_shift) {
                        wlr_seat_keyboard_notify_key(compositor->seat,
                            0, 42, WL_KEYBOARD_KEY_STATE_PRESSED);
                    }
                    wlr_seat_keyboard_notify_key(compositor->seat,
                        0, keycode, WL_KEYBOARD_KEY_STATE_PRESSED);
                    wlr_seat_keyboard_notify_key(compositor->seat,
                        1, keycode, WL_KEYBOARD_KEY_STATE_RELEASED);
                    if (need_shift) {
                        wlr_seat_keyboard_notify_key(compositor->seat,
                            1, 42, WL_KEYBOARD_KEY_STATE_RELEASED);
                    }
                    wlr_log(WLR_INFO,
                        "shell: osk key '%c' sent as keycode %u%s",
                        ch > 31 ? ch : '?', keycode,
                        need_shift ? " (shifted)" : "");
                    return NULL;
                }
            }
        }
        if (reason_out != NULL) {
            *reason_out = "text_input_v3";
        }
        wlr_log(WLR_INFO,
            "shell: osk key %u ignored, no focused text input",
            index);
        return "no_text_input_focus";
    }

    /* backspace - delete one character before cursor */
    if (text[0] == '\b') {
        wlr_text_input_v3_send_delete_surrounding_text(text_input, 1, 0);
        wlr_text_input_v3_send_commit_string(text_input, "");
        wlr_text_input_v3_send_done(text_input);
        wlr_log(WLR_INFO, "shell: committed backspace from key %u", index);
        return NULL;
    }

    wlr_text_input_v3_send_commit_string(text_input, text);
    wlr_text_input_v3_send_done(text_input);
    wlr_log(WLR_INFO, "shell: committed osk text '%s' from key %u", text,
        index);
    return NULL;
}

/* Command-injection safety: the `command` / `app_id` values passed here
 * originate exclusively from lumo_shell_launcher_tile_command(), which
 * returns compile-time constants from app_catalog.c.  The bridge protocol
 * only lets the shell client send an activate_target with a validated
 * target kind and a numeric tile index — no arbitrary string is accepted
 * from the client.  There is therefore no path for user-supplied input to
 * reach the execlp() calls below. */
static void lumo_shell_launch_app(
    struct lumo_compositor *compositor,
    const char *command
) {
    pid_t pid;
    struct lumo_shell_state *state = compositor != NULL
        ? compositor->shell_state
        : NULL;
    const char *native_prefix = "lumo-app:";
    const char *app_id = NULL;
    const char *binary = "lumo-app";

    if (command == NULL || command[0] == '\0') {
        wlr_log(WLR_INFO, "shell: no command mapped for this tile");
        return;
    }

    if (strncmp(command, native_prefix, strlen(native_prefix)) == 0) {
        app_id = command + strlen(native_prefix);
        if (state != NULL && state->binary_path[0] != '\0') {
            char app_path[PATH_MAX];
            char parent_directory[PATH_MAX];

            if (lumo_shell_parent_directory(state->binary_path,
                    parent_directory, sizeof(parent_directory)) &&
                    lumo_shell_join_path(app_path, sizeof(app_path),
                        parent_directory, "lumo-app")) {
                binary = app_path;
            }
        }
    }

    /* for non-native commands (e.g. lumo-browser), also try the sibling
     * builddir path so uninstalled sessions work */
    if (app_id == NULL && state != NULL && state->binary_path[0] != '\0') {
        char ext_path[PATH_MAX];
        char parent_directory[PATH_MAX];
        if (lumo_shell_parent_directory(state->binary_path,
                parent_directory, sizeof(parent_directory)) &&
                lumo_shell_join_path(ext_path, sizeof(ext_path),
                    parent_directory, command)) {
            if (access(ext_path, X_OK) == 0) {
                binary = ext_path;
                app_id = NULL; /* use binary directly */
            }
        }
    }

    pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to fork for app launch");
        return;
    }

    if (pid == 0) {
        setsid();
        if (app_id != NULL && app_id[0] != '\0') {
            execlp(binary, binary, "--app", app_id, (char *)NULL);
        } else if (strchr(command, ' ') != NULL) {
            /* command contains arguments — use shell to parse them */
            execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        } else {
            /* try the resolved path first, fall back to PATH lookup */
            if (binary != command) {
                execl(binary, binary, (char *)NULL);
            }
            execlp(command, command, (char *)NULL);
            /* if the external binary doesn't exist, fall back to
             * lumo-app stub so the user sees something */
            if (strncmp(command, "lumo-", 5) == 0) {
                const char *fallback_id = command + 5;
                execlp("lumo-app", "lumo-app", "--app", fallback_id,
                    (char *)NULL);
            }
        }
        _exit(127);
    }

    wlr_log(WLR_INFO, "shell: launched '%s' pid=%d", command, (int)pid);
}

static void lumo_shell_bridge_handle_request_frame(
    struct lumo_shell_bridge_client *client,
    const struct lumo_shell_protocol_frame *frame
) {
    const char *kind_value;
    enum lumo_shell_target_kind target_kind;
    const char *failure_code = NULL;
    const char *failure_reason = NULL;
    uint32_t index = 0;
    bool handled = false;

    if (client == NULL || frame == NULL || client->compositor == NULL) {
        return;
    }

    if (strcmp(frame->name, "activate_target") == 0) {
        if (!lumo_shell_protocol_frame_get(frame, "kind", &kind_value) ||
                !lumo_shell_target_kind_parse(kind_value, &target_kind)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "invalid_kind", "missing_kind");
            return;
        }

        (void)lumo_shell_protocol_frame_get_u32(frame, "index", &index);

        switch (target_kind) {
        case LUMO_SHELL_TARGET_LAUNCHER_TILE: {
            const char *app_command = lumo_shell_launcher_tile_command(index);
            const char *tile_label = lumo_shell_launcher_tile_label(index);
            wlr_log(WLR_INFO,
                "shell: activate_target launcher tile %u requested (cmd=%s)",
                index,
                app_command != NULL ? app_command : "none");
            if (app_command != NULL) {
                /* check if app is already running — if so, focus it
                 * instead of launching a duplicate.  Exception:
                 * terminal and browser allow multiple windows. */
                bool already_running = false;
                bool allow_multi = (tile_label != NULL &&
                    (strcmp(tile_label, "Terminal") == 0 ||
                     strcmp(tile_label, "Browser") == 0));
                if (!allow_multi) {
                    struct lumo_toplevel *tl;
                    wl_list_for_each(tl, &client->compositor->toplevels,
                            link) {
                        const char *aid = tl->xdg_toplevel->app_id;
                        /* match: "lumo-app:browser" → app_id "lumo-browser" */
                        if (aid != NULL && app_command != NULL) {
                            /* extract name from command "lumo-app:NAME" */
                            const char *colon = strchr(app_command, ':');
                            const char *cmd_name = colon ? colon + 1 : app_command;
                            char expected_id[64];
                            snprintf(expected_id, sizeof(expected_id),
                                "lumo-%s", cmd_name);
                            if (strcmp(aid, expected_id) == 0) {
                                /* focus existing instance */
                                tl->scene_tree->node.enabled = true;
                                wlr_scene_node_raise_to_top(
                                    &tl->scene_tree->node);
                                wlr_seat_keyboard_notify_enter(
                                    client->compositor->seat,
                                    tl->xdg_toplevel->base->surface,
                                    NULL, 0, NULL);
                                lumo_protocol_set_scrim_state(
                                    client->compositor, LUMO_SCRIM_DIMMED);
                                already_running = true;
                                wlr_log(WLR_INFO,
                                    "shell: focused existing %s", aid);
                                break;
                            }
                        }
                    }
                }
                if (!already_running)
                    lumo_shell_launch_app(client->compositor, app_command);
            }
            lumo_protocol_set_launcher_visible(client->compositor, false);
            handled = true;
            break;
        }
        case LUMO_SHELL_TARGET_LAUNCHER_CLOSE:
            wlr_log(WLR_INFO,
                "shell: activate_target launcher close requested");
            lumo_protocol_set_launcher_visible(client->compositor, false);
            handled = true;
            break;
        case LUMO_SHELL_TARGET_GESTURE_HANDLE:
            wlr_log(WLR_INFO,
                "shell: activate_target gesture handle → go home");
            /* dismiss everything and minimize all apps (go home) */
            if (client->compositor->notification_panel_visible)
                lumo_protocol_set_notification_panel_visible(
                    client->compositor, false);
            if (client->compositor->time_panel_visible)
                lumo_protocol_set_time_panel_visible(client->compositor,
                    false);
            if (client->compositor->quick_settings_visible)
                lumo_protocol_set_quick_settings_visible(client->compositor,
                    false);
            if (client->compositor->keyboard_visible)
                lumo_protocol_set_keyboard_visible(client->compositor, false);
            if (client->compositor->launcher_visible)
                lumo_protocol_set_launcher_visible(client->compositor, false);
            if (client->compositor->sidebar_visible)
                lumo_protocol_set_sidebar_visible(client->compositor, false);
            /* minimize all running apps */
            {
                struct lumo_toplevel *tl;
                wl_list_for_each(tl, &client->compositor->toplevels, link) {
                    tl->scene_tree->node.enabled = false;
                }
                wlr_seat_keyboard_clear_focus(client->compositor->seat);
                lumo_protocol_set_scrim_state(client->compositor,
                    LUMO_SCRIM_HIDDEN);
            }
            handled = true;
            break;
        case LUMO_SHELL_TARGET_OSK_KEY:
            wlr_log(WLR_INFO,
                "shell: activate_target osk key %u requested",
                index);
            failure_code = lumo_shell_bridge_commit_osk_text(
                client->compositor, index, &failure_reason);
            handled = failure_code == NULL;
            break;
        case LUMO_SHELL_TARGET_NONE:
        default:
            handled = false;
            break;
        }

        if (handled) {
            (void)lumo_shell_bridge_send_result(client, frame, true, NULL,
                NULL);
        } else {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                failure_code != NULL ? failure_code : "unsupported_target",
                failure_reason != NULL ? failure_reason : "unhandled_kind");
        }
        return;
    }

    if (strcmp(frame->name, "set_launcher_visible") == 0) {
        bool visible = false;

        if (!lumo_shell_protocol_frame_get_bool(frame, "visible", &visible)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "missing_field", "visible");
            return;
        }

        lumo_protocol_set_launcher_visible(client->compositor, visible);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "set_keyboard_visible") == 0) {
        bool visible = false;

        if (!lumo_shell_protocol_frame_get_bool(frame, "visible", &visible)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "missing_field", "visible");
            return;
        }

        lumo_protocol_set_keyboard_visible(client->compositor, visible);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "set_scrim_state") == 0) {
        const char *value = NULL;
        enum lumo_scrim_state scrim_state;

        if (!lumo_shell_protocol_frame_get(frame, "state", &value) ||
                !lumo_scrim_state_parse(value, &scrim_state)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "invalid_state", "scrim_state");
            return;
        }

        lumo_protocol_set_scrim_state(client->compositor, scrim_state);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "set_gesture_threshold") == 0) {
        double threshold = 0.0;
        uint32_t timeout_ms = client->compositor->gesture_timeout_ms;

        if (!lumo_shell_protocol_frame_get_double(frame, "threshold",
                &threshold)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "missing_field", "threshold");
            return;
        }

        (void)lumo_shell_protocol_frame_get_u32(frame, "timeout_ms",
            &timeout_ms);
        client->compositor->gesture_timeout_ms = timeout_ms;
        lumo_protocol_set_gesture_threshold(client->compositor, threshold);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "ack_keyboard_resize") == 0) {
        uint32_t serial = 0;

        if (!lumo_shell_protocol_frame_get_u32(frame, "serial", &serial)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "missing_field", "serial");
            return;
        }

        lumo_protocol_ack_keyboard_resize(client->compositor, serial);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "set_rotation") == 0) {
        const char *value = NULL;
        enum lumo_rotation rotation;

        if (!lumo_shell_protocol_frame_get(frame, "rotation", &value) ||
                !lumo_rotation_parse(value, &rotation)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "invalid_rotation", "rotation");
            return;
        }

        lumo_input_set_rotation(client->compositor, rotation);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "set_touch_audit_active") == 0) {
        bool active = false;

        if (!lumo_shell_protocol_frame_get_bool(frame, "active", &active)) {
            (void)lumo_shell_bridge_send_result(client, frame, false,
                "missing_field", "active");
            return;
        }

        lumo_touch_audit_set_active(client->compositor, active);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "set_volume") == 0) {
        uint32_t pct = 0;
        if (lumo_shell_protocol_frame_get_u32(frame, "pct", &pct)) {
            lumo_write_volume_pct(pct);
            client->compositor->volume_pct = pct;
            lumo_shell_mark_state_dirty(client->compositor);
            wlr_log(WLR_INFO, "shell: volume set to %u%%", pct);
        }
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "set_brightness") == 0) {
        uint32_t pct = 0;
        if (lumo_shell_protocol_frame_get_u32(frame, "pct", &pct)) {
            lumo_write_brightness_pct(pct);
            client->compositor->brightness_pct = pct;
            lumo_shell_mark_state_dirty(client->compositor);
            wlr_log(WLR_INFO, "shell: brightness set to %u%%", pct);
        }
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "show_toast") == 0) {
        const char *msg = NULL;
        uint32_t duration = 3000;
        if (lumo_shell_protocol_frame_get(frame, "message", &msg) &&
                msg != NULL) {
            strncpy(client->compositor->toast_message, msg,
                sizeof(client->compositor->toast_message) - 1);
            client->compositor->toast_message[
                sizeof(client->compositor->toast_message) - 1] = '\0';
            (void)lumo_shell_protocol_frame_get_u32(frame, "duration",
                &duration);
            client->compositor->toast_duration_ms = duration;
            {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                client->compositor->toast_show_time_ms =
                    (uint64_t)ts.tv_sec * 1000 +
                    (uint64_t)ts.tv_nsec / 1000000;
            }
            lumo_shell_mark_state_dirty(client->compositor);
            wlr_log(WLR_INFO, "shell: toast '%s' (%ums)", msg, duration);
        }
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "capture_screenshot") == 0) {
        if (client->compositor->notification_panel_visible) {
            lumo_protocol_set_notification_panel_visible(client->compositor, false);
        }
        if (client->compositor->quick_settings_visible) {
            lumo_protocol_set_quick_settings_visible(client->compositor, false);
        }
        if (client->compositor->time_panel_visible) {
            lumo_protocol_set_time_panel_visible(client->compositor, false);
        }
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        lumo_shell_capture_screenshot_async(client->compositor);
        return;
    }

    if (strcmp(frame->name, "ping") == 0) {
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "reload_session") == 0) {
        wlr_log(WLR_INFO, "shell: reload_session requested");
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        lumo_compositor_stop(client->compositor);
        return;
    }

    if (strcmp(frame->name, "cycle_rotation") == 0) {
        enum lumo_rotation next;
        switch (client->compositor->active_rotation) {
        case LUMO_ROTATION_NORMAL: next = LUMO_ROTATION_90; break;
        case LUMO_ROTATION_90: next = LUMO_ROTATION_180; break;
        case LUMO_ROTATION_180: next = LUMO_ROTATION_270; break;
        case LUMO_ROTATION_270: default: next = LUMO_ROTATION_NORMAL; break;
        }
        lumo_output_set_rotation(client->compositor, NULL, next);
        wlr_log(WLR_INFO, "shell: rotation cycled to %s",
            lumo_rotation_name(next));
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    if (strcmp(frame->name, "close_app") == 0) {
        uint32_t index = 0;
        bool has_index = lumo_shell_protocol_frame_get_u32(frame, "index",
            &index);
        if (has_index) {
            /* close by index (from sidebar context menu) */
            struct lumo_toplevel *tl;
            uint32_t n = 0;
            bool found = false;
            wl_list_for_each(tl, &client->compositor->toplevels, link) {
                if (n == index) {
                    wlr_xdg_toplevel_send_close(tl->xdg_toplevel);
                    found = true;
                    break;
                }
                n++;
            }
            wlr_log(WLR_INFO, "shell: close_app index=%u found=%d",
                index, found);
            (void)lumo_shell_bridge_send_result(client, frame, found,
                NULL, NULL);
        } else {
            /* close focused app (legacy) */
            bool closed = lumo_protocol_close_focused_app(
                client->compositor);
            wlr_log(WLR_INFO, "shell: close_app requested, result=%d",
                closed);
            (void)lumo_shell_bridge_send_result(client, frame, closed,
                closed ? NULL : "no_app",
                closed ? NULL : "no_focused_toplevel");
        }
        return;
    }

    /* sidebar: focus app by index in running apps list */
    if (strcmp(frame->name, "focus_app") == 0) {
        uint32_t index = 0;
        lumo_shell_protocol_frame_get_u32(frame, "index", &index);
        struct lumo_toplevel *tl;
        uint32_t n = 0;
        bool found = false;
        wl_list_for_each(tl, &client->compositor->toplevels, link) {
            if (n == index) {
                tl->scene_tree->node.enabled = true;
                wlr_scene_node_raise_to_top(&tl->scene_tree->node);
                struct wlr_surface *surface =
                    tl->xdg_toplevel->base->surface;
                struct wlr_seat *seat = client->compositor->seat;
                wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
                /* hide launcher and sidebar, set scrim for focused app */
                lumo_protocol_set_launcher_visible(client->compositor, false);
                lumo_protocol_set_sidebar_visible(client->compositor, false);
                lumo_protocol_set_scrim_state(client->compositor,
                    LUMO_SCRIM_DIMMED);
                found = true;
                break;
            }
            n++;
        }
        wlr_log(WLR_INFO, "shell: focus_app index=%u found=%d", index, found);
        (void)lumo_shell_bridge_send_result(client, frame, found, NULL, NULL);
        return;
    }

    /* minimize focused app (go home) */
    if (strcmp(frame->name, "minimize_focused") == 0) {
        struct lumo_toplevel *tl;
        bool minimized = false;
        wl_list_for_each(tl, &client->compositor->toplevels, link) {
            if (tl->scene_tree->node.enabled) {
                tl->scene_tree->node.enabled = false;
                minimized = true;
            }
        }
        if (minimized) {
            struct wlr_seat *seat = client->compositor->seat;
            wlr_seat_keyboard_clear_focus(seat);
            lumo_protocol_set_launcher_visible(client->compositor, false);
            lumo_protocol_set_sidebar_visible(client->compositor, false);
            lumo_protocol_set_scrim_state(client->compositor,
                LUMO_SCRIM_HIDDEN);
        }
        wlr_log(WLR_INFO, "shell: minimize_focused result=%d", minimized);
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    /* new window for multi-window apps (terminal, browser) */
    if (strcmp(frame->name, "new_window") == 0) {
        uint32_t index = 0;
        lumo_shell_protocol_frame_get_u32(frame, "index", &index);
        struct lumo_toplevel *tl;
        uint32_t n = 0;
        wl_list_for_each(tl, &client->compositor->toplevels, link) {
            if (n == index) {
                const char *aid = tl->xdg_toplevel->app_id;
                if (aid != NULL) {
                    /* find the matching launcher command and re-launch */
                    const char *cmd = NULL;
                    if (strstr(aid, "terminal") != NULL)
                        cmd = "lumo-app:terminal";
                    else if (strstr(aid, "browser") != NULL)
                        cmd = "lumo-app:browser";
                    if (cmd != NULL) {
                        lumo_shell_launch_app(client->compositor, cmd);
                        lumo_protocol_set_sidebar_visible(
                            client->compositor, false);
                        wlr_log(WLR_INFO,
                            "shell: new_window for %s", aid);
                    }
                }
                break;
            }
            n++;
        }
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    /* open app drawer (launcher) */
    if (strcmp(frame->name, "open_drawer") == 0) {
        /* hide sidebar first, then show launcher */
        lumo_protocol_set_sidebar_visible(client->compositor, false);
        lumo_protocol_set_launcher_visible(client->compositor, true);
        wlr_log(WLR_INFO, "shell: open_drawer requested");
        (void)lumo_shell_bridge_send_result(client, frame, true, NULL, NULL);
        return;
    }

    wlr_log(WLR_INFO, "shell: unhandled request %s", frame->name);
    (void)lumo_shell_bridge_send_result(client, frame, false,
        "unknown_request", "unsupported_name");
}

static void lumo_shell_bridge_client_handle_frame(
    const struct lumo_shell_protocol_frame *frame,
    void *data
) {
    struct lumo_shell_bridge_client *client = data;

    if (client == NULL || frame == NULL) {
        return;
    }

    if (frame->kind == LUMO_SHELL_PROTOCOL_FRAME_REQUEST) {
        lumo_shell_bridge_handle_request_frame(client, frame);
        return;
    }

    wlr_log(WLR_DEBUG, "shell: ignoring frame kind=%s name=%s",
        lumo_shell_protocol_frame_kind_name(frame->kind), frame->name);
}

static int lumo_shell_bridge_client_event(
    int fd,
    uint32_t mask,
    void *data
) {
    struct lumo_shell_bridge_client *client = data;
    struct lumo_shell_state *state = NULL;
    struct lumo_shell_bridge *bridge = NULL;
    char chunk[256];

    (void)mask;
    if (client == NULL) {
        return 0;
    }

    if (client->compositor != NULL) {
        state = client->compositor->shell_state;
        if (state != NULL) {
            bridge = &state->bridge;
        }
    }

    for (;;) {
        ssize_t bytes_read = recv(fd, chunk, sizeof(chunk), 0);

        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
            wlr_log_errno(WLR_ERROR, "shell: failed to read protocol frame");
            if (bridge != NULL) {
                lumo_shell_bridge_remove_client(bridge, client);
            }
            return 0;
        }

        if (bytes_read == 0) {
            if (bridge != NULL) {
                lumo_shell_bridge_remove_client(bridge, client);
            }
            return 0;
        }

        if (!lumo_shell_protocol_stream_feed(&client->stream, chunk,
                (size_t)bytes_read, lumo_shell_bridge_client_handle_frame,
                client)) {
            wlr_log(WLR_ERROR, "shell: invalid protocol frame from client");
            if (bridge != NULL) {
                lumo_shell_bridge_remove_client(bridge, client);
            }
            return 0;
        }
    }
}

static int lumo_shell_bridge_accept_event(
    int fd,
    uint32_t mask,
    void *data
) {
    struct lumo_compositor *compositor = data;
    struct lumo_shell_state *state;
    struct sockaddr_un address;
    socklen_t address_size = sizeof(address);
    int client_fd;

    (void)mask;
    if (compositor == NULL || compositor->shell_state == NULL) {
        return 0;
    }

    state = compositor->shell_state;
    for (;;) {
        address_size = sizeof(address);
        client_fd = accept(fd, (struct sockaddr *)&address, &address_size);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                wlr_log_errno(WLR_ERROR, "shell: failed to accept state client");
            }
            break;
        }

        int flags = fcntl(client_fd, F_GETFD);
        if (flags >= 0) {
            (void)fcntl(client_fd, F_SETFD, flags | FD_CLOEXEC);
        }
        flags = fcntl(client_fd, F_GETFL);
        if (flags >= 0) {
            (void)fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        }

        struct lumo_shell_bridge_client *client =
            calloc(1, sizeof(*client));
        if (client == NULL) {
            close(client_fd);
            continue;
        }

        client->fd = client_fd;
        client->compositor = compositor;
        lumo_shell_protocol_stream_init(&client->stream);
        client->source = wl_event_loop_add_fd(compositor->event_loop,
            client_fd, WL_EVENT_READABLE, lumo_shell_bridge_client_event,
            client);
        if (client->source == NULL) {
            close(client_fd);
            free(client);
            continue;
        }

        client->protocol_version = LUMO_SHELL_PROTOCOL_VERSION;
        wl_list_insert(&state->bridge.clients, &client->link);

        /* send protocol hello with version and feature flags */
        {
            struct lumo_shell_protocol_frame hello;
            lumo_shell_protocol_frame_init(&hello,
                LUMO_SHELL_PROTOCOL_FRAME_EVENT, "hello", 0);
            lumo_shell_protocol_frame_add_u32(&hello, "version",
                LUMO_SHELL_PROTOCOL_VERSION);
            lumo_shell_protocol_frame_add_string(&hello, "compositor",
                "lumo");
            lumo_shell_protocol_frame_add_bool(&hello, "sidebar_support",
                true);
            lumo_shell_protocol_frame_add_bool(&hello, "wave_background",
                true);
            lumo_shell_bridge_send_frame(client_fd, &hello);
        }

        lumo_shell_bridge_send_state_snapshot(compositor, client_fd);
    }

    return 0;
}

bool lumo_shell_bridge_start(struct lumo_compositor *compositor) {
    struct lumo_shell_state *state;
    const char *runtime_dir;
    struct sockaddr_un address;

    if (compositor == NULL || compositor->display == NULL ||
            compositor->event_loop == NULL || compositor->config == NULL) {
        return false;
    }

    runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir == NULL || runtime_dir[0] == '\0') {
        wlr_log(WLR_ERROR, "shell: XDG_RUNTIME_DIR is not set");
        return false;
    }

    state = compositor->shell_state;
    if (state == NULL) {
        return false;
    }

    if (!lumo_shell_state_socket_path(runtime_dir, state->bridge.socket_path,
            sizeof(state->bridge.socket_path))) {
        wlr_log(WLR_ERROR, "shell: failed to resolve state socket path");
        return false;
    }

    state->bridge.listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (state->bridge.listen_fd < 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to create state socket");
        return false;
    }

    int flags = fcntl(state->bridge.listen_fd, F_GETFD);
    if (flags >= 0) {
        (void)fcntl(state->bridge.listen_fd, F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(state->bridge.listen_fd, F_GETFL);
    if (flags >= 0) {
        (void)fcntl(state->bridge.listen_fd, F_SETFL, flags | O_NONBLOCK);
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s",
            state->bridge.socket_path) >= (int)sizeof(address.sun_path)) {
        wlr_log(WLR_ERROR, "shell: state socket path is too long");
        close(state->bridge.listen_fd);
        state->bridge.listen_fd = -1;
        return false;
    }

    unlink(state->bridge.socket_path);
    /* restrict socket to owner only (prevents local privilege escalation) */
    mode_t old_umask = umask(0077);
    if (bind(state->bridge.listen_fd, (struct sockaddr *)&address,
            sizeof(address)) != 0) {
        umask(old_umask);
        wlr_log_errno(WLR_ERROR, "shell: failed to bind state socket");
        close(state->bridge.listen_fd);
        state->bridge.listen_fd = -1;
        unlink(state->bridge.socket_path);
        return false;
    }

    umask(old_umask);

    if (listen(state->bridge.listen_fd, 4) != 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to listen on state socket");
        close(state->bridge.listen_fd);
        state->bridge.listen_fd = -1;
        unlink(state->bridge.socket_path);
        return false;
    }

    wl_list_init(&state->bridge.clients);
    state->bridge.listen_source = wl_event_loop_add_fd(
        compositor->event_loop,
        state->bridge.listen_fd,
        WL_EVENT_READABLE,
        lumo_shell_bridge_accept_event,
        compositor
    );
    if (state->bridge.listen_source == NULL) {
        wlr_log(WLR_ERROR, "shell: failed to watch state socket");
        close(state->bridge.listen_fd);
        state->bridge.listen_fd = -1;
        unlink(state->bridge.socket_path);
        return false;
    }

    if (setenv("LUMO_STATE_SOCKET", state->bridge.socket_path, true) != 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to export LUMO_STATE_SOCKET");
        wl_event_source_remove(state->bridge.listen_source);
        state->bridge.listen_source = NULL;
        close(state->bridge.listen_fd);
        state->bridge.listen_fd = -1;
        unlink(state->bridge.socket_path);
        return false;
    }

    wlr_log(WLR_INFO, "shell: state bridge listening on %s",
        state->bridge.socket_path);
    return true;
}

void lumo_shell_bridge_remove_client(
    struct lumo_shell_bridge *bridge,
    struct lumo_shell_bridge_client *client
) {
    if (bridge == NULL || client == NULL) {
        return;
    }

    if (client->source != NULL) {
        wl_event_source_remove(client->source);
        client->source = NULL;
    }
    wl_list_remove(&client->link);
    if (client->fd >= 0) {
        close(client->fd);
    }
    free(client);
}

void lumo_shell_bridge_stop(struct lumo_shell_state *state) {
    struct lumo_shell_bridge_client *client;
    struct lumo_shell_bridge_client *tmp;

    if (state == NULL) {
        return;
    }

    if (state->bridge.listen_source != NULL) {
        wl_event_source_remove(state->bridge.listen_source);
        state->bridge.listen_source = NULL;
    }

    wl_list_for_each_safe(client, tmp, &state->bridge.clients, link) {
        lumo_shell_bridge_remove_client(&state->bridge, client);
    }

    if (state->bridge.listen_fd >= 0) {
        close(state->bridge.listen_fd);
        state->bridge.listen_fd = -1;
    }

    if (state->bridge.socket_path[0] != '\0') {
        unlink(state->bridge.socket_path);
        state->bridge.socket_path[0] = '\0';
    }
}

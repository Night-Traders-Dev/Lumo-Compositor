#include "lumo/compositor.h"
#include "lumo/shell_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

struct lumo_shell_process {
    enum lumo_shell_mode mode;
    pid_t pid;
};

static void lumo_write_volume_pct(uint32_t pct);
static void lumo_write_brightness_pct(uint32_t pct);

struct lumo_shell_bridge_client {
    struct wl_list link;
    struct wl_event_source *source;
    int fd;
    struct lumo_compositor *compositor;
    struct lumo_shell_protocol_stream stream;
};

struct lumo_shell_bridge {
    int listen_fd;
    struct wl_event_source *listen_source;
    char socket_path[PATH_MAX];
    struct wl_list clients;
};

struct lumo_shell_state {
    struct lumo_compositor *compositor;
    struct wl_event_source *child_signal_source;
    bool stopping;
    bool state_broadcast_pending;
    char binary_path[PATH_MAX];
    size_t count;
    struct lumo_shell_process processes[5];
    struct lumo_shell_bridge bridge;
};

static void lumo_shell_bridge_remove_client(
    struct lumo_shell_bridge *bridge,
    struct lumo_shell_bridge_client *client
);
static void lumo_shell_bridge_broadcast_state(
    struct lumo_compositor *compositor
);
static void lumo_shell_mark_state_dirty(
    struct lumo_compositor *compositor
);
static int lumo_shell_spawn_process(
    struct lumo_compositor *compositor,
    enum lumo_shell_mode mode,
    const char *binary_path,
    struct lumo_shell_process *process
);
static void lumo_shell_reap_children(struct lumo_compositor *compositor);

static const char *lumo_shell_default_binary_name(void) {
    return "lumo-shell";
}

static const char *lumo_shell_mode_argument(enum lumo_shell_mode mode) {
    const char *mode_name = lumo_shell_mode_name(mode);

    return strcmp(mode_name, "unknown") == 0 ? NULL : mode_name;
}

static bool lumo_shell_has_path_separator(const char *path) {
    return path != NULL && strchr(path, '/') != NULL;
}

static bool lumo_shell_copy_path(
    char *buffer,
    size_t buffer_size,
    const char *path
) {
    size_t length;

    if (buffer == NULL || buffer_size == 0 || path == NULL) {
        return false;
    }

    length = strlen(path);
    if (length + 1 > buffer_size) {
        return false;
    }

    memcpy(buffer, path, length + 1);
    return true;
}

static bool lumo_shell_join_path(
    char *buffer,
    size_t buffer_size,
    const char *prefix,
    const char *suffix
) {
    size_t prefix_length;
    size_t suffix_length;
    size_t required;

    if (buffer == NULL || buffer_size == 0 || prefix == NULL || suffix == NULL) {
        return false;
    }

    prefix_length = strlen(prefix);
    suffix_length = strlen(suffix);
    required = prefix_length + 1 + suffix_length + 1;
    if (required > buffer_size) {
        return false;
    }

    memcpy(buffer, prefix, prefix_length);
    buffer[prefix_length] = '/';
    memcpy(buffer + prefix_length + 1, suffix, suffix_length + 1);
    return true;
}

bool lumo_shell_state_socket_path(
    const char *runtime_dir,
    char *buffer,
    size_t buffer_size
) {
    if (runtime_dir == NULL || runtime_dir[0] == '\0') {
        return false;
    }

    return lumo_shell_join_path(buffer, buffer_size, runtime_dir,
        "lumo-shell-state.sock");
}

size_t lumo_shell_state_format_line(
    char *buffer,
    size_t buffer_size,
    const char *key,
    const char *value
) {
    int written;

    if (buffer == NULL || buffer_size == 0 || key == NULL || value == NULL) {
        return 0;
    }

    written = snprintf(buffer, buffer_size, "%s=%s\n", key, value);
    if (written < 0 || (size_t)written >= buffer_size) {
        return 0;
    }

    return (size_t)written;
}

size_t lumo_shell_state_format_bool(
    char *buffer,
    size_t buffer_size,
    const char *key,
    bool value
) {
    return lumo_shell_state_format_line(buffer, buffer_size, key,
        value ? "1" : "0");
}

size_t lumo_shell_state_format_double(
    char *buffer,
    size_t buffer_size,
    const char *key,
    double value
) {
    char formatted[32];

    if (snprintf(formatted, sizeof(formatted), "%.2f", value) < 0) {
        return 0;
    }

    return lumo_shell_state_format_line(buffer, buffer_size, key, formatted);
}

static bool lumo_shell_parent_directory(
    const char *path,
    char *buffer,
    size_t buffer_size
) {
    const char *slash;
    size_t length;

    if (path == NULL || buffer == NULL || buffer_size == 0) {
        return false;
    }

    slash = strrchr(path, '/');
    if (slash == NULL) {
        return false;
    }

    length = (size_t)(slash - path);
    if (length + 1 > buffer_size) {
        return false;
    }

    memcpy(buffer, path, length);
    buffer[length] = '\0';
    return true;
}

static void lumo_shell_log_child_status(
    const struct lumo_shell_process *process,
    int status
) {
    const char *mode_name;

    if (process == NULL) {
        return;
    }

    mode_name = lumo_shell_mode_argument(process->mode);
    if (mode_name == NULL) {
        mode_name = "unknown";
    }

    if (WIFEXITED(status)) {
        wlr_log(WLR_INFO,
            "shell: %s pid=%d exited with status %d",
            mode_name,
            (int)process->pid,
            WEXITSTATUS(status));
        return;
    }

    if (WIFSIGNALED(status)) {
        wlr_log(WLR_INFO,
            "shell: %s pid=%d terminated by signal %d",
            mode_name,
            (int)process->pid,
            WTERMSIG(status));
        return;
    }

    wlr_log(WLR_INFO,
        "shell: %s pid=%d stopped with status 0x%x",
        mode_name,
        (int)process->pid,
        status);
}

static struct lumo_shell_process *lumo_shell_process_for_pid(
    struct lumo_shell_state *state,
    pid_t pid
) {
    if (state == NULL || pid <= 0) {
        return NULL;
    }

    for (size_t i = 0; i < state->count; i++) {
        if (state->processes[i].pid == pid) {
            return &state->processes[i];
        }
    }

    return NULL;
}

static bool lumo_shell_spawn_tracked_process(
    struct lumo_compositor *compositor,
    struct lumo_shell_state *state,
    enum lumo_shell_mode mode
) {
    size_t index = 0;
    struct lumo_shell_process *process;

    if (compositor == NULL || state == NULL ||
            !lumo_shell_mode_index(mode, &index) ||
            index >= sizeof(state->processes) / sizeof(state->processes[0])) {
        return false;
    }

    process = &state->processes[index];
    process->mode = mode;
    process->pid = -1;
    if (lumo_shell_spawn_process(compositor, mode, state->binary_path,
            process) != 0) {
        return false;
    }

    if (index + 1 > state->count) {
        state->count = index + 1;
    }
    return true;
}

static int lumo_shell_handle_child_signal(
    int signal_number,
    void *data
) {
    (void)signal_number;
    lumo_shell_reap_children(data);

    return 0;
}

static void lumo_shell_reap_children(struct lumo_compositor *compositor) {
    struct lumo_shell_state *state;
    int status = 0;
    pid_t pid;

    if (compositor == NULL) {
        return;
    }

    state = compositor->shell_state;
    if (state == NULL) {
        return;
    }

    for (;;) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid == 0) {
            break;
        }
        if (pid < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != ECHILD) {
                wlr_log_errno(WLR_ERROR, "shell: failed to reap child");
            }
            break;
        }

        struct lumo_shell_process *process = lumo_shell_process_for_pid(state,
            pid);
        if (process == NULL) {
            /* Reap untracked children (e.g. pids from lumo_shell_launch_app)
             * to prevent zombie processes. The waitpid(-1,...) loop above
             * already handles these; we simply discard them here. */
            wlr_log(WLR_DEBUG, "shell: reaped untracked child pid=%d",
                (int)pid);
            continue;
        }

        lumo_shell_log_child_status(process, status);
        process->pid = -1;
        if (state->stopping) {
            continue;
        }

        if (!lumo_shell_spawn_tracked_process(compositor, state,
                process->mode)) {
            wlr_log(WLR_ERROR, "shell: failed to respawn %s client",
                lumo_shell_mode_argument(process->mode));
            continue;
        }

        wlr_log(WLR_INFO, "shell: respawned %s client pid=%d",
            lumo_shell_mode_argument(process->mode), (int)process->pid);
        lumo_shell_mark_state_dirty(compositor);
    }
}

static bool lumo_shell_wait_for_exec_result(int fd) {
    struct pollfd pollfd = {
        .fd = fd,
        .events = POLLIN | POLLHUP,
    };
    int poll_result;

    for (;;) {
        poll_result = poll(&pollfd, 1, 250);
        if (poll_result < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    if (poll_result < 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to poll exec status pipe");
        return false;
    }

    if (poll_result == 0) {
        return true;
    }

    if (pollfd.revents & POLLIN) {
        int child_errno = 0;
        ssize_t bytes_read = read(fd, &child_errno, sizeof(child_errno));

        if (bytes_read > 0) {
            errno = child_errno;
            wlr_log_errno(WLR_ERROR, "shell: failed to exec shell client");
            return false;
        }
    }

    return true;
}

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

static bool lumo_shell_bridge_output_size(
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
    if (!lumo_shell_protocol_frame_add_bool(frame, "quick_settings_visible",
            compositor->quick_settings_visible)) {
        return false;
    }
    if (!lumo_shell_protocol_frame_add_bool(frame, "time_panel_visible",
            compositor->time_panel_visible)) {
        return false;
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

static void lumo_shell_bridge_broadcast_state(
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

static void lumo_shell_mark_state_dirty(
    struct lumo_compositor *compositor
) {
    struct lumo_shell_state *state;

    if (compositor == NULL) {
        return;
    }

    state = compositor->shell_state;
    if (state != NULL) {
        state->state_broadcast_pending = true;
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

    /* shift key (empty text) - ignore for now */
    if (text[0] == '\0') {
        wlr_log(WLR_INFO, "shell: osk key %u is modifier, ignoring", index);
        return NULL;
    }

    text_input = lumo_shell_bridge_focused_text_input(compositor);
    if (text_input == NULL) {
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
            static char app_path[PATH_MAX];
            char parent_directory[PATH_MAX];

            if (lumo_shell_parent_directory(state->binary_path,
                    parent_directory, sizeof(parent_directory)) &&
                    lumo_shell_join_path(app_path, sizeof(app_path),
                        parent_directory, "lumo-app")) {
                binary = app_path;
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
        } else {
            execlp(command, command, (char *)NULL);
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
            wlr_log(WLR_INFO,
                "shell: activate_target launcher tile %u requested (cmd=%s)",
                index,
                app_command != NULL ? app_command : "none");
            if (app_command != NULL) {
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
                "shell: activate_target gesture handle requested");
            lumo_protocol_set_launcher_visible(client->compositor,
                !client->compositor->launcher_visible);
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
            snprintf(client->compositor->toast_message,
                sizeof(client->compositor->toast_message),
                "Volume: %u%%", pct);
            {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                client->compositor->toast_show_time_ms =
                    (uint64_t)ts.tv_sec * 1000 +
                    (uint64_t)ts.tv_nsec / 1000000;
            }
            client->compositor->toast_duration_ms = 2000;
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
            snprintf(client->compositor->toast_message,
                sizeof(client->compositor->toast_message),
                "Brightness: %u%%", pct);
            {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                client->compositor->toast_show_time_ms =
                    (uint64_t)ts.tv_sec * 1000 +
                    (uint64_t)ts.tv_nsec / 1000000;
            }
            client->compositor->toast_duration_ms = 2000;
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

        wl_list_insert(&state->bridge.clients, &client->link);
        lumo_shell_bridge_send_state_snapshot(compositor, client_fd);
    }

    return 0;
}

static bool lumo_shell_bridge_start(struct lumo_compositor *compositor) {
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
    if (bind(state->bridge.listen_fd, (struct sockaddr *)&address,
            sizeof(address)) != 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to bind state socket");
        close(state->bridge.listen_fd);
        state->bridge.listen_fd = -1;
        unlink(state->bridge.socket_path);
        return false;
    }

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

static void lumo_shell_bridge_remove_client(
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

static void lumo_shell_bridge_stop(struct lumo_shell_state *state) {
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

static int lumo_shell_spawn_process(
    struct lumo_compositor *compositor,
    enum lumo_shell_mode mode,
    const char *binary_path,
    struct lumo_shell_process *process
) {
    const char *mode_argument;
    const char *argv[4] = {0};
    int status_pipe[2] = {-1, -1};
    pid_t pid;
    int flags;
    int child_errno = 0;

    if (compositor == NULL || binary_path == NULL || process == NULL) {
        return -1;
    }

    mode_argument = lumo_shell_mode_argument(mode);
    if (mode_argument == NULL) {
        wlr_log(WLR_ERROR, "shell: unsupported mode requested");
        return -1;
    }

    if (pipe(status_pipe) != 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to create status pipe");
        return -1;
    }

    flags = fcntl(status_pipe[0], F_GETFD);
    if (flags >= 0) {
        (void)fcntl(status_pipe[0], F_SETFD, flags | FD_CLOEXEC);
    }
    flags = fcntl(status_pipe[1], F_GETFD);
    if (flags >= 0) {
        (void)fcntl(status_pipe[1], F_SETFD, flags | FD_CLOEXEC);
    }

    argv[0] = binary_path;
    argv[1] = "--mode";
    argv[2] = mode_argument;
    argv[3] = NULL;

    pid = fork();
    if (pid < 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to fork shell client");
        close(status_pipe[0]);
        close(status_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        close(status_pipe[0]);
        execv(binary_path, (char *const *)argv);
        child_errno = errno;
        (void)write(status_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }

    close(status_pipe[1]);
    if (!lumo_shell_wait_for_exec_result(status_pipe[0])) {
        close(status_pipe[0]);
        (void)kill(pid, SIGTERM);
        (void)waitpid(pid, NULL, 0);
        return -1;
    }
    close(status_pipe[0]);

    process->mode = mode;
    process->pid = pid;
    return 0;
}

bool lumo_shell_resolve_binary_path(
    const struct lumo_compositor_config *config,
    char *buffer,
    size_t buffer_size
) {
    const char *requested_path;
    const char *executable_path;
    char parent_directory[PATH_MAX];

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    requested_path = config != NULL ? config->shell_path : NULL;
    executable_path = config != NULL ? config->executable_path : NULL;

    if (requested_path != NULL && requested_path[0] != '\0') {
        if (requested_path[0] == '/' || lumo_shell_has_path_separator(requested_path)) {
            return lumo_shell_copy_path(buffer, buffer_size, requested_path);
        }

        if (lumo_shell_parent_directory(executable_path, parent_directory,
                sizeof(parent_directory))) {
            return lumo_shell_join_path(buffer, buffer_size, parent_directory,
                requested_path);
        }

        return lumo_shell_copy_path(buffer, buffer_size, requested_path);
    }

    if (lumo_shell_parent_directory(executable_path, parent_directory,
            sizeof(parent_directory))) {
        return lumo_shell_join_path(buffer, buffer_size, parent_directory,
            lumo_shell_default_binary_name());
    }

    return lumo_shell_copy_path(buffer, buffer_size,
        lumo_shell_default_binary_name());
}

size_t lumo_shell_build_argv(
    enum lumo_shell_mode mode,
    const char *binary,
    const char **argv,
    size_t capacity
) {
    const char *mode_argument;

    if (binary == NULL || argv == NULL || capacity < 4) {
        return 0;
    }

    mode_argument = lumo_shell_mode_argument(mode);
    if (mode_argument == NULL) {
        return 0;
    }

    argv[0] = binary;
    argv[1] = "--mode";
    argv[2] = mode_argument;
    argv[3] = NULL;
    return 3;
}

static int lumo_weather_timer_cb(void *data);

/* --- volume / brightness helpers --- */

static uint32_t lumo_read_brightness_pct(void) {
    FILE *fp;
    int cur = 0, max = 255;
    fp = fopen("/sys/class/backlight/soc:lcd_backlight/brightness", "r");
    if (fp) { if (fscanf(fp, "%d", &cur) < 1) cur = 0; fclose(fp); }
    fp = fopen("/sys/class/backlight/soc:lcd_backlight/max_brightness", "r");
    if (fp) { if (fscanf(fp, "%d", &max) < 1) max = 255; fclose(fp); }
    if (max <= 0) max = 255;
    return (uint32_t)(cur * 100 / max);
}

static void lumo_write_brightness_pct(uint32_t pct) {
    int max = 255;
    FILE *mfp = fopen("/sys/class/backlight/soc:lcd_backlight/max_brightness", "r");
    if (mfp) { if (fscanf(mfp, "%d", &max) < 1) max = 255; fclose(mfp); }
    if (pct > 100) pct = 100;
    int val = (int)(pct * (uint32_t)max / 100);
    if (val < 1) val = 1;
    FILE *fp = fopen("/sys/class/backlight/soc:lcd_backlight/brightness", "w");
    if (fp) { fprintf(fp, "%d", val); fclose(fp); }
}

static uint32_t lumo_read_volume_pct(void) {
    /* read ALSA volume from sysfs to avoid blocking popen */
    FILE *fp;
    int val = 145, max_val = 192; /* es8323 PCM defaults */
    fp = fopen("/proc/asound/card1/codec#0/codec_reg", "r");
    if (fp) { fclose(fp); } /* not useful, use amixer in bg */
    /* fallback: just return last known value or 50% */
    return 50;
}

static void lumo_write_volume_pct(uint32_t pct) {
    /* non-blocking: fork amixer so the event loop isn't stalled */
    pid_t pid;
    char pct_str[8];
    if (pct > 100) pct = 100;
    snprintf(pct_str, sizeof(pct_str), "%u%%", pct);
    pid = fork();
    if (pid == 0) {
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        execlp("amixer", "amixer", "-c", "1", "set", "PCM", pct_str,
            (char *)NULL);
        _exit(127);
    }
    /* parent returns immediately — child reaped by SIGCHLD handler */
}

static void lumo_refresh_vol_bright(struct lumo_compositor *compositor) {
    if (compositor == NULL) return;
    uint32_t vol = lumo_read_volume_pct();
    uint32_t bri = lumo_read_brightness_pct();
    if (vol != compositor->volume_pct || bri != compositor->brightness_pct) {
        compositor->volume_pct = vol;
        compositor->brightness_pct = bri;
        lumo_shell_mark_state_dirty(compositor);
    }
}

static void lumo_shell_play_boot_sound(void) {
    /* generate a short two-tone chime WAV in /tmp and play it async */
    pid_t pid = fork();
    if (pid != 0) return; /* parent returns immediately */

    /* child: create a tiny WAV and play it */
    const char *path = "/tmp/lumo-boot.wav";
    const uint32_t sample_rate = 22050;
    const uint32_t duration_ms = 400;
    const uint32_t num_samples = sample_rate * duration_ms / 1000;
    const uint32_t data_size = num_samples * 2; /* 16-bit mono */
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) _exit(1);

    /* WAV header */
    uint32_t chunk_size = 36 + data_size;
    uint16_t audio_fmt = 1; /* PCM */
    uint16_t channels = 1;
    uint32_t byte_rate = sample_rate * 2;
    uint16_t block_align = 2;
    uint16_t bits = 16;
    fwrite("RIFF", 1, 4, fp);
    fwrite(&chunk_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, fp);
    fwrite(&audio_fmt, 2, 1, fp);
    fwrite(&channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_size, 4, 1, fp);

    /* two-tone chime: C5 (523Hz) then E5 (659Hz) with fade */
    for (uint32_t i = 0; i < num_samples; i++) {
        double t = (double)i / sample_rate;
        double freq = (i < num_samples / 2) ? 523.0 : 659.0;
        double env = 1.0 - (double)i / num_samples; /* linear fade */
        env *= env; /* quadratic fade for smoother decay */
        double sample = env * 0.4 * __builtin_sin(2.0 * 3.14159265 * freq * t);
        int16_t pcm = (int16_t)(sample * 32000.0);
        fwrite(&pcm, 2, 1, fp);
    }
    fclose(fp);

    /* play with aplay (non-interactive, no blocking the compositor) */
    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    execlp("aplay", "aplay", "-q", path, (char *)NULL);
    _exit(127);
}

int lumo_shell_autostart_start(struct lumo_compositor *compositor) {
    struct lumo_shell_state *state;
    const enum lumo_shell_mode modes[] = {
        LUMO_SHELL_MODE_BACKGROUND,
        LUMO_SHELL_MODE_LAUNCHER,
        LUMO_SHELL_MODE_OSK,
        LUMO_SHELL_MODE_GESTURE,
        LUMO_SHELL_MODE_STATUS,
    };

    if (compositor == NULL || compositor->display == NULL ||
            compositor->config == NULL) {
        return -1;
    }

    if (compositor->shell_state != NULL) {
        lumo_shell_autostart_stop(compositor);
    }

    if (setenv("LUMO_STATE_SOCKET", "", true) != 0) {
        wlr_log_errno(WLR_ERROR, "shell: failed to clear LUMO_STATE_SOCKET");
        return -1;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        wlr_log_errno(WLR_ERROR, "shell: failed to allocate shell state");
        return -1;
    }

    compositor->shell_state = state;
    state->compositor = compositor;
    state->bridge.listen_fd = -1;
    state->bridge.listen_source = NULL;
    state->bridge.socket_path[0] = '\0';
    state->child_signal_source = NULL;
    state->stopping = false;
    wl_list_init(&state->bridge.clients);
    for (size_t i = 0; i < sizeof(state->processes) / sizeof(state->processes[0]); i++) {
        state->processes[i].pid = -1;
    }

    if (!lumo_shell_resolve_binary_path(compositor->config, state->binary_path,
            sizeof(state->binary_path))) {
        wlr_log(WLR_ERROR, "shell: failed to resolve shell binary path");
        lumo_shell_autostart_stop(compositor);
        return -1;
    }

    state->child_signal_source = wl_event_loop_add_signal(
        compositor->event_loop,
        SIGCHLD,
        lumo_shell_handle_child_signal,
        compositor
    );
    if (state->child_signal_source == NULL) {
        wlr_log(WLR_ERROR, "shell: failed to watch child process exits");
        lumo_shell_autostart_stop(compositor);
        return -1;
    }

    if (!lumo_shell_bridge_start(compositor)) {
        lumo_shell_autostart_stop(compositor);
        return -1;
    }

    wlr_log(WLR_INFO, "shell: launching clients from %s", state->binary_path);

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (!lumo_shell_spawn_tracked_process(compositor, state, modes[i])) {
            lumo_shell_autostart_stop(compositor);
            return -1;
        }
    }

    /* read initial volume and brightness */
    compositor->volume_pct = lumo_read_volume_pct();
    compositor->brightness_pct = lumo_read_brightness_pct();

    /* play boot chime */
    lumo_shell_play_boot_sound();

    /* start weather timer — first fetch after 2s, then every 5 min */
    if (compositor->event_loop != NULL) {
        compositor->weather_timer = wl_event_loop_add_timer(
            compositor->event_loop, lumo_weather_timer_cb, compositor);
        if (compositor->weather_timer != NULL) {
            wl_event_source_timer_update(compositor->weather_timer, 2000);
        }
    }

    return 0;
}

/* --- weather fetcher --- */

static void lumo_weather_parse(struct lumo_compositor *compositor,
    const char *data)
{
    /* expects format like "+5°C Sunny" or "-2°C Partly cloudy" from
     * curl 'https://wttr.in/41101?format=%t+%C' */
    int temp = 0;
    char condition[32] = "";

    if (data == NULL || compositor == NULL) return;

    /* parse temperature: skip leading whitespace, handle +/- */
    const char *p = data;
    while (*p == ' ') p++;
    if (sscanf(p, "%d", &temp) < 1) temp = 0;

    /* find condition after °C or °F */
    const char *deg = strstr(p, "\xC2\xB0"); /* UTF-8 degree sign */
    if (deg != NULL) {
        deg += 2; /* skip ° */
        if (*deg == 'C' || *deg == 'F') deg++;
        while (*deg == ' ') deg++;
        /* parse: "Sunny 45% →10mph\n" or similar */
        strncpy(condition, deg, sizeof(condition) - 1);
        condition[sizeof(condition) - 1] = '\0';
        char *nl = strchr(condition, '\n');
        if (nl) *nl = '\0';

        /* extract humidity (look for XX%) */
        char humidity[16] = "";
        char wind[24] = "";
        {
            const char *hp = strstr(deg, "%");
            if (hp != NULL) {
                /* scan backwards from % to find digits */
                const char *hs = hp;
                while (hs > deg && (*(hs - 1) >= '0' && *(hs - 1) <= '9'))
                    hs--;
                if (hs < hp) {
                    size_t hlen = (size_t)(hp - hs + 1);
                    if (hlen < sizeof(humidity)) {
                        memcpy(humidity, hs, hlen);
                        humidity[hlen] = '\0';
                    }
                }
            }
            /* extract wind (everything after % and space) */
            if (hp != NULL) {
                const char *wp = hp + 1;
                while (*wp == ' ') wp++;
                strncpy(wind, wp, sizeof(wind) - 1);
                wind[sizeof(wind) - 1] = '\0';
                nl = strchr(wind, '\n');
                if (nl) *nl = '\0';
            }
            /* truncate condition to just the weather name */
            if (hp != NULL) {
                /* find the space before humidity */
                char *sp = condition;
                char *pct = strstr(sp, "%");
                if (pct != NULL) {
                    char *cut = pct;
                    while (cut > sp && *(cut - 1) != ' ') cut--;
                    if (cut > sp) {
                        cut--;
                        while (cut > sp && *(cut - 1) == ' ') cut--;
                        *cut = '\0';
                    }
                }
            }
        }
        strncpy(compositor->weather_humidity, humidity,
            sizeof(compositor->weather_humidity) - 1);
        strncpy(compositor->weather_wind, wind,
            sizeof(compositor->weather_wind) - 1);
    }

    /* map condition text to a weather code for the theme engine:
     * 0=clear 1=partly_cloudy 2=cloudy 3=rain 4=storm 5=snow 6=fog */
    int code = 0;
    char lower[32];
    for (int i = 0; i < (int)sizeof(lower) - 1 && condition[i]; i++) {
        lower[i] = (condition[i] >= 'A' && condition[i] <= 'Z')
            ? condition[i] + 32 : condition[i];
        lower[i + 1] = '\0';
    }
    if (strstr(lower, "snow") || strstr(lower, "sleet") ||
            strstr(lower, "ice") || strstr(lower, "blizzard"))
        code = 5;
    else if (strstr(lower, "thunder") || strstr(lower, "storm"))
        code = 4;
    else if (strstr(lower, "rain") || strstr(lower, "drizzle") ||
            strstr(lower, "shower"))
        code = 3;
    else if (strstr(lower, "fog") || strstr(lower, "mist") ||
            strstr(lower, "haze"))
        code = 6;
    else if (strstr(lower, "overcast") || strstr(lower, "cloudy"))
        code = 2;
    else if (strstr(lower, "partly"))
        code = 1;

    if (strcmp(compositor->weather_condition, condition) != 0 ||
            compositor->weather_temp_c != temp ||
            compositor->weather_code != code) {
        strncpy(compositor->weather_condition, condition,
            sizeof(compositor->weather_condition) - 1);
        compositor->weather_temp_c = temp;
        compositor->weather_code = code;
        wlr_log(WLR_INFO, "weather: %dF %s (code=%d)", temp, condition,
            code);
        lumo_shell_mark_state_dirty(compositor);
    }
}

static void lumo_weather_fetch(struct lumo_compositor *compositor) {
    int pipefd[2];
    pid_t pid;

    if (compositor == NULL) return;
    if (pipe(pipefd) < 0) return;

    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        close(STDERR_FILENO);
        execlp("curl", "curl", "-s", "--max-time", "8",
            "https://wttr.in/41101?format=%t+%C+%h+%w&u", (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);

    /* Use poll() with a 5 s timeout before reading so that a stalled curl
     * process cannot block the compositor event loop indefinitely.  curl is
     * also given --max-time 10 as a belt-and-suspenders guard, but poll()
     * here ensures we give up earlier if the kernel pipe never becomes
     * readable (e.g. curl hangs before writing anything). */
    char buf[128] = "";
    ssize_t n = 0;
    {
        struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN };
        int pr = poll(&pfd, 1, 5000);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            n = read(pipefd[0], buf, sizeof(buf) - 1);
        } else if (pr == 0) {
            wlr_log(WLR_INFO, "weather: curl timed out after 5s, skipping");
        }
    }
    close(pipefd[0]);
    waitpid(pid, NULL, WNOHANG); /* non-blocking reap; zombie cleaned by SIGCHLD */

    if (n > 0) {
        buf[n] = '\0';
        lumo_weather_parse(compositor, buf);
    }
}

static int lumo_weather_timer_cb(void *data) {
    struct lumo_compositor *compositor = data;
    lumo_weather_fetch(compositor);
    /* re-arm for 5 minutes */
    if (compositor != NULL && compositor->weather_timer != NULL) {
        wl_event_source_timer_update(compositor->weather_timer, 300000);
    }
    return 0;
}

void lumo_shell_autostart_poll(struct lumo_compositor *compositor) {
    struct lumo_shell_state *state;

    lumo_shell_reap_children(compositor);

    if (compositor == NULL) {
        return;
    }
    state = compositor->shell_state;
    if (state != NULL && state->state_broadcast_pending) {
        lumo_shell_bridge_broadcast_state(compositor);
    }
}

void lumo_shell_autostart_stop(struct lumo_compositor *compositor) {
    struct lumo_shell_state *state;

    if (compositor == NULL) {
        return;
    }

    state = compositor->shell_state;
    if (state == NULL) {
        return;
    }

    state->stopping = true;
    if (state->child_signal_source != NULL) {
        wl_event_source_remove(state->child_signal_source);
        state->child_signal_source = NULL;
    }

    lumo_shell_bridge_stop(state);

    for (size_t i = 0; i < state->count; i++) {
        if (state->processes[i].pid > 0) {
            (void)kill(state->processes[i].pid, SIGTERM);
        }
    }

    for (size_t i = 0; i < state->count; i++) {
        struct lumo_shell_process *process = &state->processes[i];
        int status = 0;
        bool reaped = false;

        if (process->pid <= 0) {
            continue;
        }

        for (int attempt = 0; attempt < 100; attempt++) {
            pid_t waited = waitpid(process->pid, &status, WNOHANG);
            if (waited == process->pid) {
                reaped = true;
                break;
            }
            if (waited < 0) {
                if (errno == EINTR) {
                    attempt--;
                    continue;
                }
                wlr_log_errno(WLR_ERROR, "shell: failed to reap pid %d",
                    (int)process->pid);
                break;
            }

            struct timespec sleep_time = {
                .tv_sec = 0,
                .tv_nsec = 10 * 1000 * 1000,
            };
            nanosleep(&sleep_time, NULL);
        }

        if (!reaped) {
            (void)kill(process->pid, SIGKILL);
            if (waitpid(process->pid, &status, 0) == process->pid) {
                reaped = true;
            }
        }

        if (reaped) {
            lumo_shell_log_child_status(process, status);
        }
    }

    free(state);
    compositor->shell_state = NULL;
}

void lumo_shell_state_broadcast_launcher_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    (void)visible;
    lumo_shell_mark_state_dirty(compositor);
}

void lumo_shell_state_broadcast_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    (void)visible;
    lumo_shell_mark_state_dirty(compositor);
}

void lumo_shell_state_broadcast_scrim_state(
    struct lumo_compositor *compositor,
    enum lumo_scrim_state state
) {
    (void)state;
    lumo_shell_mark_state_dirty(compositor);
}

void lumo_shell_state_broadcast_gesture_threshold(
    struct lumo_compositor *compositor,
    double threshold,
    uint32_t timeout_ms
) {
    (void)threshold;
    (void)timeout_ms;
    lumo_shell_mark_state_dirty(compositor);
}

void lumo_shell_state_broadcast_rotation(
    struct lumo_compositor *compositor,
    enum lumo_rotation rotation
) {
    (void)rotation;
    lumo_shell_mark_state_dirty(compositor);
}

void lumo_shell_state_broadcast_touch_debug(struct lumo_compositor *compositor) {
    lumo_shell_mark_state_dirty(compositor);
}

void lumo_shell_state_broadcast_touch_audit(struct lumo_compositor *compositor) {
    lumo_shell_mark_state_dirty(compositor);
}

#include "lumo/compositor.h"

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

struct lumo_shell_bridge_client {
    struct wl_list link;
    int fd;
};

struct lumo_shell_bridge {
    int listen_fd;
    struct wl_event_source *listen_source;
    char socket_path[PATH_MAX];
    struct wl_list clients;
};

struct lumo_shell_state {
    size_t count;
    struct lumo_shell_process processes[3];
    struct lumo_shell_bridge bridge;
};

static const char *lumo_shell_default_binary_name(void) {
    return "lumo-shell";
}

static const char *lumo_shell_mode_argument(enum lumo_shell_mode mode) {
    switch (mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        return "launcher";
    case LUMO_SHELL_MODE_OSK:
        return "osk";
    case LUMO_SHELL_MODE_GESTURE:
        return "gesture";
    default:
        return NULL;
    }
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

static const char *lumo_shell_scrim_state_name(enum lumo_scrim_state state) {
    switch (state) {
    case LUMO_SCRIM_DIMMED:
        return "dimmed";
    case LUMO_SCRIM_MODAL:
        return "modal";
    case LUMO_SCRIM_HIDDEN:
    default:
        return "hidden";
    }
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

static void lumo_shell_bridge_remove_client(
    struct lumo_shell_bridge *bridge,
    struct lumo_shell_bridge_client *client
) {
    if (bridge == NULL || client == NULL) {
        return;
    }

    wl_list_remove(&client->link);
    if (client->fd >= 0) {
        close(client->fd);
    }
    free(client);
}

static void lumo_shell_bridge_send_snapshot(
    struct lumo_compositor *compositor,
    int fd
) {
    char line[128];
    size_t length;
    const char *scrim_state;

    if (compositor == NULL || compositor->config == NULL) {
        return;
    }

    length = lumo_shell_state_format_bool(line, sizeof(line),
        "launcher visible", compositor->launcher_visible);
    if (length > 0) {
        (void)lumo_shell_bridge_write_all(fd, line, length);
    }

    length = lumo_shell_state_format_bool(line, sizeof(line),
        "keyboard visible", compositor->keyboard_visible);
    if (length > 0) {
        (void)lumo_shell_bridge_write_all(fd, line, length);
    }

    scrim_state = lumo_shell_scrim_state_name(compositor->scrim_state);
    length = lumo_shell_state_format_line(line, sizeof(line),
        "scrim state", scrim_state);
    if (length > 0) {
        (void)lumo_shell_bridge_write_all(fd, line, length);
    }

    length = lumo_shell_state_format_line(line, sizeof(line),
        "rotation", lumo_rotation_name(compositor->active_rotation));
    if (length > 0) {
        (void)lumo_shell_bridge_write_all(fd, line, length);
    }

    length = lumo_shell_state_format_double(line, sizeof(line),
        "gesture threshold", compositor->gesture_threshold);
    if (length > 0) {
        (void)lumo_shell_bridge_write_all(fd, line, length);
    }

    length = snprintf(line, sizeof(line), "gesture timeout_ms=%u\n",
        compositor->gesture_timeout_ms);
    if (length > 0 && (size_t)length < sizeof(line)) {
        (void)lumo_shell_bridge_write_all(fd, line, (size_t)length);
    }
}

static void lumo_shell_bridge_broadcast(
    struct lumo_compositor *compositor,
    const char *line,
    size_t length
) {
    struct lumo_shell_state *state;
    struct lumo_shell_bridge_client *client;
    struct lumo_shell_bridge_client *tmp;

    if (compositor == NULL || line == NULL || length == 0) {
        return;
    }

    state = compositor->shell_state;
    if (state == NULL) {
        return;
    }

    wl_list_for_each_safe(client, tmp, &state->bridge.clients, link) {
        if (!lumo_shell_bridge_write_all(client->fd, line, length)) {
            lumo_shell_bridge_remove_client(&state->bridge, client);
        }
    }
}

static void lumo_shell_bridge_broadcast_bool(
    struct lumo_compositor *compositor,
    const char *key,
    bool value
) {
    char line[64];
    size_t length = lumo_shell_state_format_bool(line, sizeof(line), key, value);

    if (length > 0) {
        lumo_shell_bridge_broadcast(compositor, line, length);
    }
}

static void lumo_shell_bridge_broadcast_line(
    struct lumo_compositor *compositor,
    const char *key,
    const char *value
) {
    char line[128];
    size_t length = lumo_shell_state_format_line(line, sizeof(line), key, value);

    if (length > 0) {
        lumo_shell_bridge_broadcast(compositor, line, length);
    }
}

static void lumo_shell_bridge_broadcast_double(
    struct lumo_compositor *compositor,
    const char *key,
    double value
) {
    char line[64];
    size_t length = lumo_shell_state_format_double(line, sizeof(line), key,
        value);

    if (length > 0) {
        lumo_shell_bridge_broadcast(compositor, line, length);
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
        wl_list_insert(&state->bridge.clients, &client->link);
        lumo_shell_bridge_send_snapshot(compositor, client_fd);
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

int lumo_shell_autostart_start(struct lumo_compositor *compositor) {
    struct lumo_shell_state *state;
    char binary_path[PATH_MAX];
    const enum lumo_shell_mode modes[] = {
        LUMO_SHELL_MODE_LAUNCHER,
        LUMO_SHELL_MODE_OSK,
        LUMO_SHELL_MODE_GESTURE,
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

    if (!lumo_shell_resolve_binary_path(compositor->config, binary_path,
            sizeof(binary_path))) {
        wlr_log(WLR_ERROR, "shell: failed to resolve shell binary path");
        return -1;
    }

    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        wlr_log_errno(WLR_ERROR, "shell: failed to allocate shell state");
        return -1;
    }

    compositor->shell_state = state;
    state->bridge.listen_fd = -1;
    state->bridge.listen_source = NULL;
    state->bridge.socket_path[0] = '\0';
    wl_list_init(&state->bridge.clients);

    if (!lumo_shell_bridge_start(compositor)) {
        lumo_shell_autostart_stop(compositor);
        return -1;
    }

    wlr_log(WLR_INFO, "shell: launching clients from %s", binary_path);

    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (lumo_shell_spawn_process(compositor, modes[i], binary_path,
                &state->processes[state->count]) != 0) {
            lumo_shell_autostart_stop(compositor);
            return -1;
        }
        state->count++;
    }

    return 0;
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
    lumo_shell_bridge_broadcast_bool(compositor, "launcher visible", visible);
}

void lumo_shell_state_broadcast_keyboard_visible(
    struct lumo_compositor *compositor,
    bool visible
) {
    lumo_shell_bridge_broadcast_bool(compositor, "keyboard visible", visible);
}

void lumo_shell_state_broadcast_scrim_state(
    struct lumo_compositor *compositor,
    enum lumo_scrim_state state
) {
    lumo_shell_bridge_broadcast_line(compositor, "scrim state",
        lumo_shell_scrim_state_name(state));
}

void lumo_shell_state_broadcast_gesture_threshold(
    struct lumo_compositor *compositor,
    double threshold,
    uint32_t timeout_ms
) {
    char timeout_value[32];

    lumo_shell_bridge_broadcast_double(compositor, "gesture threshold",
        threshold);
    if (snprintf(timeout_value, sizeof(timeout_value), "%u", timeout_ms) > 0) {
        lumo_shell_bridge_broadcast_line(compositor, "gesture timeout_ms",
            timeout_value);
    }
}

void lumo_shell_state_broadcast_rotation(
    struct lumo_compositor *compositor,
    enum lumo_rotation rotation
) {
    lumo_shell_bridge_broadcast_line(compositor, "rotation",
        lumo_rotation_name(rotation));
}

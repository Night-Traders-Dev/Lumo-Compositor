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

bool lumo_shell_copy_path(
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

bool lumo_shell_join_path(
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

bool lumo_shell_parent_directory(
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
    const enum lumo_shell_mode modes[] = {
        LUMO_SHELL_MODE_BACKGROUND,
        LUMO_SHELL_MODE_LAUNCHER,
        LUMO_SHELL_MODE_OSK,
        LUMO_SHELL_MODE_GESTURE,
        LUMO_SHELL_MODE_STATUS,
        LUMO_SHELL_MODE_SIDEBAR,
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

    wlr_log(WLR_INFO, "shell: launching unified client from %s",
        state->binary_path);

    /* launch a single unified shell process that manages all 6 surfaces.
     * This eliminates the configure roundtrip timing issues that cause
     * panels/launcher to not render in non-unified mode with the GPU
     * compositor. */
    {
        bool spawned = false;
        for (int attempt = 0; attempt < 3 && !spawned; attempt++) {
            if (attempt > 0) {
                wlr_log(WLR_INFO, "shell: retry %d for unified client",
                    attempt);
                usleep(100000);
            }
            {
                pid_t pid = fork();
                if (pid < 0) {
                    wlr_log_errno(WLR_ERROR, "shell: fork failed");
                } else if (pid == 0) {
                    setsid();
                    execl(state->binary_path, state->binary_path,
                        "--unified", (char *)NULL);
                    _exit(127);
                } else {
                    state->processes[0].pid = pid;
                    state->processes[0].mode = LUMO_SHELL_MODE_BACKGROUND;
                    spawned = true;
                    wlr_log(WLR_INFO, "shell: unified client pid=%d",
                        (int)pid);
                }
            }
        }
        if (!spawned) {
            wlr_log(WLR_ERROR, "shell: failed to spawn unified client");
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


void lumo_shell_autostart_poll(struct lumo_compositor *compositor) {
    struct lumo_shell_state *state;

    lumo_shell_reap_children(compositor);

    if (compositor == NULL) {
        return;
    }
    state = compositor->shell_state;
    if (state == NULL) {
        return;
    }
    if (state->state_broadcast_pending) {
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
    if (state->state_broadcast_source != NULL) {
        wl_event_source_remove(state->state_broadcast_source);
        state->state_broadcast_source = NULL;
    }
    if (compositor->weather_timer != NULL) {
        wl_event_source_remove(compositor->weather_timer);
        compositor->weather_timer = NULL;
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

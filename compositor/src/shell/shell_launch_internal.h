/*
 * shell_launch_internal.h — shared struct definitions and cross-file
 * declarations for the shell_launch split modules.
 */
#ifndef LUMO_SHELL_LAUNCH_INTERNAL_H
#define LUMO_SHELL_LAUNCH_INTERNAL_H

#include "lumo/compositor.h"
#include "lumo/shell_protocol.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-server-core.h>

/* ── struct definitions ──────────────────────────────────────────── */

struct lumo_shell_process {
    enum lumo_shell_mode mode;
    pid_t pid;
};

struct lumo_shell_bridge_client {
    struct wl_list link;
    struct wl_event_source *source;
    int fd;
    uint32_t protocol_version;
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
    struct wl_event_source *state_broadcast_source;
    bool stopping;
    bool state_broadcast_pending;
    char binary_path[PATH_MAX];
    size_t count;
    struct lumo_shell_process processes[6];
    struct lumo_shell_bridge bridge;
};

/* ── shell_launch.c helpers used across files ────────────────────── */

bool lumo_shell_copy_path(
    char *buffer, size_t buffer_size, const char *path);
bool lumo_shell_join_path(
    char *buffer, size_t buffer_size,
    const char *prefix, const char *suffix);
bool lumo_shell_parent_directory(
    const char *path, char *buffer, size_t buffer_size);

/* ── shell_bridge.c entry points ─────────────────────────────────── */

bool lumo_shell_bridge_start(struct lumo_compositor *compositor);
void lumo_shell_bridge_stop(struct lumo_shell_state *state);
void lumo_shell_bridge_remove_client(
    struct lumo_shell_bridge *bridge,
    struct lumo_shell_bridge_client *client);
void lumo_shell_bridge_broadcast_state(
    struct lumo_compositor *compositor);
void lumo_shell_state_broadcast_idle(void *data);
void lumo_shell_mark_state_dirty(
    struct lumo_compositor *compositor);
bool lumo_shell_bridge_output_size(
    struct lumo_compositor *compositor,
    uint32_t *width,
    uint32_t *height);

/* ── shell_hw.c entry points ─────────────────────────────────────── */

uint32_t lumo_read_brightness_pct(void);
void lumo_write_brightness_pct(uint32_t pct);
uint32_t lumo_read_volume_pct(void);
void lumo_write_volume_pct(uint32_t pct);
void lumo_shell_capture_screenshot_async(
    struct lumo_compositor *compositor);
void lumo_shell_play_boot_sound(void);
int lumo_weather_timer_cb(void *data);

#endif /* LUMO_SHELL_LAUNCH_INTERNAL_H */

/*
 * shell_client.c — Lumo shell client main file.
 *
 * Contains buffer management, surface configuration, Wayland init,
 * event loop, and main(). Rendering, input, drawing primitives, and
 * protocol handling live in their own shell_*.c modules.
 */
#include "shell_client_internal.h"
#include "lumo/version.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* ── SHM buffer management ────────────────────────────────────────── */

static int lumo_create_shm_file(size_t size) {
    char template[] = "/tmp/lumo-shell-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0) return -1;
    unlink(template);
    if (ftruncate(fd, (off_t)size) != 0) { close(fd); return -1; }
    return fd;
}

static void lumo_shell_buffer_destroy(struct lumo_shell_buffer *buffer) {
    if (buffer == NULL) return;
    if (buffer->buffer != NULL) {
        wl_buffer_destroy(buffer->buffer);
        buffer->buffer = NULL;
    }
    if (buffer->pool != NULL) {
        wl_shm_pool_destroy(buffer->pool);
        buffer->pool = NULL;
    }
    if (buffer->data != NULL) {
        munmap(buffer->data, buffer->size);
        buffer->data = NULL;
    }
    if (buffer->fd >= 0) {
        close(buffer->fd);
        buffer->fd = -1;
    }
    free(buffer);
}

static void lumo_shell_buffer_release(
    void *data, struct wl_buffer *wl_buffer
) {
    struct lumo_shell_buffer *buffer = data;
    (void)wl_buffer;
    if (buffer != NULL) buffer->busy = false;
}

static struct lumo_shell_buffer *lumo_shell_alloc_buffer(
    struct lumo_shell_client *client, uint32_t width, uint32_t height
) {
    struct lumo_shell_buffer *buffer;
    size_t stride, size;
    if (width > SIZE_MAX / 4u) return NULL;
    stride = width * 4u;
    if (height > 0 && stride > SIZE_MAX / height) return NULL;
    size = stride * height;
    int fd = lumo_create_shm_file(size);
    if (fd < 0) return NULL;

    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) { close(fd); return NULL; }

    buffer->client = client;
    buffer->fd = fd;
    buffer->size = size;
    buffer->width = width;
    buffer->height = height;
    buffer->busy = false;
    buffer->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->data == MAP_FAILED) { close(fd); free(buffer); return NULL; }

    buffer->pool = wl_shm_create_pool(client->shm, fd, (int)size);
    if (buffer->pool == NULL) {
        munmap(buffer->data, size); close(fd); free(buffer); return NULL;
    }

    buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, 0, (int)width,
        (int)height, (int)(width * 4u), WL_SHM_FORMAT_ARGB8888);
    if (buffer->buffer == NULL) {
        wl_shm_pool_destroy(buffer->pool);
        munmap(buffer->data, size); close(fd); free(buffer); return NULL;
    }

    buffer->release.release = lumo_shell_buffer_release;
    wl_buffer_add_listener(buffer->buffer, &buffer->release, buffer);
    return buffer;
}

static struct lumo_shell_buffer *lumo_shell_get_free_buffer(
    struct lumo_shell_client *client, uint32_t width, uint32_t height
) {
    for (int i = 0; i < 2; i++) {
        struct lumo_shell_buffer *buf = client->buffers[i];
        if (buf != NULL && !buf->busy &&
                buf->width == width && buf->height == height)
            return buf;
    }
    for (int i = 0; i < 2; i++) {
        if (client->buffers[i] == NULL || !client->buffers[i]->busy) {
            if (client->buffers[i] != NULL) {
                lumo_shell_buffer_destroy(client->buffers[i]);
                client->buffers[i] = NULL;
            }
            client->buffers[i] = lumo_shell_alloc_buffer(client, width, height);
            return client->buffers[i];
        }
    }
    /* both busy — force-recycle */
    int victim = (client->buffer == client->buffers[0]) ? 1 : 0;
    lumo_shell_buffer_destroy(client->buffers[victim]);
    client->buffers[victim] = lumo_shell_alloc_buffer(client, width, height);
    return client->buffers[victim];
}

/* ── surface configuration ────────────────────────────────────────── */

static bool lumo_shell_surface_config_equal(
    const struct lumo_shell_surface_config *lhs,
    const struct lumo_shell_surface_config *rhs
) {
    if (lhs == NULL || rhs == NULL) return false;
    return lhs->mode == rhs->mode &&
        lhs->name == rhs->name &&
        lhs->width == rhs->width &&
        lhs->height == rhs->height &&
        lhs->anchor == rhs->anchor &&
        lhs->exclusive_zone == rhs->exclusive_zone &&
        lhs->margin_top == rhs->margin_top &&
        lhs->margin_right == rhs->margin_right &&
        lhs->margin_bottom == rhs->margin_bottom &&
        lhs->margin_left == rhs->margin_left &&
        lhs->keyboard_interactive == rhs->keyboard_interactive &&
        lhs->background_rgba == rhs->background_rgba;
}

static bool lumo_shell_client_should_be_visible(
    const struct lumo_shell_client *client
) {
    if (client == NULL) return false;
    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        return client->compositor_launcher_visible ||
            client->compositor_touch_audit_active ||
            client->compositor_quick_settings_visible ||
            client->compositor_time_panel_visible;
    case LUMO_SHELL_MODE_OSK:
        return client->compositor_keyboard_visible;
    case LUMO_SHELL_MODE_GESTURE:
    case LUMO_SHELL_MODE_STATUS:
    case LUMO_SHELL_MODE_BACKGROUND:
        return true;
    default:
        return false;
    }
}

static bool lumo_shell_client_build_config(
    const struct lumo_shell_client *client, bool visible,
    struct lumo_shell_surface_config *config
) {
    uint32_t output_width = 1280, output_height = 800;
    if (client == NULL || config == NULL) return false;

    if (!visible && client->mode != LUMO_SHELL_MODE_GESTURE &&
            client->mode != LUMO_SHELL_MODE_STATUS &&
            client->mode != LUMO_SHELL_MODE_BACKGROUND)
        return lumo_shell_surface_bootstrap_config(client->mode, config);

    if (client->output_width_hint > 0)
        output_width = client->output_width_hint;
    else if (client->configured_width > 0)
        output_width = client->configured_width;

    if (client->output_height_hint > 0)
        output_height = client->output_height_hint;
    else if (client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            client->configured_height > 0)
        output_height = client->configured_height;

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        if (!lumo_shell_surface_bootstrap_config(client->mode, config))
            return false;
        config->width = 0;
        config->height = 0;
        config->anchor = LUMO_SHELL_ANCHOR_TOP | LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT | LUMO_SHELL_ANCHOR_RIGHT;
        config->keyboard_interactive = true;
        return true;
    case LUMO_SHELL_MODE_OSK:
        if (!lumo_shell_surface_config_for_mode(client->mode, output_width,
                output_height, config))
            return false;
        config->width = 0;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_GESTURE:
        if (!lumo_shell_surface_config_for_mode(client->mode, output_width,
                output_height, config))
            return lumo_shell_surface_bootstrap_config(client->mode, config);
        config->width = 0;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_STATUS:
    case LUMO_SHELL_MODE_BACKGROUND:
        if (!lumo_shell_surface_config_for_mode(client->mode, output_width,
                output_height, config))
            return lumo_shell_surface_bootstrap_config(client->mode, config);
        config->width = 0;
        config->background_rgba = 0x00000000;
        return true;
    default:
        return false;
    }
}

static bool lumo_shell_client_apply_config(
    struct lumo_shell_client *client,
    const struct lumo_shell_surface_config *config
) {
    uint32_t keyboard_interactive;
    if (client == NULL || config == NULL || client->layer_surface == NULL ||
            client->surface == NULL)
        return false;

    client->config = *config;
    zwlr_layer_surface_v1_set_size(client->layer_surface,
        config->width, config->height);
    zwlr_layer_surface_v1_set_anchor(client->layer_surface, config->anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(client->layer_surface,
        config->exclusive_zone);
    zwlr_layer_surface_v1_set_margin(client->layer_surface,
        config->margin_top, config->margin_right,
        config->margin_bottom, config->margin_left);

    keyboard_interactive = config->keyboard_interactive
        ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
        : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    zwlr_layer_surface_v1_set_keyboard_interactivity(client->layer_surface,
        keyboard_interactive);
    wl_surface_commit(client->surface);
    return true;
}

static void lumo_shell_client_update_input_region(
    struct lumo_shell_client *client, uint32_t width, uint32_t height
) {
    struct wl_region *region;
    struct lumo_rect rect = {0};

    if (client == NULL || client->surface == NULL ||
            client->compositor == NULL || width == 0 || height == 0)
        return;

    region = wl_compositor_create_region(client->compositor);
    if (region == NULL) return;

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        if (!client->compositor_touch_audit_active &&
                client->compositor_launcher_visible &&
                lumo_shell_launcher_panel_rect(width, height, &rect))
            wl_region_add(region, rect.x, rect.y, rect.width, rect.height);
        if (!client->compositor_launcher_visible &&
                (client->compositor_quick_settings_visible ||
                    client->compositor_time_panel_visible)) {
            int bar_h = 48;
            int pw = (int)width / 2;
            if (pw < 200) pw = 200;
            if (client->compositor_quick_settings_visible)
                wl_region_add(region, (int)width - pw - 8, bar_h,
                    pw + 8, (int)height - bar_h);
            if (client->compositor_time_panel_visible)
                wl_region_add(region, 0, bar_h, pw + 16, 200);
        }
        break;
    case LUMO_SHELL_MODE_OSK:
        wl_region_add(region, 0, 0, (int)width, (int)height);
        break;
    case LUMO_SHELL_MODE_GESTURE:
        if (lumo_shell_gesture_handle_rect(width, height, &rect))
            wl_region_add(region, rect.x, rect.y, rect.width, rect.height);
        break;
    case LUMO_SHELL_MODE_STATUS:
        if (client->compositor_quick_settings_visible ||
                client->compositor_time_panel_visible)
            wl_region_add(region, 0, 0, (int)width, (int)height);
        break;
    case LUMO_SHELL_MODE_BACKGROUND:
    default:
        break;
    }

    wl_surface_set_input_region(client->surface, region);
    wl_region_destroy(region);
}

/* ── surface state transitions ────────────────────────────────────── */

void lumo_shell_client_finish_hide_if_needed(
    struct lumo_shell_client *client
) {
    struct lumo_shell_surface_config hidden_config;
    if (client == NULL || client->mode == LUMO_SHELL_MODE_GESTURE ||
            client->mode == LUMO_SHELL_MODE_STATUS ||
            client->mode == LUMO_SHELL_MODE_BACKGROUND ||
            client->target_visible || client->surface_hidden)
        return;
    if (lumo_shell_client_build_config(client, false, &hidden_config) &&
            !lumo_shell_surface_config_equal(&client->config, &hidden_config))
        (void)lumo_shell_client_apply_config(client, &hidden_config);
    client->surface_hidden = true;
}

static void lumo_shell_client_begin_transition(
    struct lumo_shell_client *client, bool visible
) {
    struct lumo_shell_surface_config config;
    double current_value;
    if (client == NULL) return;

    if (client->mode == LUMO_SHELL_MODE_GESTURE ||
            client->mode == LUMO_SHELL_MODE_STATUS ||
            client->mode == LUMO_SHELL_MODE_BACKGROUND) {
        client->target_visible = true;
        client->surface_hidden = false;
        client->animation_active = false;
        return;
    }

    current_value = lumo_shell_client_animation_value(client);
    client->target_visible = visible;

    if (visible) {
        if (lumo_shell_client_build_config(client, true, &config) &&
                (client->surface_hidden ||
                    !lumo_shell_surface_config_equal(&client->config, &config)))
            (void)lumo_shell_client_apply_config(client, &config);
        client->surface_hidden = false;
    }

    if (!visible && client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            !client->compositor_launcher_visible) {
        client->animation_from = 0.0;
        client->animation_to = 0.0;
        client->animation_active = false;
        lumo_shell_client_finish_hide_if_needed(client);
        return;
    }

    client->animation_from = current_value;
    client->animation_to = visible ? 1.0 : 0.0;
    client->animation_started_msec = lumo_now_msec();
    client->animation_duration_msec = lumo_shell_transition_duration_ms(
        client->mode, visible);
    client->animation_active =
        client->animation_from != client->animation_to;

    if (!client->animation_active && !visible)
        lumo_shell_client_finish_hide_if_needed(client);
}

void lumo_shell_client_sync_surface_state(
    struct lumo_shell_client *client, bool force_layout
) {
    struct lumo_shell_surface_config config;
    bool desired_visible;
    if (client == NULL) return;

    desired_visible = lumo_shell_client_should_be_visible(client);
    if (desired_visible != client->target_visible ||
            ((client->mode == LUMO_SHELL_MODE_GESTURE ||
                client->mode == LUMO_SHELL_MODE_STATUS ||
                client->mode == LUMO_SHELL_MODE_BACKGROUND) &&
                client->surface_hidden)) {
        lumo_shell_client_begin_transition(client, desired_visible);
        return;
    }

    if ((desired_visible || client->mode == LUMO_SHELL_MODE_GESTURE ||
                client->mode == LUMO_SHELL_MODE_STATUS ||
                client->mode == LUMO_SHELL_MODE_BACKGROUND) &&
            force_layout &&
            lumo_shell_client_build_config(client, true, &config) &&
            !lumo_shell_surface_config_equal(&client->config, &config)) {
        (void)lumo_shell_client_apply_config(client, &config);
        client->surface_hidden = false;
    }
}

/* ── animation ────────────────────────────────────────────────────── */

int lumo_shell_client_animation_timeout(
    const struct lumo_shell_client *client
) {
    uint64_t now, end_time;
    if (client == NULL || !client->animation_active) {
        if (client != NULL && client->mode == LUMO_SHELL_MODE_BACKGROUND)
            return 66;
        if (client != NULL && client->mode == LUMO_SHELL_MODE_STATUS) {
            if (client->compositor_time_panel_visible) return 1000;
            return 30000;
        }
        return -1;
    }
    now = lumo_now_msec();
    end_time = client->animation_started_msec + client->animation_duration_msec;
    if (end_time <= now) return 0;
    if (end_time - now > 33u) return 33;
    return (int)(end_time - now);
}

void lumo_shell_client_tick_animation(struct lumo_shell_client *client) {
    if (client == NULL || !client->animation_active) return;
    if (lumo_now_msec() >= client->animation_started_msec +
            client->animation_duration_msec) {
        client->animation_active = false;
        if (!client->target_visible)
            lumo_shell_client_finish_hide_if_needed(client);
    }
    (void)lumo_shell_client_redraw(client);
}

/* ── drawing pipeline ─────────────────────────────────────────────── */

static bool lumo_shell_draw_buffer(
    struct lumo_shell_client *client, uint32_t width, uint32_t height
) {
    struct lumo_shell_buffer *buffer;
    const struct lumo_shell_target *active_target;

    if (client == NULL || client->shm == NULL || client->surface == NULL ||
            width == 0 || height == 0)
        return false;

    buffer = lumo_shell_get_free_buffer(client, width, height);
    if (buffer == NULL || buffer->data == NULL) return false;

    active_target = client->active_target_valid ? &client->active_target : NULL;
    lumo_render_surface(client, buffer->data, width, height, active_target);
    lumo_shell_client_update_input_region(client, width, height);
    wl_surface_attach(client->surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(client->surface, 0, 0, (int)width, (int)height);
    wl_surface_commit(client->surface);

    buffer->busy = true;
    client->buffer = buffer;
    client->configured_width = width;
    client->configured_height = height;
    client->configured = true;
    return true;
}

bool lumo_shell_client_redraw(struct lumo_shell_client *client) {
    if (client == NULL || !client->configured) return false;
    return lumo_shell_draw_buffer(client, client->configured_width,
        client->configured_height);
}

/* ── Wayland listeners ────────────────────────────────────────────── */

static void lumo_shell_seat_handle_capabilities(
    void *data, struct wl_seat *seat, uint32_t capabilities
) {
    struct lumo_shell_client *client = data;
    if (client == NULL) return;

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0) {
        if (client->pointer == NULL) {
            client->pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(client->pointer,
                &lumo_shell_pointer_listener, client);
        }
    } else if (client->pointer != NULL) {
        wl_pointer_release(client->pointer);
        client->pointer = NULL;
        client->pointer_pressed = false;
        client->pointer_position_valid = false;
        if (!client->touch_pressed)
            lumo_shell_client_clear_active_target(client);
    }

    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0) {
        if (client->touch == NULL) {
            client->touch = wl_seat_get_touch(seat);
            wl_touch_add_listener(client->touch,
                &lumo_shell_touch_listener, client);
        }
    } else if (client->touch != NULL) {
        wl_touch_release(client->touch);
        client->touch = NULL;
        client->touch_pressed = false;
        client->active_touch_id = -1;
        if (!client->pointer_pressed)
            lumo_shell_client_clear_active_target(client);
    }
}

static void lumo_shell_seat_handle_name(
    void *data, struct wl_seat *seat, const char *name
) {
    (void)data; (void)seat; (void)name;
}

static const struct wl_seat_listener lumo_shell_seat_listener = {
    .capabilities = lumo_shell_seat_handle_capabilities,
    .name = lumo_shell_seat_handle_name,
};

static void lumo_shell_handle_configure(
    void *data, struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial, uint32_t width, uint32_t height
) {
    struct lumo_shell_client *client = data;
    uint32_t draw_width, draw_height;
    (void)layer_surface;
    if (client == NULL) return;

    zwlr_layer_surface_v1_ack_configure(client->layer_surface, serial);
    draw_width = width != 0 ? width : client->config.width;
    draw_height = height != 0 ? height : client->config.height;
    if (draw_width == 0 || draw_height == 0) return;

    fprintf(stderr, "lumo-shell: %s configure %ux%u\n",
        lumo_shell_mode_name(client->mode), draw_width, draw_height);

    if (client->mode == LUMO_SHELL_MODE_LAUNCHER) {
        client->output_width_hint = draw_width;
        client->output_height_hint = draw_height;
    } else if (client->output_width_hint == 0) {
        client->output_width_hint = draw_width;
    }

    if (!lumo_shell_draw_buffer(client, draw_width, draw_height))
        fprintf(stderr, "lumo-shell: failed to render %s surface\n",
            client->config.name != NULL ? client->config.name : "shell");
}

static void lumo_shell_handle_closed(
    void *data, struct zwlr_layer_surface_v1 *layer_surface
) {
    struct lumo_shell_client *client = data;
    (void)layer_surface;
    if (client != NULL && client->display != NULL) {
        wl_display_disconnect(client->display);
        client->display = NULL;
    }
}

static const struct zwlr_layer_surface_v1_listener
    lumo_shell_surface_listener = {
    .configure = lumo_shell_handle_configure,
    .closed = lumo_shell_handle_closed,
};

static void lumo_shell_registry_add(
    void *data, struct wl_registry *registry,
    uint32_t name, const char *interface, uint32_t version
) {
    struct lumo_shell_client *client = data;
    if (client == NULL) return;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        client->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, version < 4 ? version : 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        client->shm = wl_registry_bind(registry, name,
            &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        client->seat = wl_registry_bind(registry, name,
            &wl_seat_interface, version < 5 ? version : 5);
        if (client->seat != NULL)
            wl_seat_add_listener(client->seat,
                &lumo_shell_seat_listener, client);
    } else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        client->layer_shell = wl_registry_bind(registry, name,
            &zwlr_layer_shell_v1_interface, 1);
    }
}

static void lumo_shell_registry_remove(
    void *data, struct wl_registry *registry, uint32_t name
) {
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener lumo_shell_registry_listener = {
    .global = lumo_shell_registry_add,
    .global_remove = lumo_shell_registry_remove,
};

/* ── surface creation ─────────────────────────────────────────────── */

static bool lumo_shell_create_surface(struct lumo_shell_client *client) {
    if (client == NULL || client->compositor == NULL ||
            client->layer_shell == NULL)
        return false;

    client->target_visible = client->mode == LUMO_SHELL_MODE_GESTURE ||
        client->mode == LUMO_SHELL_MODE_STATUS ||
        client->mode == LUMO_SHELL_MODE_BACKGROUND;
    client->surface_hidden = client->mode != LUMO_SHELL_MODE_GESTURE &&
        client->mode != LUMO_SHELL_MODE_STATUS &&
        client->mode != LUMO_SHELL_MODE_BACKGROUND;
    client->animation_active = false;
    client->animation_from = client->target_visible ? 1.0 : 0.0;
    client->animation_to = client->animation_from;
    client->animation_duration_msec = 0;

    if (!lumo_shell_surface_bootstrap_config(client->mode, &client->config))
        return false;

    client->surface = wl_compositor_create_surface(client->compositor);
    if (client->surface == NULL) return false;

    {
        uint32_t layer;
        if (client->mode == LUMO_SHELL_MODE_LAUNCHER ||
                client->mode == LUMO_SHELL_MODE_OSK)
            layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
        else if (client->mode == LUMO_SHELL_MODE_BACKGROUND)
            layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
        else
            layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;

        client->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            client->layer_shell, client->surface, NULL, layer,
            client->config.name != NULL ? client->config.name : "lumo-shell");
    }
    if (client->layer_surface == NULL) return false;

    zwlr_layer_surface_v1_add_listener(client->layer_surface,
        &lumo_shell_surface_listener, client);
    return lumo_shell_client_apply_config(client, &client->config);
}

/* ── event loop ───────────────────────────────────────────────────── */

static int lumo_shell_client_run(struct lumo_shell_client *client) {
    int display_fd, timeout_ms;
    if (client == NULL || client->display == NULL) return -1;

    display_fd = wl_display_get_fd(client->display);
    while (client->display != NULL) {
        struct pollfd fds[2];
        nfds_t nfds = 0;
        int poll_result;

        if (wl_display_dispatch_pending(client->display) == -1) return -1;
        if (wl_display_flush(client->display) < 0 && errno != EAGAIN)
            return -1;

        fds[nfds].fd = display_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        if (client->state_fd >= 0) {
            fds[nfds].fd = client->state_fd;
            fds[nfds].events = POLLIN | POLLHUP | POLLERR;
            nfds++;
        }

        timeout_ms = lumo_shell_client_animation_timeout(client);
        poll_result = poll(fds, nfds, timeout_ms);
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        if (poll_result == 0) {
            if (client->mode == LUMO_SHELL_MODE_STATUS ||
                    client->mode == LUMO_SHELL_MODE_BACKGROUND)
                (void)lumo_shell_client_redraw(client);
            lumo_shell_client_tick_animation(client);
            continue;
        }

        if (client->state_fd >= 0 && nfds > 1 &&
                (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (!lumo_shell_client_pump_protocol(client)) {
                close(client->state_fd);
                client->state_fd = -1;
            }
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_dispatch(client->display) == -1) break;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (client->animation_active)
            lumo_shell_client_tick_animation(client);
    }
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────── */

static void lumo_shell_print_usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [--mode launcher|osk|gesture|status|background]\n",
        argv0);
}

static bool lumo_shell_parse_mode(
    const char *value, enum lumo_shell_mode *mode
) {
    if (value == NULL || mode == NULL) return false;
    if (strcmp(value, "launcher") == 0) {
        *mode = LUMO_SHELL_MODE_LAUNCHER; return true; }
    if (strcmp(value, "osk") == 0) {
        *mode = LUMO_SHELL_MODE_OSK; return true; }
    if (strcmp(value, "gesture") == 0) {
        *mode = LUMO_SHELL_MODE_GESTURE; return true; }
    if (strcmp(value, "status") == 0) {
        *mode = LUMO_SHELL_MODE_STATUS; return true; }
    if (strcmp(value, "background") == 0) {
        *mode = LUMO_SHELL_MODE_BACKGROUND; return true; }
    return false;
}

int main(int argc, char **argv) {
    struct lumo_shell_client client = {
        .mode = LUMO_SHELL_MODE_LAUNCHER,
        .state_fd = -1,
        .active_touch_id = -1,
        .next_request_id = 1,
        .compositor_launcher_visible = false,
        .compositor_keyboard_visible = false,
        .compositor_scrim_state = LUMO_SHELL_REMOTE_SCRIM_HIDDEN,
        .compositor_rotation_degrees = 0,
        .compositor_gesture_threshold = 32.0,
        .compositor_gesture_timeout_ms = 90,
        .compositor_keyboard_resize_pending = false,
        .compositor_keyboard_resize_acked = true,
        .compositor_keyboard_resize_serial = 0,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            lumo_shell_print_usage(argv[0]); return 0;
        }
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (!lumo_shell_parse_mode(argv[++i], &client.mode)) {
                fprintf(stderr, "lumo-shell: invalid mode '%s'\n", argv[i]);
                lumo_shell_print_usage(argv[0]); return 1;
            }
            continue;
        }
        fprintf(stderr, "lumo-shell: unknown argument '%s'\n", argv[i]);
        lumo_shell_print_usage(argv[0]); return 1;
    }

    client.display = wl_display_connect(NULL);
    if (client.display == NULL) {
        fprintf(stderr,
            "lumo-shell: failed to connect to Wayland display\n");
        return 1;
    }

    lumo_shell_protocol_stream_init(&client.protocol_stream);

    client.registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(client.registry,
        &lumo_shell_registry_listener, &client);
    wl_display_roundtrip(client.display);

    if (client.compositor == NULL || client.shm == NULL ||
            client.layer_shell == NULL) {
        fprintf(stderr,
            "lumo-shell: missing compositor, shm, or layer-shell global\n");
        wl_display_disconnect(client.display);
        return 1;
    }

    if (!lumo_shell_create_surface(&client)) {
        fprintf(stderr, "lumo-shell: failed to create shell surface\n");
        wl_display_disconnect(client.display);
        return 1;
    }

    client.state_fd = lumo_shell_client_connect_state_socket(&client);
    if (client.state_fd >= 0) {
        fprintf(stderr, "lumo-shell: connected state socket %s\n",
            client.state_socket_path);
        (void)lumo_shell_client_pump_protocol(&client);
    }

    (void)lumo_shell_client_run(&client);

    if (client.display != NULL)
        wl_display_disconnect(client.display);

    return 0;
}

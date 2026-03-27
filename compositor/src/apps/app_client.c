#define _DEFAULT_SOURCE
#include "lumo/app.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

struct lumo_app_client;

struct lumo_app_buffer {
    struct lumo_app_client *client;
    struct wl_buffer *buffer;
    struct wl_shm_pool *pool;
    void *data;
    int fd;
    size_t size;
    struct wl_buffer_listener release;
};

struct lumo_app_client {
    enum lumo_app_id app_id;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_touch *touch;
    struct xdg_wm_base *wm_base;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct lumo_app_buffer *buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pending_width;
    uint32_t pending_height;
    bool configured;
    bool running;
    bool pointer_pressed;
    bool pointer_position_valid;
    bool touch_pressed;
    bool close_active;
    int32_t active_touch_id;
    double pointer_x;
    double pointer_y;
    char browse_path[1024];
    double touch_down_x;
    double touch_down_y;
    int scroll_offset;
    bool stopwatch_running;
    uint64_t stopwatch_start_ms;
    uint64_t stopwatch_accumulated_ms;
    int selected_row;
    char notes[8][128];
    int note_count;
    int note_editing;
};

static bool lumo_app_client_redraw(struct lumo_app_client *client);

static int lumo_app_create_shm_file(size_t size) {
    char template[] = "/tmp/lumo-app-XXXXXX";
    int fd = mkstemp(template);

    if (fd < 0) {
        return -1;
    }

    unlink(template);
    if (ftruncate(fd, (off_t)size) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void lumo_app_buffer_release(
    void *data,
    struct wl_buffer *wl_buffer
) {
    struct lumo_app_buffer *buffer = data;

    (void)wl_buffer;
    if (buffer == NULL) {
        return;
    }

    if (buffer->client != NULL && buffer->client->buffer == buffer) {
        buffer->client->buffer = NULL;
    }
    if (buffer->buffer != NULL) {
        wl_buffer_destroy(buffer->buffer);
    }
    if (buffer->pool != NULL) {
        wl_shm_pool_destroy(buffer->pool);
    }
    if (buffer->data != NULL) {
        munmap(buffer->data, buffer->size);
    }
    if (buffer->fd >= 0) {
        close(buffer->fd);
    }

    free(buffer);
}

static bool lumo_app_client_close_contains(
    const struct lumo_app_client *client,
    double x,
    double y
) {
    struct lumo_rect rect = {0};

    if (client == NULL ||
            !lumo_app_close_rect(client->width, client->height, &rect)) {
        return false;
    }

    return lumo_rect_contains(&rect, x, y);
}

static void lumo_app_client_set_close_active(
    struct lumo_app_client *client,
    bool close_active
) {
    if (client == NULL || client->close_active == close_active) {
        return;
    }

    client->close_active = close_active;
    (void)lumo_app_client_redraw(client);
}

static bool lumo_app_client_draw_buffer(struct lumo_app_client *client) {
    struct lumo_app_buffer *buffer;
    size_t stride;
    size_t size;
    int fd;

    if (client == NULL || client->shm == NULL || client->surface == NULL ||
            client->width == 0 || client->height == 0) {
        return false;
    }

    stride = (size_t)client->width * 4u;
    size = stride * (size_t)client->height;
    fd = lumo_app_create_shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "lumo-app: failed to create shm file: %s\n",
            strerror(errno));
        return false;
    }

    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        close(fd);
        return false;
    }

    buffer->client = client;
    buffer->fd = fd;
    buffer->size = size;
    buffer->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->data == MAP_FAILED) {
        fprintf(stderr, "lumo-app: mmap failed: %s\n", strerror(errno));
        close(fd);
        free(buffer);
        return false;
    }

    buffer->pool = wl_shm_create_pool(client->shm, fd, (int)size);
    if (buffer->pool == NULL) {
        fprintf(stderr, "lumo-app: failed to create shm pool\n");
        munmap(buffer->data, size);
        close(fd);
        free(buffer);
        return false;
    }

    buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, 0,
        (int)client->width, (int)client->height, (int)stride,
        WL_SHM_FORMAT_ARGB8888);
    if (buffer->buffer == NULL) {
        fprintf(stderr, "lumo-app: failed to create shm buffer\n");
        wl_shm_pool_destroy(buffer->pool);
        munmap(buffer->data, size);
        close(fd);
        free(buffer);
        return false;
    }

    buffer->release.release = lumo_app_buffer_release;
    wl_buffer_add_listener(buffer->buffer, &buffer->release, buffer);

    {
        uint64_t sw_elapsed = client->stopwatch_accumulated_ms;
        if (client->stopwatch_running && client->stopwatch_start_ms > 0) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 +
                (uint64_t)ts.tv_nsec / 1000000;
            sw_elapsed += now_ms - client->stopwatch_start_ms;
        }
        struct lumo_app_render_context ctx = {
            .app_id = client->app_id,
            .close_active = client->close_active,
            .browse_path = client->browse_path,
            .scroll_offset = client->scroll_offset,
            .stopwatch_running = client->stopwatch_running,
            .stopwatch_elapsed_ms = sw_elapsed,
            .selected_row = client->selected_row,
            .note_count = client->note_count,
            .note_editing = client->note_editing,
        };
        memcpy(ctx.notes, client->notes, sizeof(ctx.notes));
        lumo_app_render(&ctx, buffer->data, client->width, client->height);
    }
    wl_surface_attach(client->surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(client->surface, 0, 0, (int)client->width,
        (int)client->height);
    wl_surface_commit(client->surface);
    client->buffer = buffer;
    return true;
}

static bool lumo_app_client_redraw(struct lumo_app_client *client) {
    if (client == NULL || !client->configured) {
        return false;
    }

    return lumo_app_client_draw_buffer(client);
}

static void lumo_app_wm_base_handle_ping(
    void *data,
    struct xdg_wm_base *xdg_wm_base,
    uint32_t serial
) {
    (void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener lumo_app_wm_base_listener = {
    .ping = lumo_app_wm_base_handle_ping,
};

static void lumo_app_xdg_surface_handle_configure(
    void *data,
    struct xdg_surface *xdg_surface,
    uint32_t serial
) {
    struct lumo_app_client *client = data;
    uint32_t width;
    uint32_t height;

    if (client == NULL) {
        return;
    }

    xdg_surface_ack_configure(xdg_surface, serial);
    width = client->pending_width != 0 ? client->pending_width : client->width;
    height = client->pending_height != 0 ? client->pending_height : client->height;
    if (width == 0) {
        width = 1280;
    }
    if (height == 0) {
        height = 800;
    }

    client->width = width;
    client->height = height;
    client->configured = true;
    if (!lumo_app_client_redraw(client)) {
        fprintf(stderr, "lumo-app: failed to render app surface\n");
    }
}

static const struct xdg_surface_listener lumo_app_xdg_surface_listener = {
    .configure = lumo_app_xdg_surface_handle_configure,
};

static void lumo_app_xdg_toplevel_handle_configure(
    void *data,
    struct xdg_toplevel *xdg_toplevel,
    int32_t width,
    int32_t height,
    struct wl_array *states
) {
    struct lumo_app_client *client = data;

    (void)xdg_toplevel;
    (void)states;
    if (client == NULL) {
        return;
    }

    if (width > 0) {
        client->pending_width = (uint32_t)width;
    }
    if (height > 0) {
        client->pending_height = (uint32_t)height;
    }
}

static void lumo_app_xdg_toplevel_handle_close(
    void *data,
    struct xdg_toplevel *xdg_toplevel
) {
    struct lumo_app_client *client = data;

    (void)xdg_toplevel;
    if (client != NULL) {
        client->running = false;
    }
}

static const struct xdg_toplevel_listener lumo_app_xdg_toplevel_listener = {
    .configure = lumo_app_xdg_toplevel_handle_configure,
    .close = lumo_app_xdg_toplevel_handle_close,
};

static void lumo_app_pointer_handle_enter(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial,
    struct wl_surface *surface,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y
) {
    struct lumo_app_client *client = data;

    (void)wl_pointer;
    (void)serial;
    (void)surface;
    if (client == NULL) {
        return;
    }

    client->pointer_x = wl_fixed_to_double(surface_x);
    client->pointer_y = wl_fixed_to_double(surface_y);
    client->pointer_position_valid = true;
}

static void lumo_app_pointer_handle_leave(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial,
    struct wl_surface *surface
) {
    struct lumo_app_client *client = data;

    (void)wl_pointer;
    (void)serial;
    (void)surface;
    if (client == NULL) {
        return;
    }

    client->pointer_position_valid = false;
    if (!client->touch_pressed) {
        lumo_app_client_set_close_active(client, false);
    }
}

static void lumo_app_pointer_handle_motion(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t time,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y
) {
    struct lumo_app_client *client = data;

    (void)wl_pointer;
    (void)time;
    if (client == NULL) {
        return;
    }

    client->pointer_x = wl_fixed_to_double(surface_x);
    client->pointer_y = wl_fixed_to_double(surface_y);
    client->pointer_position_valid = true;
    if (client->pointer_pressed) {
        lumo_app_client_set_close_active(client,
            lumo_app_client_close_contains(client, client->pointer_x,
                client->pointer_y));
    }
}

static void lumo_app_pointer_handle_button(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    uint32_t state
) {
    struct lumo_app_client *client = data;
    bool should_close = false;

    (void)wl_pointer;
    (void)serial;
    (void)time;
    (void)button;
    if (client == NULL || !client->pointer_position_valid) {
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        client->pointer_pressed = true;
        lumo_app_client_set_close_active(client,
            lumo_app_client_close_contains(client, client->pointer_x,
                client->pointer_y));
        return;
    }

    should_close = client->pointer_pressed && client->close_active &&
        lumo_app_client_close_contains(client, client->pointer_x,
            client->pointer_y);
    client->pointer_pressed = false;
    lumo_app_client_set_close_active(client, false);
    if (should_close) {
        client->running = false;
    }
}

static void lumo_app_pointer_handle_axis(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t time,
    uint32_t axis,
    wl_fixed_t value
) {
    (void)data;
    (void)wl_pointer;
    (void)time;
    (void)axis;
    (void)value;
}

static void lumo_app_pointer_handle_frame(
    void *data,
    struct wl_pointer *wl_pointer
) {
    (void)data;
    (void)wl_pointer;
}

static const struct wl_pointer_listener lumo_app_pointer_listener = {
    .enter = lumo_app_pointer_handle_enter,
    .leave = lumo_app_pointer_handle_leave,
    .motion = lumo_app_pointer_handle_motion,
    .button = lumo_app_pointer_handle_button,
    .axis = lumo_app_pointer_handle_axis,
    .frame = lumo_app_pointer_handle_frame,
};

static void lumo_app_touch_handle_down(
    void *data,
    struct wl_touch *wl_touch,
    uint32_t serial,
    uint32_t time,
    struct wl_surface *surface,
    int32_t id,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct lumo_app_client *client = data;

    (void)wl_touch;
    (void)serial;
    (void)time;
    (void)surface;
    if (client == NULL) {
        return;
    }

    client->touch_pressed = true;
    client->active_touch_id = id;
    client->touch_down_x = wl_fixed_to_double(x);
    client->touch_down_y = wl_fixed_to_double(y);
    lumo_app_client_set_close_active(client,
        lumo_app_client_close_contains(client, client->touch_down_x,
            client->touch_down_y));
}

static void lumo_app_touch_handle_up(
    void *data,
    struct wl_touch *wl_touch,
    uint32_t serial,
    uint32_t time,
    int32_t id
) {
    struct lumo_app_client *client = data;
    bool should_close;

    (void)wl_touch;
    (void)serial;
    (void)time;
    if (client == NULL || !client->touch_pressed ||
            client->active_touch_id != id) {
        return;
    }

    should_close = client->close_active;
    client->touch_pressed = false;
    client->active_touch_id = -1;
    lumo_app_client_set_close_active(client, false);
    if (should_close) {
        client->running = false;
        return;
    }

    if (client->app_id == LUMO_APP_CLOCK && client->width > 0 &&
            client->height > 0) {
        int card = lumo_app_clock_card_at(client->width, client->height,
            client->touch_down_x, client->touch_down_y);
        if (card == 1) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 +
                (uint64_t)ts.tv_nsec / 1000000;
            if (client->stopwatch_running) {
                client->stopwatch_accumulated_ms +=
                    now_ms - client->stopwatch_start_ms;
                client->stopwatch_running = false;
            } else {
                client->stopwatch_start_ms = now_ms;
                client->stopwatch_running = true;
            }
            (void)lumo_app_client_redraw(client);
        } else if (card == 2) {
            client->stopwatch_accumulated_ms = 0;
            client->stopwatch_running = false;
            client->stopwatch_start_ms = 0;
            (void)lumo_app_client_redraw(client);
        }
    }

    if (client->app_id == LUMO_APP_FILES && client->width > 0 &&
            client->height > 0) {
        int entry_idx = lumo_app_files_entry_at(client->width, client->height,
            client->touch_down_x, client->touch_down_y);
        if (entry_idx >= 0) {
            int adjusted = entry_idx + client->scroll_offset;
            DIR *dir = opendir(client->browse_path);
            if (dir != NULL) {
                struct dirent *entry;
                int visible = 0;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] == '.') {
                        continue;
                    }
                    if (visible == adjusted) {
                        if (entry->d_type == DT_DIR) {
                            size_t plen = strlen(client->browse_path);
                            if (plen + 1 + strlen(entry->d_name) + 1 <
                                    sizeof(client->browse_path)) {
                                const char *sep = (plen > 1) ? "/" : "";
                                snprintf(client->browse_path + plen,
                                    sizeof(client->browse_path) - plen,
                                    "%s%s", sep, entry->d_name);
                                client->scroll_offset = 0;
                                (void)lumo_app_client_redraw(client);
                            }
                        } else {
                            client->selected_row = adjusted;
                            (void)lumo_app_client_redraw(client);
                        }
                        break;
                    }
                    visible++;
                }
                closedir(dir);
            }
        } else if (client->touch_down_y < 130.0 &&
                client->touch_down_y > 100.0) {
            char *last_slash = strrchr(client->browse_path, '/');
            if (last_slash != NULL && last_slash != client->browse_path) {
                *last_slash = '\0';
                client->scroll_offset = 0;
                client->selected_row = -1;
                (void)lumo_app_client_redraw(client);
            } else if (last_slash == client->browse_path) {
                client->browse_path[1] = '\0';
                client->scroll_offset = 0;
                client->selected_row = -1;
                (void)lumo_app_client_redraw(client);
            }
        }
    }

    if (client->app_id == LUMO_APP_NOTES && client->width > 0 &&
            client->height > 0) {
        int row = lumo_app_notes_row_at(client->width, client->height,
            client->touch_down_x, client->touch_down_y);
        if (row >= 0 && row < client->note_count) {
            client->selected_row = (client->selected_row == row) ? -1 : row;
            (void)lumo_app_client_redraw(client);
        } else if (row == -2) {
            if (client->note_count < 8) {
                snprintf(client->notes[client->note_count],
                    sizeof(client->notes[0]), "NOTE %d", client->note_count + 1);
                client->note_count++;
                (void)lumo_app_client_redraw(client);
            }
        }
    }

    if (client->app_id == LUMO_APP_SETTINGS && client->width > 0 &&
            client->height > 0) {
        int row = lumo_app_settings_row_at(client->width, client->height,
            client->touch_down_x, client->touch_down_y);
        if (row >= 0) {
            client->selected_row = (client->selected_row == row) ? -1 : row;
            (void)lumo_app_client_redraw(client);
        }
    }
}

static void lumo_app_touch_handle_motion(
    void *data,
    struct wl_touch *wl_touch,
    uint32_t time,
    int32_t id,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct lumo_app_client *client = data;

    (void)wl_touch;
    (void)time;
    if (client == NULL || !client->touch_pressed ||
            client->active_touch_id != id) {
        return;
    }

    {
        double cur_y = wl_fixed_to_double(y);
        double cur_x = wl_fixed_to_double(x);
        lumo_app_client_set_close_active(client,
            lumo_app_client_close_contains(client, cur_x, cur_y));

        if (client->app_id == LUMO_APP_FILES) {
            double dy = client->touch_down_y - cur_y;
            if (dy > 30.0) {
                client->scroll_offset++;
                client->touch_down_y = cur_y;
                (void)lumo_app_client_redraw(client);
            } else if (dy < -30.0 && client->scroll_offset > 0) {
                client->scroll_offset--;
                client->touch_down_y = cur_y;
                (void)lumo_app_client_redraw(client);
            }
        }
    }
}

static void lumo_app_touch_handle_frame(
    void *data,
    struct wl_touch *wl_touch
) {
    (void)data;
    (void)wl_touch;
}

static void lumo_app_touch_handle_cancel(
    void *data,
    struct wl_touch *wl_touch
) {
    struct lumo_app_client *client = data;

    (void)wl_touch;
    if (client == NULL) {
        return;
    }

    client->touch_pressed = false;
    client->active_touch_id = -1;
    lumo_app_client_set_close_active(client, false);
}

static void lumo_app_touch_handle_shape(
    void *data,
    struct wl_touch *wl_touch,
    int32_t id,
    wl_fixed_t major,
    wl_fixed_t minor
) {
    (void)data;
    (void)wl_touch;
    (void)id;
    (void)major;
    (void)minor;
}

static void lumo_app_touch_handle_orientation(
    void *data,
    struct wl_touch *wl_touch,
    int32_t id,
    wl_fixed_t orientation
) {
    (void)data;
    (void)wl_touch;
    (void)id;
    (void)orientation;
}

static const struct wl_touch_listener lumo_app_touch_listener = {
    .down = lumo_app_touch_handle_down,
    .up = lumo_app_touch_handle_up,
    .motion = lumo_app_touch_handle_motion,
    .frame = lumo_app_touch_handle_frame,
    .cancel = lumo_app_touch_handle_cancel,
    .shape = lumo_app_touch_handle_shape,
    .orientation = lumo_app_touch_handle_orientation,
};

static void lumo_app_seat_handle_capabilities(
    void *data,
    struct wl_seat *seat,
    uint32_t capabilities
) {
    struct lumo_app_client *client = data;

    if (client == NULL) {
        return;
    }

    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0) {
        if (client->pointer == NULL) {
            client->pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(client->pointer, &lumo_app_pointer_listener,
                client);
        }
    } else if (client->pointer != NULL) {
        wl_pointer_release(client->pointer);
        client->pointer = NULL;
        client->pointer_pressed = false;
        client->pointer_position_valid = false;
        if (!client->touch_pressed) {
            lumo_app_client_set_close_active(client, false);
        }
    }

    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0) {
        if (client->touch == NULL) {
            client->touch = wl_seat_get_touch(seat);
            wl_touch_add_listener(client->touch, &lumo_app_touch_listener,
                client);
        }
    } else if (client->touch != NULL) {
        wl_touch_release(client->touch);
        client->touch = NULL;
        client->touch_pressed = false;
        client->active_touch_id = -1;
        if (!client->pointer_pressed) {
            lumo_app_client_set_close_active(client, false);
        }
    }
}

static void lumo_app_seat_handle_name(
    void *data,
    struct wl_seat *seat,
    const char *name
) {
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener lumo_app_seat_listener = {
    .capabilities = lumo_app_seat_handle_capabilities,
    .name = lumo_app_seat_handle_name,
};

static void lumo_app_registry_add(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version
) {
    struct lumo_app_client *client = data;

    if (client == NULL) {
        return;
    }

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        client->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, version < 4 ? version : 4);
        return;
    }

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        return;
    }

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        client->seat = wl_registry_bind(registry, name, &wl_seat_interface,
            version < 5 ? version : 5);
        if (client->seat != NULL) {
            wl_seat_add_listener(client->seat, &lumo_app_seat_listener, client);
        }
        return;
    }

    if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        client->wm_base = wl_registry_bind(registry, name,
            &xdg_wm_base_interface, version < 4 ? version : 4);
        if (client->wm_base != NULL) {
            xdg_wm_base_add_listener(client->wm_base,
                &lumo_app_wm_base_listener, client);
        }
    }
}

static void lumo_app_registry_remove(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener lumo_app_registry_listener = {
    .global = lumo_app_registry_add,
    .global_remove = lumo_app_registry_remove,
};

static bool lumo_app_client_create_surface(struct lumo_app_client *client) {
    char title[96];
    char app_id[64];
    const char *app_name;

    if (client == NULL || client->compositor == NULL || client->wm_base == NULL) {
        return false;
    }

    app_name = lumo_app_id_name(client->app_id);
    client->surface = wl_compositor_create_surface(client->compositor);
    if (client->surface == NULL) {
        return false;
    }

    client->xdg_surface = xdg_wm_base_get_xdg_surface(client->wm_base,
        client->surface);
    if (client->xdg_surface == NULL) {
        return false;
    }

    client->xdg_toplevel = xdg_surface_get_toplevel(client->xdg_surface);
    if (client->xdg_toplevel == NULL) {
        return false;
    }

    xdg_surface_add_listener(client->xdg_surface,
        &lumo_app_xdg_surface_listener, client);
    xdg_toplevel_add_listener(client->xdg_toplevel,
        &lumo_app_xdg_toplevel_listener, client);

    snprintf(title, sizeof(title), "Lumo %s", lumo_app_title(client->app_id));
    snprintf(app_id, sizeof(app_id), "lumo-%s",
        app_name != NULL ? app_name : "app");
    xdg_toplevel_set_title(client->xdg_toplevel, title);
    xdg_toplevel_set_app_id(client->xdg_toplevel, app_id);
    xdg_toplevel_set_fullscreen(client->xdg_toplevel, NULL);

    wl_surface_commit(client->surface);
    return true;
}

static void lumo_app_client_destroy(struct lumo_app_client *client) {
    if (client == NULL) {
        return;
    }

    if (client->pointer != NULL) {
        wl_pointer_release(client->pointer);
        client->pointer = NULL;
    }
    if (client->touch != NULL) {
        wl_touch_release(client->touch);
        client->touch = NULL;
    }
    if (client->seat != NULL) {
        wl_seat_release(client->seat);
        client->seat = NULL;
    }
    if (client->buffer != NULL) {
        lumo_app_buffer_release(client->buffer, client->buffer->buffer);
        client->buffer = NULL;
    }
    if (client->xdg_toplevel != NULL) {
        xdg_toplevel_destroy(client->xdg_toplevel);
        client->xdg_toplevel = NULL;
    }
    if (client->xdg_surface != NULL) {
        xdg_surface_destroy(client->xdg_surface);
        client->xdg_surface = NULL;
    }
    if (client->surface != NULL) {
        wl_surface_destroy(client->surface);
        client->surface = NULL;
    }
    if (client->wm_base != NULL) {
        xdg_wm_base_destroy(client->wm_base);
        client->wm_base = NULL;
    }
    if (client->shm != NULL) {
        wl_shm_destroy(client->shm);
        client->shm = NULL;
    }
    if (client->compositor != NULL) {
        wl_compositor_destroy(client->compositor);
        client->compositor = NULL;
    }
    if (client->registry != NULL) {
        wl_registry_destroy(client->registry);
        client->registry = NULL;
    }
    if (client->display != NULL) {
        wl_display_disconnect(client->display);
        client->display = NULL;
    }
}

static void lumo_app_print_usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [--app phone|messages|browser|camera|maps|music|photos|videos|clock|notes|files|settings]\n",
        argv0);
}

int main(int argc, char **argv) {
    struct lumo_app_client client = {
        .app_id = LUMO_APP_PHONE,
        .running = true,
        .active_touch_id = -1,
        .selected_row = -1,
        .note_editing = -1,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            lumo_app_print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--app") == 0 && i + 1 < argc) {
            if (!lumo_app_id_parse(argv[++i], &client.app_id)) {
                fprintf(stderr, "lumo-app: invalid app id '%s'\n", argv[i]);
                lumo_app_print_usage(argv[0]);
                return 1;
            }
            continue;
        }

        fprintf(stderr, "lumo-app: unknown argument '%s'\n", argv[i]);
        lumo_app_print_usage(argv[0]);
        return 1;
    }

    {
        const char *home = getenv("HOME");
        if (home != NULL && strlen(home) < sizeof(client.browse_path)) {
            strncpy(client.browse_path, home, sizeof(client.browse_path) - 1);
        } else {
            strncpy(client.browse_path, "/home", sizeof(client.browse_path) - 1);
        }
        client.browse_path[sizeof(client.browse_path) - 1] = '\0';
    }

    client.display = wl_display_connect(NULL);
    if (client.display == NULL) {
        fprintf(stderr, "lumo-app: failed to connect to Wayland display\n");
        return 1;
    }

    client.registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(client.registry, &lumo_app_registry_listener,
        &client);
    wl_display_roundtrip(client.display);

    if (client.compositor == NULL || client.shm == NULL || client.wm_base == NULL) {
        fprintf(stderr, "lumo-app: missing compositor, shm, or xdg-shell global\n");
        lumo_app_client_destroy(&client);
        return 1;
    }

    if (!lumo_app_client_create_surface(&client)) {
        fprintf(stderr, "lumo-app: failed to create app surface\n");
        lumo_app_client_destroy(&client);
        return 1;
    }

    {
        int display_fd = wl_display_get_fd(client.display);
        bool needs_periodic = client.app_id == LUMO_APP_CLOCK ||
            client.app_id == LUMO_APP_SETTINGS;
        int timeout_ms = client.app_id == LUMO_APP_CLOCK ? 1000 :
            client.app_id == LUMO_APP_SETTINGS ? 5000 : -1;

        while (client.running) {
            struct pollfd pfd = {
                .fd = display_fd,
                .events = POLLIN,
            };
            int ret;

            if (wl_display_dispatch_pending(client.display) == -1) {
                break;
            }
            if (wl_display_flush(client.display) < 0 && errno != EAGAIN) {
                break;
            }

            ret = poll(&pfd, 1, timeout_ms);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }

            if (ret == 0 && needs_periodic) {
                (void)lumo_app_client_redraw(&client);
                continue;
            }

            if (pfd.revents & POLLIN) {
                if (wl_display_dispatch(client.display) == -1) {
                    break;
                }
            }
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                break;
            }
        }
    }

    lumo_app_client_destroy(&client);
    return 0;
}

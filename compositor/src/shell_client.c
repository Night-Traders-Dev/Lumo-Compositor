#include "lumo/shell.h"
#include "lumo/shell_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct lumo_shell_client;

struct lumo_shell_buffer {
    struct lumo_shell_client *client;
    struct wl_buffer *buffer;
    struct wl_shm_pool *pool;
    void *data;
    int fd;
    size_t size;
    uint32_t width;
    uint32_t height;
    struct wl_buffer_listener release;
};

enum lumo_shell_remote_scrim_state {
    LUMO_SHELL_REMOTE_SCRIM_HIDDEN = 0,
    LUMO_SHELL_REMOTE_SCRIM_DIMMED,
    LUMO_SHELL_REMOTE_SCRIM_MODAL,
};

struct lumo_shell_client {
    enum lumo_shell_mode mode;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_touch *touch;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct lumo_shell_buffer *buffer;
    struct lumo_shell_surface_config config;
    int state_fd;
    char state_socket_path[PATH_MAX];
    struct lumo_shell_protocol_stream protocol_stream;
    uint32_t configured_width;
    uint32_t configured_height;
    bool configured;
    bool running;
    bool pointer_pressed;
    bool touch_pressed;
    bool active_target_valid;
    bool pointer_position_valid;
    int32_t active_touch_id;
    uint32_t next_request_id;
    double pointer_x;
    double pointer_y;
    bool compositor_launcher_visible;
    bool compositor_keyboard_visible;
    enum lumo_shell_remote_scrim_state compositor_scrim_state;
    uint32_t compositor_rotation_degrees;
    double compositor_gesture_threshold;
    uint32_t compositor_gesture_timeout_ms;
    bool compositor_keyboard_resize_pending;
    bool compositor_keyboard_resize_acked;
    uint32_t compositor_keyboard_resize_serial;
    struct lumo_shell_target active_target;
};

static uint32_t lumo_rgba(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void lumo_fill_rect(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int x,
    int y,
    int rect_width,
    int rect_height,
    uint32_t color
) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + rect_width;
    int y1 = y + rect_height;

    if (pixels == NULL || width == 0 || height == 0) {
        return;
    }
    if (x0 >= (int)width || y0 >= (int)height) {
        return;
    }
    if (x1 > (int)width) {
        x1 = (int)width;
    }
    if (y1 > (int)height) {
        y1 = (int)height;
    }

    for (int row = y0; row < y1; row++) {
        for (int col = x0; col < x1; col++) {
            pixels[row * (int)width + col] = color;
        }
    }
}

static void lumo_fill_background(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t color
) {
    if (pixels == NULL) {
        return;
    }

    for (uint32_t row = 0; row < height; row++) {
        for (uint32_t col = 0; col < width; col++) {
            pixels[row * width + col] = color;
        }
    }
}

static void lumo_draw_outline(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    uint32_t thickness,
    uint32_t color
) {
    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return;
    }

    lumo_fill_rect(pixels, width, height, rect->x, rect->y, rect->width,
        (int)thickness, color);
    lumo_fill_rect(pixels, width, height, rect->x,
        rect->y + rect->height - (int)thickness, rect->width, (int)thickness,
        color);
    lumo_fill_rect(pixels, width, height, rect->x, rect->y,
        (int)thickness, rect->height, color);
    lumo_fill_rect(pixels, width, height,
        rect->x + rect->width - (int)thickness, rect->y, (int)thickness,
        rect->height, color);
}

static void lumo_draw_launcher(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target
) {
    const uint32_t bg = client != NULL && client->compositor_launcher_visible
        ? lumo_rgba(0x17, 0x20, 0x33)
        : lumo_rgba(0x0D, 0x12, 0x1D);
    const uint32_t header = client != NULL && client->compositor_launcher_visible
        ? lumo_rgba(0x10, 0x18, 0x27)
        : lumo_rgba(0x08, 0x0C, 0x14);
    const uint32_t highlight = lumo_rgba(0xEE, 0xF2, 0xFF);
    const uint32_t tile_colors[] = {
        lumo_rgba(0x4F, 0x46, 0xE5),
        lumo_rgba(0x14, 0xB8, 0xA6),
        lumo_rgba(0xF4, 0xA7, 0x39),
        lumo_rgba(0xEF, 0x44, 0x44),
    };
    const size_t tile_count = lumo_shell_launcher_tile_count();

    lumo_fill_background(pixels, width, height, bg);
    lumo_fill_rect(pixels, width, height, 0, 0, (int)width, (int)(height / 5),
        header);

    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        struct lumo_rect tile_rect;
        uint32_t row = tile_index / 3;
        uint32_t col = tile_index % 3;
        uint32_t tile_color = tile_colors[(row + col) %
            (sizeof(tile_colors) / sizeof(tile_colors[0]))];
        bool active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_LAUNCHER_TILE &&
            active_target->index == tile_index;

        if (!lumo_shell_launcher_tile_rect(width, height, tile_index,
                &tile_rect)) {
            continue;
        }

        lumo_fill_rect(pixels, width, height, tile_rect.x, tile_rect.y,
            tile_rect.width, tile_rect.height, tile_color);
        if (active) {
            lumo_draw_outline(pixels, width, height, &tile_rect, 4, highlight);
        }
    }
}

static void lumo_draw_osk(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target
) {
    const uint32_t bg = client != NULL && client->compositor_keyboard_visible
        ? lumo_rgba(0x22, 0x22, 0x22)
        : lumo_rgba(0x14, 0x14, 0x14);
    const uint32_t key = client != NULL && client->compositor_keyboard_visible
        ? lumo_rgba(0x46, 0x46, 0x46)
        : lumo_rgba(0x2E, 0x2E, 0x2E);
    const uint32_t key_light = client != NULL && client->compositor_keyboard_visible
        ? lumo_rgba(0x62, 0x62, 0x62)
        : lumo_rgba(0x44, 0x44, 0x44);
    const uint32_t spacer = lumo_rgba(0x2D, 0x2D, 0x2D);
    const uint32_t row_columns[] = {10, 10, 6};
    const size_t key_count = lumo_shell_osk_key_count();
    const uint32_t highlight = lumo_rgba(0xEE, 0xF2, 0xFF);

    lumo_fill_background(pixels, width, height, bg);
    lumo_fill_rect(pixels, width, height, 0, 0, (int)width, 42, spacer);

    for (uint32_t key_index = 0, row = 0, col = 0; key_index < key_count;
            key_index++) {
        struct lumo_rect key_rect;
        uint32_t color = (row == 0) ? key_light : key;
        bool active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_OSK_KEY &&
            active_target->index == key_index;

        if (!lumo_shell_osk_key_rect(width, height, key_index, &key_rect)) {
            continue;
        }

        if (row == 2 && col == 2) {
            color = key_light;
        }

        lumo_fill_rect(pixels, width, height, key_rect.x, key_rect.y,
            key_rect.width, key_rect.height, color);
        if (active) {
            lumo_draw_outline(pixels, width, height, &key_rect, 4, highlight);
        }

        col++;
        if (col >= row_columns[row]) {
            row++;
            col = 0;
        }
    }
}

static void lumo_draw_gesture(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target
) {
    const uint32_t bg = client != NULL &&
            client->compositor_scrim_state == LUMO_SHELL_REMOTE_SCRIM_MODAL
        ? lumo_rgba(0x0B, 0x0E, 0x12)
        : client != NULL &&
                client->compositor_scrim_state ==
                    LUMO_SHELL_REMOTE_SCRIM_DIMMED
            ? lumo_rgba(0x0D, 0x10, 0x16)
            : lumo_rgba(0x0F, 0x11, 0x15);
    const uint32_t accent = client != NULL && client->compositor_gesture_threshold > 40.0
        ? lumo_rgba(0x98, 0xE4, 0xFF)
        : lumo_rgba(0x7C, 0xD3, 0xFF);
    const uint32_t highlight = lumo_rgba(0xEE, 0xF2, 0xFF);
    struct lumo_rect handle_rect = {0};
    uint32_t handle_thickness = client != NULL
        ? (uint32_t)(client->compositor_gesture_threshold / 12.0)
        : 4;

    if (!lumo_shell_gesture_handle_rect(width, height, &handle_rect)) {
        return;
    }

    if (handle_thickness < 4) {
        handle_thickness = 4;
    }
    if (handle_thickness > 10) {
        handle_thickness = 10;
    }

    lumo_fill_background(pixels, width, height, bg);
    lumo_fill_rect(pixels, width, height, 0,
        (int)(height / 2 - (int)(handle_thickness / 2)),
        (int)width, (int)handle_thickness, accent);
    lumo_fill_rect(pixels, width, height, handle_rect.x, handle_rect.y,
        handle_rect.width, handle_rect.height, accent);

    if (active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_GESTURE_HANDLE) {
        lumo_draw_outline(pixels, width, height, &handle_rect,
            handle_thickness, highlight);
    }
}

static void lumo_render_placeholder(
    struct lumo_shell_client *client,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_target *active_target
) {
    if (client == NULL || pixels == NULL) {
        return;
    }

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        lumo_draw_launcher(pixels, width, height, client, active_target);
        return;
    case LUMO_SHELL_MODE_OSK:
        lumo_draw_osk(pixels, width, height, client, active_target);
        return;
    case LUMO_SHELL_MODE_GESTURE:
        lumo_draw_gesture(pixels, width, height, client, active_target);
        return;
    default:
        break;
    }
}

static int lumo_create_shm_file(size_t size) {
    char template[] = "/tmp/lumo-shell-XXXXXX";
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

static void lumo_shell_buffer_release(
    void *data,
    struct wl_buffer *wl_buffer
) {
    struct lumo_shell_buffer *buffer = data;
    struct lumo_shell_client *client = buffer != NULL ? buffer->client : NULL;

    (void)wl_buffer;
    if (client != NULL && client->buffer == buffer) {
        client->buffer = NULL;
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

static bool lumo_shell_draw_buffer(
    struct lumo_shell_client *client,
    uint32_t width,
    uint32_t height
) {
    struct lumo_shell_buffer *buffer;
    size_t stride;
    size_t size;
    int fd;
    const struct lumo_shell_target *active_target;

    if (client == NULL || client->shm == NULL || client->surface == NULL ||
            width == 0 || height == 0) {
        return false;
    }

    stride = width * 4u;
    size = stride * height;
    fd = lumo_create_shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "lumo-shell: failed to create shm file: %s\n",
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
    buffer->width = width;
    buffer->height = height;
    buffer->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->data == MAP_FAILED) {
        fprintf(stderr, "lumo-shell: mmap failed: %s\n", strerror(errno));
        close(fd);
        free(buffer);
        return false;
    }

    buffer->pool = wl_shm_create_pool(client->shm, fd, (int)size);
    if (buffer->pool == NULL) {
        fprintf(stderr, "lumo-shell: failed to create shm pool\n");
        munmap(buffer->data, size);
        close(fd);
        free(buffer);
        return false;
    }

    buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, 0, (int)width,
        (int)height, (int)stride, WL_SHM_FORMAT_XRGB8888);
    if (buffer->buffer == NULL) {
        fprintf(stderr, "lumo-shell: failed to create shm buffer\n");
        wl_shm_pool_destroy(buffer->pool);
        munmap(buffer->data, size);
        close(fd);
        free(buffer);
        return false;
    }

    buffer->release.release = lumo_shell_buffer_release;
    wl_buffer_add_listener(buffer->buffer, &buffer->release, buffer);

    active_target = client->active_target_valid ? &client->active_target : NULL;
    lumo_render_placeholder(client, buffer->data, width, height, active_target);
    wl_surface_attach(client->surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(client->surface, 0, 0, (int)width, (int)height);
    wl_surface_commit(client->surface);

    client->buffer = buffer;
    client->configured_width = width;
    client->configured_height = height;
    client->configured = true;
    return true;
}

static bool lumo_shell_client_redraw(struct lumo_shell_client *client) {
    if (client == NULL || !client->configured) {
        return false;
    }

    return lumo_shell_draw_buffer(client, client->configured_width,
        client->configured_height);
}

static void lumo_shell_client_set_active_target(
    struct lumo_shell_client *client,
    const struct lumo_shell_target *target
) {
    if (client == NULL || target == NULL) {
        return;
    }

    client->active_target = *target;
    client->active_target_valid = true;
    (void)lumo_shell_client_redraw(client);
}

static void lumo_shell_client_clear_active_target(
    struct lumo_shell_client *client
) {
    if (client == NULL) {
        return;
    }

    client->active_target_valid = false;
    memset(&client->active_target, 0, sizeof(client->active_target));
    (void)lumo_shell_client_redraw(client);
}

static void lumo_shell_client_note_target(
    struct lumo_shell_client *client,
    double x,
    double y
) {
    struct lumo_shell_target target = {0};

    if (client == NULL || client->configured_width == 0 ||
            client->configured_height == 0) {
        return;
    }

    if (lumo_shell_target_for_mode(client->mode, client->configured_width,
            client->configured_height, x, y, &target)) {
        lumo_shell_client_set_active_target(client, &target);
        return;
    }

    lumo_shell_client_clear_active_target(client);
}

static bool lumo_shell_client_send_frame(
    struct lumo_shell_client *client,
    const struct lumo_shell_protocol_frame *frame
);

static void lumo_shell_client_activate_target(struct lumo_shell_client *client) {
    struct lumo_shell_protocol_frame frame;
    const char *kind_name;

    if (client == NULL || !client->active_target_valid) {
        return;
    }

    kind_name = lumo_shell_target_kind_name(client->active_target.kind);
    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "activate_target",
            client->next_request_id++)) {
        fprintf(stderr, "lumo-shell: failed to build activate request\n");
        return;
    }

    if (!lumo_shell_protocol_frame_add_string(&frame, "kind", kind_name) ||
            !lumo_shell_protocol_frame_add_u32(&frame, "index",
                client->active_target.index) ||
            !lumo_shell_protocol_frame_add_string(&frame, "mode",
                lumo_shell_mode_name(client->mode)) ||
            !lumo_shell_client_send_frame(client, &frame)) {
        fprintf(stderr, "lumo-shell: failed to send activate request\n");
        return;
    }

    fprintf(stderr,
        "lumo-shell: %s request activate %s %u\n",
        lumo_shell_mode_name(client->mode),
        kind_name,
        client->active_target.index);
}

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

static bool lumo_shell_client_send_frame(
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

static void lumo_shell_client_apply_state_frame(
    struct lumo_shell_client *client,
    const struct lumo_shell_protocol_frame *frame
) {
    bool bool_value;
    uint32_t rotation_value;
    double double_value;
    uint32_t timeout_value;
    bool changed = false;
    const char *value;

    if (client == NULL || frame == NULL) {
        return;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "launcher_visible",
            &bool_value)) {
        client->compositor_launcher_visible = bool_value;
        fprintf(stderr, "lumo-shell: launcher visible=%s\n",
            bool_value ? "true" : "false");
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "keyboard_visible",
            &bool_value)) {
        client->compositor_keyboard_visible = bool_value;
        fprintf(stderr, "lumo-shell: keyboard visible=%s\n",
            bool_value ? "true" : "false");
        changed = true;
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

    if (lumo_shell_protocol_frame_get(frame, "status", &value) &&
            strcmp(value, "ok") == 0) {
        fprintf(stderr, "lumo-shell: compositor response ok for %s\n",
            frame->name);
    }

    if (lumo_shell_protocol_frame_get(frame, "code", &value)) {
        fprintf(stderr, "lumo-shell: compositor response %s code=%s\n",
            frame->name, value);
    }

    if (changed) {
        (void)lumo_shell_client_redraw(client);
    }
}

static int lumo_shell_client_connect_state_socket(
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

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(fd);
        return -1;
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

static bool lumo_shell_client_pump_protocol(struct lumo_shell_client *client) {
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

static int lumo_shell_client_run(struct lumo_shell_client *client) {
    int display_fd;

    if (client == NULL || client->display == NULL) {
        return -1;
    }

    display_fd = wl_display_get_fd(client->display);
    while (client->display != NULL) {
        struct pollfd fds[2];
        nfds_t nfds = 0;
        int poll_result;

        if (wl_display_dispatch_pending(client->display) == -1) {
            return -1;
        }
        if (wl_display_flush(client->display) < 0 && errno != EAGAIN) {
            return -1;
        }

        fds[nfds].fd = display_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        if (client->state_fd >= 0) {
            fds[nfds].fd = client->state_fd;
            fds[nfds].events = POLLIN | POLLHUP | POLLERR;
            nfds++;
        }

        poll_result = poll(fds, nfds, -1);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (client->state_fd >= 0 && nfds > 1 &&
                (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (!lumo_shell_client_pump_protocol(client)) {
                close(client->state_fd);
                client->state_fd = -1;
            }
        }

        if (fds[0].revents & POLLIN) {
            if (wl_display_dispatch(client->display) == -1) {
                break;
            }
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }

        if (client->state_fd < 0) {
            continue;
        }
    }

    return 0;
}

static void lumo_shell_pointer_handle_enter(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial,
    struct wl_surface *surface,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y
) {
    struct lumo_shell_client *client = data;

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

static void lumo_shell_pointer_handle_leave(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial,
    struct wl_surface *surface
) {
    struct lumo_shell_client *client = data;

    (void)wl_pointer;
    (void)serial;
    (void)surface;
    if (client == NULL) {
        return;
    }

    client->pointer_position_valid = false;
    if (!client->pointer_pressed && !client->touch_pressed) {
        lumo_shell_client_clear_active_target(client);
    }
}

static void lumo_shell_pointer_handle_motion(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t time,
    wl_fixed_t surface_x,
    wl_fixed_t surface_y
) {
    struct lumo_shell_client *client = data;

    (void)wl_pointer;
    (void)time;
    if (client == NULL) {
        return;
    }

    client->pointer_x = wl_fixed_to_double(surface_x);
    client->pointer_y = wl_fixed_to_double(surface_y);
    client->pointer_position_valid = true;

    if (client->pointer_pressed) {
        lumo_shell_client_note_target(client, client->pointer_x,
            client->pointer_y);
    }
}

static void lumo_shell_pointer_handle_button(
    void *data,
    struct wl_pointer *wl_pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    uint32_t state
) {
    struct lumo_shell_client *client = data;

    (void)wl_pointer;
    (void)serial;
    (void)time;
    (void)button;
    if (client == NULL || !client->pointer_position_valid) {
        return;
    }

    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
        client->pointer_pressed = true;
        lumo_shell_client_note_target(client, client->pointer_x,
            client->pointer_y);
        return;
    }

    if (client->pointer_pressed) {
        client->pointer_pressed = false;
        lumo_shell_client_activate_target(client);
        if (!client->touch_pressed) {
            lumo_shell_client_clear_active_target(client);
        }
    }
}

static void lumo_shell_pointer_handle_frame(
    void *data,
    struct wl_pointer *wl_pointer
) {
    (void)data;
    (void)wl_pointer;
}

static void lumo_shell_pointer_handle_axis(
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

static const struct wl_pointer_listener lumo_shell_pointer_listener = {
    .enter = lumo_shell_pointer_handle_enter,
    .leave = lumo_shell_pointer_handle_leave,
    .motion = lumo_shell_pointer_handle_motion,
    .button = lumo_shell_pointer_handle_button,
    .axis = lumo_shell_pointer_handle_axis,
    .frame = lumo_shell_pointer_handle_frame,
};

static void lumo_shell_touch_handle_down(
    void *data,
    struct wl_touch *wl_touch,
    uint32_t serial,
    uint32_t time,
    struct wl_surface *surface,
    int32_t id,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct lumo_shell_client *client = data;

    (void)wl_touch;
    (void)serial;
    (void)time;
    (void)surface;
    if (client == NULL) {
        return;
    }

    client->touch_pressed = true;
    client->active_touch_id = id;
    lumo_shell_client_note_target(client, wl_fixed_to_double(x),
        wl_fixed_to_double(y));
}

static void lumo_shell_touch_handle_up(
    void *data,
    struct wl_touch *wl_touch,
    uint32_t serial,
    uint32_t time,
    int32_t id
) {
    struct lumo_shell_client *client = data;

    (void)wl_touch;
    (void)serial;
    (void)time;
    if (client == NULL || !client->touch_pressed ||
            client->active_touch_id != id) {
        return;
    }

    client->touch_pressed = false;
    client->active_touch_id = -1;
    lumo_shell_client_activate_target(client);
    if (!client->pointer_pressed) {
        lumo_shell_client_clear_active_target(client);
    }
}

static void lumo_shell_touch_handle_motion(
    void *data,
    struct wl_touch *wl_touch,
    uint32_t time,
    int32_t id,
    wl_fixed_t x,
    wl_fixed_t y
) {
    struct lumo_shell_client *client = data;

    (void)wl_touch;
    (void)time;
    if (client == NULL || !client->touch_pressed ||
            client->active_touch_id != id) {
        return;
    }

    lumo_shell_client_note_target(client, wl_fixed_to_double(x),
        wl_fixed_to_double(y));
}

static void lumo_shell_touch_handle_frame(
    void *data,
    struct wl_touch *wl_touch
) {
    (void)data;
    (void)wl_touch;
}

static void lumo_shell_touch_handle_cancel(
    void *data,
    struct wl_touch *wl_touch
) {
    struct lumo_shell_client *client = data;

    (void)wl_touch;
    if (client == NULL) {
        return;
    }

    client->touch_pressed = false;
    client->active_touch_id = -1;
    if (!client->pointer_pressed) {
        lumo_shell_client_clear_active_target(client);
    }
}

static void lumo_shell_touch_handle_shape(
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

static void lumo_shell_touch_handle_orientation(
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

static const struct wl_touch_listener lumo_shell_touch_listener = {
    .down = lumo_shell_touch_handle_down,
    .up = lumo_shell_touch_handle_up,
    .motion = lumo_shell_touch_handle_motion,
    .frame = lumo_shell_touch_handle_frame,
    .cancel = lumo_shell_touch_handle_cancel,
    .shape = lumo_shell_touch_handle_shape,
    .orientation = lumo_shell_touch_handle_orientation,
};

static void lumo_shell_seat_handle_capabilities(
    void *data,
    struct wl_seat *seat,
    uint32_t capabilities
) {
    struct lumo_shell_client *client = data;

    if (client == NULL) {
        return;
    }

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
        if (!client->touch_pressed) {
            lumo_shell_client_clear_active_target(client);
        }
    }

    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) != 0) {
        if (client->touch == NULL) {
            client->touch = wl_seat_get_touch(seat);
            wl_touch_add_listener(client->touch, &lumo_shell_touch_listener,
                client);
        }
    } else if (client->touch != NULL) {
        wl_touch_release(client->touch);
        client->touch = NULL;
        client->touch_pressed = false;
        client->active_touch_id = -1;
        if (!client->pointer_pressed) {
            lumo_shell_client_clear_active_target(client);
        }
    }
}

static void lumo_shell_seat_handle_name(
    void *data,
    struct wl_seat *seat,
    const char *name
) {
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener lumo_shell_seat_listener = {
    .capabilities = lumo_shell_seat_handle_capabilities,
    .name = lumo_shell_seat_handle_name,
};

static void lumo_shell_handle_configure(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface,
    uint32_t serial,
    uint32_t width,
    uint32_t height
) {
    struct lumo_shell_client *client = data;
    uint32_t draw_width;
    uint32_t draw_height;

    (void)layer_surface;
    if (client == NULL) {
        return;
    }

    zwlr_layer_surface_v1_ack_configure(client->layer_surface, serial);
    draw_width = width != 0 ? width : client->config.width;
    draw_height = height != 0 ? height : client->config.height;

    if (draw_width == 0 || draw_height == 0) {
        return;
    }

    if (!lumo_shell_draw_buffer(client, draw_width, draw_height)) {
        fprintf(stderr, "lumo-shell: failed to render %s surface\n",
            client->config.name != NULL ? client->config.name : "shell");
    }
}

static void lumo_shell_handle_closed(
    void *data,
    struct zwlr_layer_surface_v1 *layer_surface
) {
    struct lumo_shell_client *client = data;

    (void)layer_surface;
    if (client != NULL && client->display != NULL) {
        wl_display_disconnect(client->display);
        client->display = NULL;
    }
}

static const struct zwlr_layer_surface_v1_listener lumo_shell_surface_listener = {
    .configure = lumo_shell_handle_configure,
    .closed = lumo_shell_handle_closed,
};

static void lumo_shell_registry_add(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version
) {
    struct lumo_shell_client *client = data;

    if (client == NULL) {
        return;
    }

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        client->compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, version < 4 ? version : 4);
        return;
    }

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        client->shm = wl_registry_bind(registry, name,
            &wl_shm_interface, 1);
        return;
    }

    if (strcmp(interface, wl_seat_interface.name) == 0) {
        client->seat = wl_registry_bind(registry, name,
            &wl_seat_interface, version < 5 ? version : 5);
        if (client->seat != NULL) {
            wl_seat_add_listener(client->seat, &lumo_shell_seat_listener,
                client);
        }
        return;
    }

    if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
        client->layer_shell = wl_registry_bind(registry, name,
            &zwlr_layer_shell_v1_interface, 1);
        return;
    }
}

static void lumo_shell_registry_remove(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener lumo_shell_registry_listener = {
    .global = lumo_shell_registry_add,
    .global_remove = lumo_shell_registry_remove,
};

static void lumo_shell_print_usage(const char *argv0) {
    fprintf(stderr, "usage: %s [--mode launcher|osk|gesture] [--debug]\n",
        argv0);
}

static bool lumo_shell_parse_mode(
    const char *value,
    enum lumo_shell_mode *mode
) {
    if (value == NULL || mode == NULL) {
        return false;
    }

    if (strcmp(value, "launcher") == 0) {
        *mode = LUMO_SHELL_MODE_LAUNCHER;
        return true;
    }
    if (strcmp(value, "osk") == 0) {
        *mode = LUMO_SHELL_MODE_OSK;
        return true;
    }
    if (strcmp(value, "gesture") == 0) {
        *mode = LUMO_SHELL_MODE_GESTURE;
        return true;
    }

    return false;
}

static bool lumo_shell_create_surface(struct lumo_shell_client *client) {
    uint32_t keyboard_interactive;

    if (client == NULL || client->compositor == NULL || client->layer_shell == NULL) {
        return false;
    }

    if (!lumo_shell_surface_config_for_mode(client->mode, 1280, 720,
            &client->config)) {
        return false;
    }

    client->surface = wl_compositor_create_surface(client->compositor);
    if (client->surface == NULL) {
        return false;
    }

    client->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        client->layer_shell,
        client->surface,
        NULL,
        client->mode == LUMO_SHELL_MODE_LAUNCHER
            ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
            : ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        client->config.name != NULL ? client->config.name : "lumo-shell"
    );
    if (client->layer_surface == NULL) {
        return false;
    }

    zwlr_layer_surface_v1_add_listener(client->layer_surface,
        &lumo_shell_surface_listener, client);
    zwlr_layer_surface_v1_set_size(client->layer_surface,
        client->config.width, client->config.height);
    zwlr_layer_surface_v1_set_anchor(client->layer_surface,
        client->config.anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(client->layer_surface,
        client->config.exclusive_zone);
    zwlr_layer_surface_v1_set_margin(client->layer_surface,
        client->config.margin_top,
        client->config.margin_right,
        client->config.margin_bottom,
        client->config.margin_left);

    keyboard_interactive = client->config.keyboard_interactive
        ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
        : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    zwlr_layer_surface_v1_set_keyboard_interactivity(client->layer_surface,
        keyboard_interactive);

    wl_surface_commit(client->surface);
    return true;
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
        .compositor_gesture_timeout_ms = 180,
        .compositor_keyboard_resize_pending = false,
        .compositor_keyboard_resize_acked = true,
        .compositor_keyboard_resize_serial = 0,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            lumo_shell_print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (!lumo_shell_parse_mode(argv[++i], &client.mode)) {
                fprintf(stderr, "lumo-shell: invalid mode '%s'\n", argv[i]);
                lumo_shell_print_usage(argv[0]);
                return 1;
            }
            continue;
        }

        fprintf(stderr, "lumo-shell: unknown argument '%s'\n", argv[i]);
        lumo_shell_print_usage(argv[0]);
        return 1;
    }

    client.display = wl_display_connect(NULL);
    if (client.display == NULL) {
        fprintf(stderr, "lumo-shell: failed to connect to Wayland display\n");
        return 1;
    }

    lumo_shell_protocol_stream_init(&client.protocol_stream);

    client.registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(client.registry, &lumo_shell_registry_listener,
        &client);
    wl_display_roundtrip(client.display);

    if (client.compositor == NULL || client.shm == NULL ||
            client.layer_shell == NULL) {
        fprintf(stderr, "lumo-shell: missing compositor, shm, or layer-shell global\n");
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

    if (client.display != NULL) {
        wl_display_disconnect(client.display);
    }

    return 0;
}

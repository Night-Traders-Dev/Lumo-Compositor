#include "lumo/shell.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
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

struct lumo_shell_client {
    enum lumo_shell_mode mode;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct lumo_shell_buffer *buffer;
    struct lumo_shell_surface_config config;
    uint32_t configured_width;
    uint32_t configured_height;
    bool configured;
    bool running;
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

static void lumo_draw_launcher(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    const uint32_t bg = lumo_rgba(0x17, 0x20, 0x33);
    const uint32_t header = lumo_rgba(0x10, 0x18, 0x27);
    const uint32_t tile_colors[] = {
        lumo_rgba(0x4F, 0x46, 0xE5),
        lumo_rgba(0x14, 0xB8, 0xA6),
        lumo_rgba(0xF4, 0xA7, 0x39),
        lumo_rgba(0xEF, 0x44, 0x44),
    };
    uint32_t header_height = height / 5;
    uint32_t gap = 18;
    uint32_t cols = 3;
    uint32_t rows = 4;
    uint32_t grid_top = header_height + 24;
    uint32_t tile_width = (width > gap * (cols + 1))
        ? (width - gap * (cols + 1)) / cols
        : width / cols;
    uint32_t tile_height = (height > grid_top + gap * (rows + 1))
        ? (height - grid_top - gap * (rows + 1)) / rows
        : height / rows;

    if (tile_width < 64) {
        tile_width = 64;
    }
    if (tile_height < 64) {
        tile_height = 64;
    }

    lumo_fill_background(pixels, width, height, bg);
    lumo_fill_rect(pixels, width, height, 0, 0, (int)width, (int)header_height,
        header);

    for (uint32_t row = 0; row < rows; row++) {
        for (uint32_t col = 0; col < cols; col++) {
            uint32_t tile_x = gap + col * (tile_width + gap);
            uint32_t tile_y = grid_top + row * (tile_height + gap);
            uint32_t tile_color = tile_colors[(row + col) % (sizeof(tile_colors) / sizeof(tile_colors[0]))];
            lumo_fill_rect(pixels, width, height, (int)tile_x, (int)tile_y,
                (int)tile_width, (int)tile_height, tile_color);
        }
    }
}

static void lumo_draw_osk(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    const uint32_t bg = lumo_rgba(0x22, 0x22, 0x22);
    const uint32_t key = lumo_rgba(0x46, 0x46, 0x46);
    const uint32_t key_light = lumo_rgba(0x62, 0x62, 0x62);
    const uint32_t spacer = lumo_rgba(0x2D, 0x2D, 0x2D);
    uint32_t top_bar = 42;
    uint32_t gap = 8;
    uint32_t row_height = (height > top_bar + gap * 4)
        ? (height - top_bar - gap * 4) / 3
        : height / 3;

    if (row_height < 40) {
        row_height = 40;
    }

    lumo_fill_background(pixels, width, height, bg);
    lumo_fill_rect(pixels, width, height, 0, 0, (int)width, (int)top_bar, spacer);

    for (uint32_t row = 0; row < 3; row++) {
        uint32_t cols = row == 2 ? 6 : 10;
        uint32_t key_width = (width > gap * (cols + 1))
            ? (width - gap * (cols + 1)) / cols
            : width / cols;
        if (key_width < 28) {
            key_width = 28;
        }

        for (uint32_t col = 0; col < cols; col++) {
            uint32_t key_x = gap + col * (key_width + gap);
            uint32_t key_y = top_bar + gap + row * (row_height + gap);
            uint32_t draw_width = key_width;
            uint32_t color = (row == 0) ? key_light : key;

            if (row == 2 && col == 2) {
                draw_width = key_width * 3 + gap * 2;
                color = key_light;
            } else if (row == 2 && col > 2) {
                key_x += key_width * 2 + gap * 2;
            }

            lumo_fill_rect(pixels, width, height, (int)key_x, (int)key_y,
                (int)draw_width, (int)row_height, color);
        }
    }
}

static void lumo_draw_gesture(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    const uint32_t bg = lumo_rgba(0x0F, 0x11, 0x15);
    const uint32_t accent = lumo_rgba(0x7C, 0xD3, 0xFF);
    uint32_t handle_width = width / 3;
    uint32_t handle_height = height / 3;
    uint32_t handle_x = (width - handle_width) / 2;
    uint32_t handle_y = (height - handle_height) / 2;

    if (handle_width < 96) {
        handle_width = 96;
        handle_x = (width > handle_width) ? (width - handle_width) / 2 : 0;
    }
    if (handle_height < 8) {
        handle_height = 8;
        handle_y = (height > handle_height) ? (height - handle_height) / 2 : 0;
    }

    lumo_fill_background(pixels, width, height, bg);
    lumo_fill_rect(pixels, width, height, 0, (int)(height / 2 - 2), (int)width,
        4, accent);
    lumo_fill_rect(pixels, width, height, (int)handle_x, (int)handle_y,
        (int)handle_width, (int)handle_height, accent);
}

static void lumo_render_placeholder(
    struct lumo_shell_client *client,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    if (client == NULL || pixels == NULL) {
        return;
    }

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        lumo_draw_launcher(pixels, width, height);
        return;
    case LUMO_SHELL_MODE_OSK:
        lumo_draw_osk(pixels, width, height);
        return;
    case LUMO_SHELL_MODE_GESTURE:
        lumo_draw_gesture(pixels, width, height);
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

    lumo_render_placeholder(client, buffer->data, width, height);
    wl_surface_attach(client->surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(client->surface, 0, 0, (int)width, (int)height);
    wl_surface_commit(client->surface);

    client->buffer = buffer;
    client->configured_width = width;
    client->configured_height = height;
    client->configured = true;
    return true;
}

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

    while (wl_display_dispatch(client.display) != -1) {
    }

    if (client.display != NULL) {
        wl_display_disconnect(client.display);
    }

    return 0;
}

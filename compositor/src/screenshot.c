#include "lumo/screenshot.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <png.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

struct lumo_screenshot_buffer {
    struct wl_buffer *buffer;
    struct wl_shm_pool *pool;
    void *data;
    int fd;
    size_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
};

struct lumo_screenshot_client {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_shm *shm;
    struct wl_output *output;
    struct zwlr_screencopy_manager_v1 *manager;
    struct zwlr_screencopy_frame_v1 *frame;
    struct lumo_screenshot_buffer capture;
    const char *socket_name;
    const char *output_path;
    bool have_buffer_info;
    bool copy_requested;
    bool ready;
    bool failed;
    bool y_invert;
};

static void lumo_screenshot_usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [--socket NAME] [--output FILE.png]\n",
        argv0);
}

static int lumo_screenshot_create_shm_file(size_t size) {
    char template[] = "/tmp/lumo-shot-XXXXXX";
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

static void lumo_screenshot_buffer_destroy(struct lumo_screenshot_buffer *buffer) {
    if (buffer == NULL) {
        return;
    }

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
    memset(buffer, 0, sizeof(*buffer));
    buffer->fd = -1;
}

static bool lumo_screenshot_buffer_create(
    struct lumo_screenshot_client *client
) {
    struct lumo_screenshot_buffer *buffer = NULL;
    size_t size;

    if (client == NULL || client->shm == NULL || !client->have_buffer_info) {
        return false;
    }

    buffer = &client->capture;
    size = (size_t)buffer->stride * (size_t)buffer->height;
    buffer->size = size;
    buffer->fd = lumo_screenshot_create_shm_file(size);
    if (buffer->fd < 0) {
        fprintf(stderr, "lumo-screenshot: failed to create shm file: %s\n",
            strerror(errno));
        return false;
    }

    buffer->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
        buffer->fd, 0);
    if (buffer->data == MAP_FAILED) {
        fprintf(stderr, "lumo-screenshot: mmap failed: %s\n", strerror(errno));
        buffer->data = NULL;
        lumo_screenshot_buffer_destroy(buffer);
        return false;
    }

    buffer->pool = wl_shm_create_pool(client->shm, buffer->fd, (int)size);
    if (buffer->pool == NULL) {
        fprintf(stderr, "lumo-screenshot: failed to create shm pool\n");
        lumo_screenshot_buffer_destroy(buffer);
        return false;
    }

    buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, 0,
        (int)buffer->width, (int)buffer->height, (int)buffer->stride,
        (int)buffer->format);
    if (buffer->buffer == NULL) {
        fprintf(stderr, "lumo-screenshot: failed to create wl_buffer\n");
        lumo_screenshot_buffer_destroy(buffer);
        return false;
    }

    return true;
}

static bool lumo_screenshot_write_png(const struct lumo_screenshot_client *client) {
    FILE *file = NULL;
    png_structp png = NULL;
    png_infop info = NULL;
    uint8_t *row = NULL;
    uint32_t *pixels = NULL;
    bool success = false;

    if (client == NULL || client->output_path == NULL ||
            client->capture.data == NULL || client->capture.width == 0 ||
            client->capture.height == 0) {
        return false;
    }

    file = fopen(client->output_path, "wb");
    if (file == NULL) {
        fprintf(stderr, "lumo-screenshot: failed to open %s: %s\n",
            client->output_path, strerror(errno));
        return false;
    }

    png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png == NULL) {
        goto done;
    }

    info = png_create_info_struct(png);
    if (info == NULL) {
        goto done;
    }

    if (setjmp(png_jmpbuf(png)) != 0) {
        goto done;
    }

    png_init_io(png, file);
    png_set_IHDR(png, info, client->capture.width, client->capture.height, 8,
        PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    row = calloc(1, (size_t)client->capture.width * 3u);
    if (row == NULL) {
        goto done;
    }

    pixels = client->capture.data;
    for (uint32_t row_index = 0; row_index < client->capture.height; row_index++) {
        uint32_t source_row = lumo_screenshot_source_row(row_index,
            client->capture.height, client->y_invert);
        const uint32_t *src = (const uint32_t *)((const uint8_t *)pixels +
            (size_t)source_row * client->capture.stride);

        lumo_screenshot_convert_shm_row(row,
            (size_t)client->capture.width * 3u, src, client->capture.width,
            client->capture.format);
        png_write_row(png, row);
    }

    png_write_end(png, info);
    success = true;

done:
    free(row);
    if (png != NULL && info != NULL) {
        png_destroy_write_struct(&png, &info);
    } else if (png != NULL) {
        png_destroy_write_struct(&png, NULL);
    }
    if (file != NULL) {
        fclose(file);
    }
    return success;
}

static void lumo_screenshot_frame_buffer(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    struct lumo_screenshot_client *client = data;

    (void)frame;
    if (client == NULL || client->have_buffer_info) {
        return;
    }

    if (!lumo_screenshot_format_supported(format)) {
        fprintf(stderr,
            "lumo-screenshot: unsupported wl_shm format 0x%08x\n",
            format);
        return;
    }

    client->capture.format = format;
    client->capture.width = width;
    client->capture.height = height;
    client->capture.stride = stride;
    client->have_buffer_info = true;
}

static void lumo_screenshot_frame_flags(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t flags
) {
    struct lumo_screenshot_client *client = data;

    (void)frame;
    if (client == NULL) {
        return;
    }

    client->y_invert =
        (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

static void lumo_screenshot_frame_ready(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t tv_sec_hi,
    uint32_t tv_sec_lo,
    uint32_t tv_nsec
) {
    struct lumo_screenshot_client *client = data;

    (void)frame;
    (void)tv_sec_hi;
    (void)tv_sec_lo;
    (void)tv_nsec;
    if (client != NULL) {
        client->ready = true;
    }
}

static void lumo_screenshot_frame_failed(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame
) {
    struct lumo_screenshot_client *client = data;

    (void)frame;
    if (client != NULL) {
        client->failed = true;
    }
}

static void lumo_screenshot_frame_damage(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height
) {
    (void)data;
    (void)frame;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
}

static void lumo_screenshot_frame_linux_dmabuf(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t format,
    uint32_t width,
    uint32_t height
) {
    (void)data;
    (void)frame;
    (void)format;
    (void)width;
    (void)height;
}

static void lumo_screenshot_frame_buffer_done(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame
) {
    struct lumo_screenshot_client *client = data;

    (void)frame;
    if (client == NULL || client->copy_requested) {
        return;
    }

    if (!client->have_buffer_info) {
        client->failed = true;
        return;
    }

    if (!lumo_screenshot_buffer_create(client)) {
        client->failed = true;
        return;
    }

    zwlr_screencopy_frame_v1_copy(client->frame, client->capture.buffer);
    client->copy_requested = true;
}

static const struct zwlr_screencopy_frame_v1_listener lumo_screenshot_frame_listener = {
    .buffer = lumo_screenshot_frame_buffer,
    .flags = lumo_screenshot_frame_flags,
    .ready = lumo_screenshot_frame_ready,
    .failed = lumo_screenshot_frame_failed,
    .damage = lumo_screenshot_frame_damage,
    .linux_dmabuf = lumo_screenshot_frame_linux_dmabuf,
    .buffer_done = lumo_screenshot_frame_buffer_done,
};

static void lumo_screenshot_registry_add(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version
) {
    struct lumo_screenshot_client *client = data;

    if (client == NULL) {
        return;
    }

    if (strcmp(interface, wl_shm_interface.name) == 0) {
        client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
        return;
    }

    if (strcmp(interface, wl_output_interface.name) == 0 && client->output == NULL) {
        client->output = wl_registry_bind(registry, name, &wl_output_interface,
            version < 2 ? version : 2);
        return;
    }

    if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        client->manager = wl_registry_bind(registry, name,
            &zwlr_screencopy_manager_v1_interface, version < 3 ? version : 3);
    }
}

static void lumo_screenshot_registry_remove(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener lumo_screenshot_registry_listener = {
    .global = lumo_screenshot_registry_add,
    .global_remove = lumo_screenshot_registry_remove,
};

static bool lumo_screenshot_default_path(char *buffer, size_t buffer_size) {
    time_t now_time;
    struct tm now_tm = {0};

    if (buffer == NULL || buffer_size == 0) {
        return false;
    }

    now_time = time(NULL);
    localtime_r(&now_time, &now_tm);
    return strftime(buffer, buffer_size, "lumo-screenshot-%Y%m%d-%H%M%S.png",
        &now_tm) != 0;
}

int main(int argc, char **argv) {
    struct lumo_screenshot_client client = {
        .capture.fd = -1,
    };
    char runtime_dir[PATH_MAX];
    char output_path[PATH_MAX];
    const char *socket_override = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            lumo_screenshot_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_override = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            client.output_path = argv[++i];
            continue;
        }
        if (client.output_path == NULL) {
            client.output_path = argv[i];
            continue;
        }

        fprintf(stderr, "lumo-screenshot: unknown argument '%s'\n", argv[i]);
        lumo_screenshot_usage(argv[0]);
        return 1;
    }

    if (client.output_path == NULL) {
        if (!lumo_screenshot_default_path(output_path, sizeof(output_path))) {
            fprintf(stderr, "lumo-screenshot: failed to build default path\n");
            return 1;
        }
        client.output_path = output_path;
    }

    if (!lumo_screenshot_runtime_dir(getenv("XDG_RUNTIME_DIR"), getuid(),
            runtime_dir, sizeof(runtime_dir))) {
        fprintf(stderr, "lumo-screenshot: failed to resolve XDG_RUNTIME_DIR\n");
        return 1;
    }
    setenv("XDG_RUNTIME_DIR", runtime_dir, 0);

    client.socket_name = lumo_screenshot_display_name(getenv("WAYLAND_DISPLAY"),
        socket_override);
    client.display = wl_display_connect(client.socket_name);
    if (client.display == NULL) {
        fprintf(stderr, "lumo-screenshot: failed to connect to '%s'\n",
            client.socket_name);
        return 1;
    }

    client.registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(client.registry, &lumo_screenshot_registry_listener,
        &client);
    wl_display_roundtrip(client.display);
    wl_display_roundtrip(client.display);

    if (client.shm == NULL || client.manager == NULL || client.output == NULL) {
        fprintf(stderr,
            "lumo-screenshot: missing wl_shm, wl_output, or screencopy manager\n");
        wl_display_disconnect(client.display);
        return 1;
    }

    client.frame = zwlr_screencopy_manager_v1_capture_output(client.manager, 0,
        client.output);
    if (client.frame == NULL) {
        fprintf(stderr, "lumo-screenshot: failed to request output capture\n");
        wl_display_disconnect(client.display);
        return 1;
    }

    zwlr_screencopy_frame_v1_add_listener(client.frame,
        &lumo_screenshot_frame_listener, &client);

    while (!client.ready && !client.failed) {
        if (wl_display_dispatch(client.display) == -1) {
            client.failed = true;
            break;
        }
    }

    if (!client.failed && !lumo_screenshot_write_png(&client)) {
        client.failed = true;
    }

    if (client.frame != NULL) {
        zwlr_screencopy_frame_v1_destroy(client.frame);
        client.frame = NULL;
    }
    if (client.output != NULL) {
        wl_output_destroy(client.output);
        client.output = NULL;
    }
    if (client.manager != NULL) {
        zwlr_screencopy_manager_v1_destroy(client.manager);
        client.manager = NULL;
    }
    if (client.shm != NULL) {
        wl_shm_destroy(client.shm);
        client.shm = NULL;
    }
    if (client.registry != NULL) {
        wl_registry_destroy(client.registry);
        client.registry = NULL;
    }
    lumo_screenshot_buffer_destroy(&client.capture);
    wl_display_disconnect(client.display);

    if (client.failed) {
        fprintf(stderr, "lumo-screenshot: capture failed\n");
        return 1;
    }

    printf("%s\n", client.output_path);
    return 0;
}

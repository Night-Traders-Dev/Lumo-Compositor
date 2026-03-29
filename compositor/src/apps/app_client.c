#define _DEFAULT_SOURCE
#include "lumo/app.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <png.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"

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
    struct zwp_text_input_manager_v3 *text_input_manager;
    struct zwp_text_input_v3 *text_input;
    bool text_input_enabled;
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
    struct wl_keyboard *keyboard;
    char term_lines[16][82];
    int term_line_count;
    int term_cursor;
    char term_input[82];
    int term_input_len;
    int scroll_offset;
    bool term_menu_open;
    bool stopwatch_running;
    uint64_t stopwatch_start_ms;
    uint64_t stopwatch_accumulated_ms;
    int clock_tab;
    uint32_t timer_total_sec;
    uint64_t timer_start_ms;
    bool timer_running;
    uint32_t alarm_hour;
    uint32_t alarm_min;
    bool alarm_enabled;
    int selected_row;
    char notes[8][128];
    int note_count;
    int note_editing;
    int pty_fd;
    pid_t pty_pid;
    char pty_line_buf[256];
    int pty_line_len;
    char pending_commit[256];
    int pending_commit_len;
    char media_files[32][64];
    int media_file_count;
    int media_selected;
    bool media_playing;
    pid_t media_pid;
    bool photo_viewing;
    uint32_t *photo_pixels;
    uint32_t photo_width;
    uint32_t photo_height;
};

static bool lumo_app_client_redraw(struct lumo_app_client *client);
static void lumo_app_notes_save(const struct lumo_app_client *client);
static void lumo_app_notes_load(struct lumo_app_client *client);
static void lumo_app_clock_save(const struct lumo_app_client *client);
static void lumo_app_clock_load(struct lumo_app_client *client);
static void lumo_app_sync_text_input_state(
    struct lumo_app_client *client,
    bool flush_pending
);

static void lumo_app_scan_media(struct lumo_app_client *client,
    const char *dir, const char **exts, int ext_count)
{
    DIR *d;
    struct dirent *entry;
    char path[1200];
    const char *home = getenv("HOME");

    if (home == NULL) home = "/home/orangepi";
    snprintf(path, sizeof(path), "%s/%s", home, dir);
    client->media_file_count = 0;
    client->media_selected = -1;
    client->media_playing = false;

    d = opendir(path);
    if (d == NULL) {
        /* create the directory if it doesn't exist */
        (void)mkdir(path, 0755);
        return;
    }

    while ((entry = readdir(d)) != NULL &&
            client->media_file_count < 32) {
        if (entry->d_name[0] == '.') continue;
        const char *dot = strrchr(entry->d_name, '.');
        if (dot == NULL) continue;
        bool match = false;
        for (int i = 0; i < ext_count; i++) {
            if (strcasecmp(dot, exts[i]) == 0) {
                match = true;
                break;
            }
        }
        if (match) {
            strncpy(client->media_files[client->media_file_count],
                entry->d_name,
                sizeof(client->media_files[0]) - 1);
            client->media_files[client->media_file_count]
                [sizeof(client->media_files[0]) - 1] = '\0';
            client->media_file_count++;
        }
    }
    closedir(d);
}

static void lumo_app_media_play(struct lumo_app_client *client,
    const char *dir)
{
    if (client->media_selected < 0 ||
            client->media_selected >= client->media_file_count) {
        return;
    }
    /* kill previous playback */
    if (client->media_pid > 0) {
        kill(client->media_pid, SIGTERM);
        waitpid(client->media_pid, NULL, WNOHANG);
        client->media_pid = 0;
    }
    char path[1300];
    const char *home = getenv("HOME");
    if (home == NULL) home = "/home/orangepi";
    snprintf(path, sizeof(path), "%s/%s/%s", home, dir,
        client->media_files[client->media_selected]);

    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        execlp("mpv", "mpv", "--no-video", "--really-quiet",
            path, (char *)NULL);
        _exit(127);
    }
    if (pid > 0) {
        client->media_pid = pid;
        client->media_playing = true;
    }
}

static void lumo_app_media_stop(struct lumo_app_client *client)
{
    if (client->media_pid > 0) {
        kill(client->media_pid, SIGTERM);
        /* Blocking wait: we just sent SIGTERM and must confirm the child has
         * exited before clearing media_pid to avoid leaving a zombie.
         * No race condition is possible here: the app client runs in a
         * single-threaded poll-based event loop, so media_pid cannot be
         * modified concurrently. */
        waitpid(client->media_pid, NULL, 0);
        client->media_pid = 0;
    }
    client->media_playing = false;
}

static bool lumo_app_load_png(const char *path, uint32_t **pixels_out,
    uint32_t *w_out, uint32_t *h_out)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
        NULL, NULL, NULL);
    if (!png) { fclose(fp); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, info);
    uint32_t w = png_get_image_width(png, info);
    uint32_t h = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_set_bgr(png); /* ARGB format */
    png_read_update_info(png, info);

    if (w > 4096 || h > 4096) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    uint32_t *pixels = calloc((size_t)w * h, sizeof(uint32_t));
    if (!pixels) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_bytep *rows = malloc(sizeof(png_bytep) * h);
    if (!rows) { free(pixels); png_destroy_read_struct(&png, &info, NULL); fclose(fp); return false; }
    for (uint32_t y = 0; y < h; y++)
        rows[y] = (png_bytep)(pixels + y * w);
    png_read_image(png, rows);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    *pixels_out = pixels;
    *w_out = w;
    *h_out = h;
    return true;
}

static bool lumo_app_load_jpeg(const char *path, uint32_t **pixels_out,
    uint32_t *w_out, uint32_t *h_out)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    uint32_t w = cinfo.output_width;
    uint32_t h = cinfo.output_height;
    if (w > 4096 || h > 4096) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return false;
    }

    uint32_t *pixels = calloc((size_t)w * h, sizeof(uint32_t));
    if (!pixels) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return false;
    }

    unsigned char *row_buf = malloc(w * 3);
    if (!row_buf) { free(pixels); jpeg_finish_decompress(&cinfo); jpeg_destroy_decompress(&cinfo); fclose(fp); return false; }
    while (cinfo.output_scanline < h) {
        unsigned char *p = row_buf;
        jpeg_read_scanlines(&cinfo, &p, 1);
        uint32_t y = cinfo.output_scanline - 1;
        for (uint32_t x = 0; x < w; x++) {
            pixels[y * w + x] = 0xFF000000 |
                ((uint32_t)row_buf[x * 3] << 16) |
                ((uint32_t)row_buf[x * 3 + 1] << 8) |
                (uint32_t)row_buf[x * 3 + 2];
        }
    }
    free(row_buf);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);

    *pixels_out = pixels;
    *w_out = w;
    *h_out = h;
    return true;
}

static bool lumo_app_load_image(struct lumo_app_client *client)
{
    if (client->media_selected < 0 ||
            client->media_selected >= client->media_file_count) {
        return false;
    }

    const char *home = getenv("HOME");
    if (!home) home = "/home/orangepi";
    char path[1300];
    snprintf(path, sizeof(path), "%s/Pictures/%s", home,
        client->media_files[client->media_selected]);

    /* free previous */
    if (client->photo_pixels) {
        free(client->photo_pixels);
        client->photo_pixels = NULL;
    }

    const char *dot = strrchr(path, '.');
    if (dot && (strcasecmp(dot, ".png") == 0)) {
        return lumo_app_load_png(path, &client->photo_pixels,
            &client->photo_width, &client->photo_height);
    }
    if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)) {
        return lumo_app_load_jpeg(path, &client->photo_pixels,
            &client->photo_width, &client->photo_height);
    }
    return false;
}

static void lumo_app_term_add_line(struct lumo_app_client *client,
    const char *line)
{
    if (client->term_line_count < 16) {
        strncpy(client->term_lines[client->term_line_count], line,
            sizeof(client->term_lines[0]) - 1);
        client->term_lines[client->term_line_count]
            [sizeof(client->term_lines[0]) - 1] = '\0';
        client->term_line_count++;
    } else {
        memmove(client->term_lines[0], client->term_lines[1],
            15 * sizeof(client->term_lines[0]));
        strncpy(client->term_lines[15], line,
            sizeof(client->term_lines[0]) - 1);
        client->term_lines[15][sizeof(client->term_lines[0]) - 1] = '\0';
    }
}

static void lumo_app_term_write(struct lumo_app_client *client,
    const char *data, size_t len)
{
    ssize_t ret;
    if (client->pty_fd < 0 || len == 0) return;
    ret = write(client->pty_fd, data, len);
    if (ret < 0 && errno != EAGAIN && errno != EINTR) {
        fprintf(stderr, "lumo-app: pty write failed: %s\n", strerror(errno));
    }
}

static bool lumo_app_pty_setup(struct lumo_app_client *client) {
    struct winsize ws = {
        .ws_row = 24,
        .ws_col = 80,
        .ws_xpixel = 0,
        .ws_ypixel = 0,
    };
    pid_t pid;
    int master_fd;
    const char *shell;

    pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (pid < 0) {
        fprintf(stderr, "lumo-app: forkpty failed: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        shell = getenv("SHELL");
        if (shell == NULL || shell[0] == '\0') shell = "/bin/sh";
        setenv("TERM", "dumb", 1);
        setenv("COLORTERM", "", 1);
        unsetenv("LS_COLORS");
        /* use /bin/sh for predictable behavior — fish/zsh don't
         * accept --norc --noprofile and our dumb-terminal parser
         * can't handle their escape sequences anyway */
        execlp("/bin/sh", "sh", (char *)NULL);
        _exit(127);
    }

    int flags = fcntl(master_fd, F_GETFL, 0);
    if (flags >= 0) fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    client->pty_fd = master_fd;
    client->pty_pid = pid;
    client->pty_line_len = 0;
    return true;
}

static void lumo_app_pty_read(struct lumo_app_client *client) {
    char buf[512];
    ssize_t n;
    bool changed = false;

    if (client->pty_fd < 0) return;

    while ((n = read(client->pty_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\x1b') {
                /* skip ANSI escape sequences */
                i++;
                if (i < n && buf[i] == '[') {
                    i++;
                    while (i < n && !((buf[i] >= 'A' && buf[i] <= 'Z') ||
                            (buf[i] >= 'a' && buf[i] <= 'z')))
                        i++;
                }
                continue;
            }
            if (ch == '\r') {
                client->pty_line_len = 0;
                continue;
            }
            if (ch == '\n') {
                client->pty_line_buf[client->pty_line_len] = '\0';
                lumo_app_term_add_line(client, client->pty_line_buf);
                client->pty_line_len = 0;
                changed = true;
                continue;
            }
            if (ch == '\b' || ch == 0x7f) {
                if (client->pty_line_len > 0) client->pty_line_len--;
                continue;
            }
            if (ch < 0x20 && ch != '\t') continue;
            if (ch == '\t') {
                int spaces = 8 - (client->pty_line_len % 8);
                for (int s = 0; s < spaces &&
                        client->pty_line_len < (int)sizeof(client->pty_line_buf) - 1; s++)
                    client->pty_line_buf[client->pty_line_len++] = ' ';
                continue;
            }
            /* Intentional truncation: characters beyond the line buffer
             * capacity are silently dropped.  This is acceptable for a
             * dumb terminal — long lines simply get clipped at display width. */
            if (client->pty_line_len < (int)sizeof(client->pty_line_buf) - 1) {
                client->pty_line_buf[client->pty_line_len++] = ch;
            }
        }
    }

    /* current incomplete line goes into term_input for display */
    client->pty_line_buf[client->pty_line_len] = '\0';
    strncpy(client->term_input, client->pty_line_buf,
        sizeof(client->term_input) - 1);
    client->term_input[sizeof(client->term_input) - 1] = '\0';
    client->term_input_len = client->pty_line_len > 81 ? 81 :
        client->pty_line_len;

    if (changed || n > 0) {
        (void)lumo_app_client_redraw(client);
    }
}

static void lumo_app_pty_cleanup(struct lumo_app_client *client) {
    if (client->pty_fd >= 0) {
        close(client->pty_fd);
        client->pty_fd = -1;
    }
    if (client->pty_pid > 0) {
        kill(client->pty_pid, SIGTERM);
        waitpid(client->pty_pid, NULL, WNOHANG);
        client->pty_pid = 0;
    }
}

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
        /* compute timer remaining */
        if (client->timer_running && client->timer_start_ms > 0) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 +
                (uint64_t)ts.tv_nsec / 1000000;
            uint64_t elapsed_sec = (now_ms - client->timer_start_ms) / 1000;
            if (elapsed_sec >= client->timer_total_sec) {
                client->timer_running = false;
                client->timer_total_sec = 0;
            }
        }
        struct lumo_app_render_context ctx = {
            .app_id = client->app_id,
            .close_active = client->close_active,
            .browse_path = client->browse_path,
            .scroll_offset = client->scroll_offset,
            .stopwatch_running = client->stopwatch_running,
            .stopwatch_elapsed_ms = sw_elapsed,
            .clock_tab = client->clock_tab,
            .timer_total_sec = client->timer_total_sec,
            .timer_remaining_sec = client->timer_total_sec,
            .timer_running = client->timer_running,
            .alarm_hour = client->alarm_hour,
            .alarm_min = client->alarm_min,
            .alarm_enabled = client->alarm_enabled,
            .selected_row = client->selected_row,
            .note_count = client->note_count,
            .note_editing = client->note_editing,
            .term_line_count = client->term_line_count,
            .term_input_len = client->term_input_len,
            .term_menu_open = client->term_menu_open,
            .media_file_count = client->media_file_count,
            .media_selected = client->media_selected,
            .media_playing = client->media_playing,
            .photo_viewing = client->photo_viewing,
            .photo_pixels = client->photo_pixels,
            .photo_width = client->photo_width,
            .photo_height = client->photo_height,
        };
        memcpy(ctx.notes, client->notes, sizeof(ctx.notes));
        memcpy(ctx.term_lines, client->term_lines, sizeof(ctx.term_lines));
        memcpy(ctx.term_input, client->term_input, sizeof(ctx.term_input));
        memcpy(ctx.media_files, client->media_files,
            sizeof(ctx.media_files));
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
        fprintf(stderr, "lumo-app: xdg_toplevel close received\n");
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

    /* close button disabled — apps are dismissed via compositor gesture */
    (void)should_close;
    client->pointer_pressed = false;
    lumo_app_client_set_close_active(client, false);
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

    /* try to enable text-input on touch if not already enabled —
     * flush pending events first so the compositor's enter event
     * sets focused_surface before we call enable()+commit() */
    if (!client->text_input_enabled &&
            lumo_app_wants_osk(client->app_id, client->note_editing)) {
        lumo_app_sync_text_input_state(client, true);
    }
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
    /* close button disabled — apps are dismissed via bottom-edge
     * swipe gesture in the compositor, not via a touch target */
    (void)should_close;

    /* terminal menu */
    if (client->app_id == LUMO_APP_MESSAGES) {
        if (client->term_menu_open) {
            /* centered menu hit test */
            int mx = (int)client->touch_down_x;
            int my = (int)client->touch_down_y;
            int menu_w = (int)client->width * 2 / 3;
            if (menu_w < 240) menu_w = 240;
            int menu_x = ((int)client->width - menu_w) / 2;
            int menu_y = ((int)client->height - 220) / 2;
            int item_h = 44;
            int iy = menu_y + 46;
            if (mx >= menu_x && mx <= menu_x + menu_w) {
                if (my >= iy && my < iy + item_h) {
                    /* New — clear terminal */
                    client->term_line_count = 0;
                    client->term_input[0] = '\0';
                    client->term_input_len = 0;
                    client->term_menu_open = false;
                    (void)lumo_app_client_redraw(client);
                    return;
                } else if (my >= iy + item_h && my < iy + item_h * 2) {
                    /* Keyboard — toggle OSK via text-input */
                    if (client->text_input != NULL) {
                        /* dispatch pending to get enter event first */
                        wl_display_roundtrip(client->display);

                        if (client->text_input_enabled) {
                            zwp_text_input_v3_disable(client->text_input);
                            zwp_text_input_v3_commit(client->text_input);
                            client->text_input_enabled = false;
                        } else {
                            zwp_text_input_v3_enable(client->text_input);
                            zwp_text_input_v3_set_content_type(
                                client->text_input,
                                ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
                                ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL);
                            zwp_text_input_v3_commit(client->text_input);
                            client->text_input_enabled = true;
                        }
                        wl_display_flush(client->display);
                        fprintf(stderr, "lumo-app: keyboard toggled %s\n",
                            client->text_input_enabled ? "on" : "off");
                    }
                    client->term_menu_open = false;
                    (void)lumo_app_client_redraw(client);
                    return;
                } else if (my >= iy + item_h * 2 && my < iy + item_h * 3) {
                    /* Settings — placeholder */
                    client->term_menu_open = false;
                    (void)lumo_app_client_redraw(client);
                    return;
                } else if (my >= iy + item_h * 3 && my < iy + item_h * 4) {
                    /* About — placeholder */
                    client->term_menu_open = false;
                    (void)lumo_app_client_redraw(client);
                    return;
                }
            }
            /* tap outside menu closes it */
            client->term_menu_open = false;
            (void)lumo_app_client_redraw(client);
            return;
        }
        /* tap on title "LUMO TERMINAL" opens menu */
        if (client->touch_down_y < 38 && client->touch_down_x < 200) {
            client->term_menu_open = true;
            (void)lumo_app_client_redraw(client);
            return;
        }
    }

    /* media apps: tap on list item to select, tap selected to play/pause */
    if ((client->app_id == LUMO_APP_MUSIC ||
            client->app_id == LUMO_APP_PHOTOS ||
            client->app_id == LUMO_APP_VIDEOS) &&
            client->media_file_count > 0) {
        int ty = (int)client->touch_down_y;
        int list_start = (client->media_selected >= 0 &&
            client->app_id == LUMO_APP_VIDEOS)
            ? 56 + (int)client->height / 3 + 72 : 144;
        if (client->app_id == LUMO_APP_PHOTOS) {
            if (client->photo_viewing) {
                /* tap while viewing = exit viewer */
                client->photo_viewing = false;
                if (client->photo_pixels) {
                    free(client->photo_pixels);
                    client->photo_pixels = NULL;
                }
                (void)lumo_app_client_redraw(client);
            } else {
                /* grid: compute cell from touch position */
                int cols = 3;
                int pad = 8;
                int cell_w = ((int)client->width - pad * (cols + 1)) / cols;
                int cell_h = cell_w * 3 / 4;
                int col = ((int)client->touch_down_x - pad) / (cell_w + pad);
                int row_idx = (ty - 56) / (cell_h + pad);
                int idx = client->scroll_offset + row_idx * cols + col;
                if (col >= 0 && col < cols && idx >= 0 &&
                        idx < client->media_file_count) {
                    if (idx == client->media_selected) {
                        /* double-tap selected = view photo */
                        if (lumo_app_load_image(client)) {
                            client->photo_viewing = true;
                        }
                    } else {
                        client->media_selected = idx;
                    }
                    (void)lumo_app_client_redraw(client);
                }
            }
        } else if (ty >= list_start) {
            int row_h = (client->app_id == LUMO_APP_MUSIC) ? 36 : 40;
            int idx = client->scroll_offset +
                (ty - list_start) / row_h;
            if (idx >= 0 && idx < client->media_file_count) {
                if (idx == client->media_selected) {
                    /* tap selected item: toggle play */
                    if (client->media_playing) {
                        lumo_app_media_stop(client);
                    } else {
                        const char *dir =
                            client->app_id == LUMO_APP_MUSIC ? "Music" :
                            "Videos";
                        lumo_app_media_play(client, dir);
                    }
                } else {
                    client->media_selected = idx;
                }
                (void)lumo_app_client_redraw(client);
            }
        } else if (client->media_selected >= 0 && ty < list_start) {
            /* tap on now playing / preview area: toggle */
            if (client->media_playing) {
                lumo_app_media_stop(client);
            } else {
                const char *dir =
                    client->app_id == LUMO_APP_MUSIC ? "Music" : "Videos";
                lumo_app_media_play(client, dir);
            }
            (void)lumo_app_client_redraw(client);
        }
    }

    if (client->app_id == LUMO_APP_CLOCK && client->width > 0 &&
            client->height > 0) {
        int ty = (int)client->touch_down_y;
        int tx = (int)client->touch_down_x;
        int tab_w = (int)client->width / 4;
        int cx = (int)client->width / 2;

        /* tab bar taps (y 48-84) */
        if (ty >= 48 && ty < 84) {
            int new_tab = tx / tab_w;
            if (new_tab >= 0 && new_tab <= 3 &&
                    new_tab != client->clock_tab) {
                client->clock_tab = new_tab;
                (void)lumo_app_client_redraw(client);
            }
            return;
        }

        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 +
            (uint64_t)ts.tv_nsec / 1000000;

        if (client->clock_tab == 2) {
            /* stopwatch: left half = start/stop, right half = reset */
            if (ty >= 240) {
                if (tx < cx) {
                    /* start/stop */
                    if (client->stopwatch_running) {
                        client->stopwatch_accumulated_ms +=
                            now_ms - client->stopwatch_start_ms;
                        client->stopwatch_running = false;
                    } else {
                        client->stopwatch_start_ms = now_ms;
                        client->stopwatch_running = true;
                    }
                } else {
                    /* reset */
                    client->stopwatch_accumulated_ms = 0;
                    client->stopwatch_running = false;
                    client->stopwatch_start_ms = 0;
                }
                (void)lumo_app_client_redraw(client);
            }
        } else if (client->clock_tab == 3) {
            /* timer controls */
            if (ty >= 230 && ty < 266) {
                /* +1M or +5M */
                if (tx < cx) {
                    client->timer_total_sec += 60;
                } else {
                    client->timer_total_sec += 300;
                }
                lumo_app_clock_save(client);
                (void)lumo_app_client_redraw(client);
            } else if (ty >= 280) {
                if (tx < cx) {
                    /* start/stop */
                    if (client->timer_running) {
                        client->timer_running = false;
                    } else if (client->timer_total_sec > 0) {
                        client->timer_start_ms = now_ms;
                        client->timer_running = true;
                    }
                } else {
                    /* reset */
                    client->timer_running = false;
                    client->timer_total_sec = 0;
                    client->timer_start_ms = 0;
                }
                (void)lumo_app_client_redraw(client);
            }
        } else if (client->clock_tab == 1) {
            /* alarm controls */
            if (ty >= 190 && ty < 226) {
                /* toggle alarm */
                client->alarm_enabled = !client->alarm_enabled;
                lumo_app_clock_save(client);
                (void)lumo_app_client_redraw(client);
            } else if (ty >= 242) {
                if (tx < cx) {
                    client->alarm_hour = (client->alarm_hour + 1) % 24;
                } else {
                    client->alarm_min = (client->alarm_min + 5) % 60;
                }
                lumo_app_clock_save(client);
                (void)lumo_app_client_redraw(client);
            }
        }

        /* save tab selection */
        if (ty >= 48 && ty < 84) {
            lumo_app_clock_save(client);
        }
    }

    if (client->app_id == LUMO_APP_FILES && client->width > 0 &&
            client->height > 0) {
        int entry_idx = lumo_app_files_entry_at(client->width, client->height,
            client->touch_down_x, client->touch_down_y);
        if (entry_idx >= 0) {
            int adjusted = entry_idx + client->scroll_offset;
            /* Path traversal safety: readdir() only returns entries that
             * exist within the currently open directory; it cannot produce
             * names that escape the tree (e.g. "../../etc/passwd").
             * Entries starting with '.' (including "." and "..") are
             * filtered out below, so browse_path can only move deeper into
             * the filesystem, never upward past its current position. */
            DIR *dir = opendir(client->browse_path);
            if (dir != NULL) {
                struct dirent *entry;
                int visible = 0;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] == '.') {
                        continue;
                    }
                    /* Explicit guard against directory traversal via
                     * ".." — already caught above by the dot check,
                     * but stated here for clarity and defence-in-depth. */
                    if (strcmp(entry->d_name, "..") == 0) continue;
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
            if (client->note_editing == row) {
                /* tap on editing note = stop editing */
                client->note_editing = -1;
            } else if (client->selected_row == row) {
                /* tap on selected = start editing */
                client->note_editing = row;
            } else {
                /* tap on unselected = select it, stop editing */
                client->selected_row = row;
                client->note_editing = -1;
            }
            (void)lumo_app_client_redraw(client);
        } else if (row == -2) {
            if (client->note_count < 8) {
                snprintf(client->notes[client->note_count],
                    sizeof(client->notes[0]), "");
                client->note_count++;
                /* auto-select and edit the new note */
                client->selected_row = client->note_count - 1;
                client->note_editing = client->note_count - 1;
                lumo_app_notes_save(client);
                (void)lumo_app_client_redraw(client);
            }
        }
        lumo_app_sync_text_input_state(client, client->note_editing >= 0);
    }

    if (client->app_id == LUMO_APP_SETTINGS && client->width > 0 &&
            client->height > 0) {
        if (client->selected_row >= 0 && client->touch_down_y < 40.0) {
            client->selected_row = -1;
            (void)lumo_app_client_redraw(client);
        } else if (client->selected_row < 0) {
            int row = lumo_app_settings_row_at(client->width, client->height,
                client->touch_down_x, client->touch_down_y);
            if (row >= 0) {
                client->selected_row = row;
                (void)lumo_app_client_redraw(client);
            }
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

        if (client->app_id == LUMO_APP_FILES ||
                (client->app_id == LUMO_APP_PHOTOS && !client->photo_viewing)) {
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

/* --- text-input-v3 listener (receives OSK committed text) --- */

static uint32_t lumo_app_text_input_purpose(enum lumo_app_id app_id) {
    return app_id == LUMO_APP_MESSAGES
        ? ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_TERMINAL
        : ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
}

static void lumo_app_sync_text_input_state(
    struct lumo_app_client *client,
    bool flush_pending
) {
    bool wants_osk;

    if (client == NULL || client->text_input == NULL) {
        return;
    }

    wants_osk = lumo_app_wants_osk(client->app_id, client->note_editing);
    if (wants_osk == client->text_input_enabled) {
        return;
    }

    if (wants_osk) {
        if (flush_pending && client->display != NULL) {
            wl_display_dispatch_pending(client->display);
        }
        zwp_text_input_v3_enable(client->text_input);
        zwp_text_input_v3_set_content_type(client->text_input,
            ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
            lumo_app_text_input_purpose(client->app_id));
        zwp_text_input_v3_commit(client->text_input);
        client->text_input_enabled = true;
        fprintf(stderr, "lumo-app: text-input enabled for %s\n",
            lumo_app_id_name(client->app_id));
    } else {
        zwp_text_input_v3_disable(client->text_input);
        zwp_text_input_v3_commit(client->text_input);
        client->text_input_enabled = false;
        fprintf(stderr, "lumo-app: text-input disabled for %s\n",
            lumo_app_id_name(client->app_id));
    }

    if (client->display != NULL) {
        wl_display_flush(client->display);
    }
}

static void lumo_app_text_input_enter(void *data,
    struct zwp_text_input_v3 *ti, struct wl_surface *surface)
{
    struct lumo_app_client *client = data;
    (void)ti; (void)surface;
    if (client == NULL) return;

    lumo_app_sync_text_input_state(client, false);
}

static void lumo_app_text_input_leave(void *data,
    struct zwp_text_input_v3 *ti, struct wl_surface *surface)
{
    struct lumo_app_client *client = data;
    (void)ti; (void)surface;
    if (client == NULL) return;
    if (client->text_input_enabled && client->text_input != NULL) {
        zwp_text_input_v3_disable(client->text_input);
        zwp_text_input_v3_commit(client->text_input);
        client->text_input_enabled = false;
        fprintf(stderr, "lumo-app: text-input disabled\n");
    }
}

static void lumo_app_text_input_preedit(void *data,
    struct zwp_text_input_v3 *ti, const char *text,
    int32_t cursor_begin, int32_t cursor_end)
{
    (void)data; (void)ti; (void)text;
    (void)cursor_begin; (void)cursor_end;
}

static void lumo_app_text_input_commit_string(void *data,
    struct zwp_text_input_v3 *ti, const char *text)
{
    struct lumo_app_client *client = data;
    (void)ti;
    if (client == NULL || text == NULL) return;
    /* buffer the committed text until done event */
    size_t len = strlen(text);
    if (client->pending_commit_len + (int)len <
            (int)sizeof(client->pending_commit) - 1) {
        memcpy(client->pending_commit + client->pending_commit_len, text, len);
        client->pending_commit_len += (int)len;
        client->pending_commit[client->pending_commit_len] = '\0';
    }
}

static void lumo_app_text_input_delete_surrounding(void *data,
    struct zwp_text_input_v3 *ti, uint32_t before, uint32_t after)
{
    struct lumo_app_client *client = data;
    (void)ti; (void)after;
    if (client == NULL) return;
    /* handle backspace from OSK */
    if (before > 0) {
        if (client->app_id == LUMO_APP_MESSAGES && client->pty_fd >= 0) {
            for (uint32_t i = 0; i < before; i++) {
                lumo_app_term_write(client, "\x7f", 1);
            }
        } else if (client->app_id == LUMO_APP_NOTES &&
                client->note_editing >= 0 &&
                client->note_editing < client->note_count) {
            size_t len = strlen(client->notes[client->note_editing]);
            for (uint32_t i = 0; i < before && len > 0; i++) {
                client->notes[client->note_editing][--len] = '\0';
            }
            lumo_app_notes_save(client);
            (void)lumo_app_client_redraw(client);
        }
    }
}

static void lumo_app_text_input_done(void *data,
    struct zwp_text_input_v3 *ti, uint32_t serial)
{
    struct lumo_app_client *client = data;
    (void)ti; (void)serial;
    if (client == NULL) return;

    if (client->pending_commit_len > 0) {
        if (client->app_id == LUMO_APP_MESSAGES && client->pty_fd >= 0) {
            /* forward OSK text to PTY */
            for (int i = 0; i < client->pending_commit_len; i++) {
                char ch = client->pending_commit[i];
                if (ch == '\n') {
                    lumo_app_term_write(client, "\n", 1);
                } else {
                    lumo_app_term_write(client, &ch, 1);
                }
            }
        } else if (client->app_id == LUMO_APP_NOTES) {
            /* OSK text for notes app */
            if (client->note_editing >= 0 &&
                    client->note_editing < client->note_count) {
                size_t cur = strlen(client->notes[client->note_editing]);
                size_t add = (size_t)client->pending_commit_len;
                if (cur + add < sizeof(client->notes[0]) - 1) {
                    memcpy(client->notes[client->note_editing] + cur,
                        client->pending_commit, add);
                    client->notes[client->note_editing][cur + add] = '\0';
                    lumo_app_notes_save(client);
                    (void)lumo_app_client_redraw(client);
                }
            }
        }
        client->pending_commit_len = 0;
        client->pending_commit[0] = '\0';
    }
}

static const struct zwp_text_input_v3_listener lumo_app_text_input_listener = {
    .enter = lumo_app_text_input_enter,
    .leave = lumo_app_text_input_leave,
    .preedit_string = lumo_app_text_input_preedit,
    .commit_string = lumo_app_text_input_commit_string,
    .delete_surrounding_text = lumo_app_text_input_delete_surrounding,
    .done = lumo_app_text_input_done,
};

/* --- keyboard handler (physical keys forwarded to PTY) --- */

static void lumo_app_keyboard_key(
    void *data, struct wl_keyboard *kb, uint32_t serial,
    uint32_t time, uint32_t key, uint32_t state
) {
    struct lumo_app_client *client = data;
    (void)kb; (void)serial; (void)time;

    if (client == NULL || state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
    if (client->app_id != LUMO_APP_MESSAGES) return;

    /* PTY mode: forward keystrokes to the shell */
    if (client->pty_fd >= 0) {
        if (key == 14) {
            lumo_app_term_write(client, "\x7f", 1); /* backspace */
        } else if (key == 28) {
            lumo_app_term_write(client, "\n", 1); /* enter */
        } else if (key == 57) {
            lumo_app_term_write(client, " ", 1); /* space */
        } else if (key >= 2 && key <= 52) {
            static const char keymap[] =
                "1234567890-="
                "\0qwertyuiop[]\0\0"
                "asdfghjkl;'\0\0\0"
                "zxcvbnm,./";
            int idx = (int)key - 2;
            if (idx >= 0 && idx < (int)sizeof(keymap) - 1 &&
                    keymap[idx] != '\0') {
                lumo_app_term_write(client, &keymap[idx], 1);
            }
        }
        return;
    }

    /* fallback: no PTY, echo locally */
    if (key == 14 && client->term_input_len > 0) {
        client->term_input[--client->term_input_len] = '\0';
        (void)lumo_app_client_redraw(client);
    } else if (key == 28) {
        if (client->term_line_count < 16) {
            char prompt[96];
            const char *user = getenv("USER");
            snprintf(prompt, sizeof(prompt), "%s$ %s",
                user ? user : "user", client->term_input);
            strncpy(client->term_lines[client->term_line_count],
                prompt, sizeof(client->term_lines[0]) - 1);
            client->term_line_count++;
        }
        client->term_input[0] = '\0';
        client->term_input_len = 0;
        (void)lumo_app_client_redraw(client);
    } else if (key >= 2 && key <= 52 && client->term_input_len < 78) {
        static const char keymap[] =
            "1234567890-="
            "\0qwertyuiop[]\0\0"
            "asdfghjkl;'\0\0\0"
            "zxcvbnm,./";
        int idx = (int)key - 2;
        if (idx >= 0 && idx < (int)sizeof(keymap) - 1 && keymap[idx] != '\0') {
            client->term_input[client->term_input_len++] = keymap[idx];
            client->term_input[client->term_input_len] = '\0';
            (void)lumo_app_client_redraw(client);
        }
    } else if (key == 57 && client->term_input_len < 78) {
        client->term_input[client->term_input_len++] = ' ';
        client->term_input[client->term_input_len] = '\0';
        (void)lumo_app_client_redraw(client);
    }
}

static void lumo_app_keyboard_keymap(void *d, struct wl_keyboard *k,
    uint32_t fmt, int32_t fd, uint32_t sz) {
    (void)d; (void)k; (void)fmt; close(fd); (void)sz;
}
static void lumo_app_keyboard_enter(void *d, struct wl_keyboard *k,
    uint32_t s, struct wl_surface *sf, struct wl_array *keys) {
    (void)d; (void)k; (void)s; (void)sf; (void)keys;
}
static void lumo_app_keyboard_leave(void *d, struct wl_keyboard *k,
    uint32_t s, struct wl_surface *sf) {
    (void)d; (void)k; (void)s; (void)sf;
}
static void lumo_app_keyboard_modifiers(void *d, struct wl_keyboard *k,
    uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t g) {
    (void)d; (void)k; (void)s; (void)dep; (void)lat; (void)lock; (void)g;
}
static void lumo_app_keyboard_repeat(void *d, struct wl_keyboard *k,
    int32_t rate, int32_t delay) {
    (void)d; (void)k; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener lumo_app_keyboard_listener = {
    .keymap = lumo_app_keyboard_keymap,
    .enter = lumo_app_keyboard_enter,
    .leave = lumo_app_keyboard_leave,
    .key = lumo_app_keyboard_key,
    .modifiers = lumo_app_keyboard_modifiers,
    .repeat_info = lumo_app_keyboard_repeat,
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

    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0) {
        if (client->keyboard == NULL) {
            client->keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(client->keyboard,
                &lumo_app_keyboard_listener, client);
        }
    } else if (client->keyboard != NULL) {
        wl_keyboard_release(client->keyboard);
        client->keyboard = NULL;
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
        return;
    }

    if (strcmp(interface, zwp_text_input_manager_v3_interface.name) == 0) {
        client->text_input_manager = wl_registry_bind(registry, name,
            &zwp_text_input_manager_v3_interface, 1);
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

    /* Ensure any active media playback is stopped and its child process is
     * reaped before tearing down the rest of the client state. */
    lumo_app_media_stop(client);

    lumo_app_pty_cleanup(client);

    if (client->text_input != NULL) {
        zwp_text_input_v3_destroy(client->text_input);
        client->text_input = NULL;
    }
    if (client->text_input_manager != NULL) {
        zwp_text_input_manager_v3_destroy(client->text_input_manager);
        client->text_input_manager = NULL;
    }
    if (client->pointer != NULL) {
        wl_pointer_release(client->pointer);
        client->pointer = NULL;
    }
    if (client->touch != NULL) {
        wl_touch_release(client->touch);
        client->touch = NULL;
    }
    if (client->keyboard != NULL) {
        wl_keyboard_release(client->keyboard);
        client->keyboard = NULL;
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

static void lumo_app_notes_load(struct lumo_app_client *client) {
    char path[1100];
    FILE *fp;

    /* Path safety: browse_path is initialised from $HOME (a trusted
     * environment variable) and the filename ".lumo-notes" is
     * hardcoded — no user-controlled input reaches this path. */
    snprintf(path, sizeof(path), "%s/.lumo-notes", client->browse_path);
    fp = fopen(path, "r");
    if (fp == NULL) {
        return;
    }

    client->note_count = 0;
    while (client->note_count < 8 &&
            fgets(client->notes[client->note_count],
                sizeof(client->notes[0]), fp) != NULL) {
        char *nl = strchr(client->notes[client->note_count], '\n');
        if (nl) *nl = '\0';
        if (client->notes[client->note_count][0] != '\0') {
            client->note_count++;
        }
    }
    fclose(fp);
}

static void lumo_app_notes_save(const struct lumo_app_client *client) {
    char path[1100];
    FILE *fp;

    /* Path safety: browse_path is initialised from $HOME (a trusted
     * environment variable) and the filename ".lumo-notes" is
     * hardcoded — no user-controlled input reaches this path. */
    snprintf(path, sizeof(path), "%s/.lumo-notes", client->browse_path);
    fp = fopen(path, "w");
    if (fp == NULL) {
        return;
    }

    for (int i = 0; i < client->note_count; i++) {
        fprintf(fp, "%s\n", client->notes[i]);
    }
    fclose(fp);
}

static void lumo_app_clock_save(const struct lumo_app_client *client) {
    const char *home = getenv("HOME");
    char path[1100];
    FILE *fp;
    if (home == NULL) return;
    snprintf(path, sizeof(path), "%s/.lumo-clock", home);
    fp = fopen(path, "w");
    if (fp == NULL) return;
    fprintf(fp, "alarm_hour=%u\n", client->alarm_hour);
    fprintf(fp, "alarm_min=%u\n", client->alarm_min);
    fprintf(fp, "alarm_enabled=%d\n", client->alarm_enabled ? 1 : 0);
    fprintf(fp, "timer_total=%u\n", client->timer_total_sec);
    fprintf(fp, "clock_tab=%d\n", client->clock_tab);
    fclose(fp);
}

static void lumo_app_clock_load(struct lumo_app_client *client) {
    const char *home = getenv("HOME");
    char path[1100], line[128];
    FILE *fp;
    if (home == NULL) return;
    snprintf(path, sizeof(path), "%s/.lumo-clock", home);
    fp = fopen(path, "r");
    if (fp == NULL) return;
    while (fgets(line, sizeof(line), fp)) {
        unsigned val;
        int ival;
        if (sscanf(line, "alarm_hour=%u", &val) == 1) client->alarm_hour = val;
        else if (sscanf(line, "alarm_min=%u", &val) == 1) client->alarm_min = val;
        else if (sscanf(line, "alarm_enabled=%d", &ival) == 1) client->alarm_enabled = ival != 0;
        else if (sscanf(line, "timer_total=%u", &val) == 1) client->timer_total_sec = val;
        else if (sscanf(line, "clock_tab=%d", &ival) == 1) client->clock_tab = ival;
    }
    fclose(fp);
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
        .pty_fd = -1,
        .pty_pid = 0,
        .alarm_hour = 6,
        .alarm_min = 30,
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

    if (client.app_id == LUMO_APP_NOTES) {
        lumo_app_notes_load(&client);
    }
    if (client.app_id == LUMO_APP_CLOCK) {
        lumo_app_clock_load(&client);
    }
    if (client.app_id == LUMO_APP_MUSIC) {
        static const char *audio_exts[] = {".mp3", ".wav", ".ogg", ".flac",
            ".m4a", ".aac"};
        lumo_app_scan_media(&client, "Music",
            audio_exts, sizeof(audio_exts) / sizeof(audio_exts[0]));
    }
    if (client.app_id == LUMO_APP_PHOTOS) {
        static const char *image_exts[] = {".jpg", ".jpeg", ".png", ".bmp",
            ".gif", ".webp"};
        lumo_app_scan_media(&client, "Pictures",
            image_exts, sizeof(image_exts) / sizeof(image_exts[0]));
    }
    if (client.app_id == LUMO_APP_VIDEOS) {
        static const char *video_exts[] = {".mp4", ".mkv", ".avi", ".mov",
            ".webm"};
        lumo_app_scan_media(&client, "Videos",
            video_exts, sizeof(video_exts) / sizeof(video_exts[0]));
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

    /* create text-input-v3 for OSK support */
    if (client.text_input_manager != NULL && client.seat != NULL) {
        client.text_input = zwp_text_input_manager_v3_get_text_input(
            client.text_input_manager, client.seat);
        if (client.text_input != NULL) {
            zwp_text_input_v3_add_listener(client.text_input,
                &lumo_app_text_input_listener, &client);
            /* do NOT enable here — wait for the compositor to send the
             * enter event (which arrives after wl_display_roundtrip).
             * enable()+commit() must happen after enter, otherwise
             * wlroots ignores the commit and current_enabled stays false */
            fprintf(stderr, "lumo-app: text-input-v3 ready\n");
        }
    }

    /* roundtrip so the compositor's enter event arrives before we
     * proceed — the enter callback will enable text-input for
     * apps that want the OSK (terminal, notes) */
    wl_display_roundtrip(client.display);

    /* set up PTY for terminal app */
    if (client.app_id == LUMO_APP_MESSAGES) {
        if (!lumo_app_pty_setup(&client)) {
            fprintf(stderr, "lumo-app: PTY setup failed, running in echo mode\n");
        } else {
            fprintf(stderr, "lumo-app: PTY shell started (pid=%d)\n",
                (int)client.pty_pid);
        }
    }

    {
        int display_fd = wl_display_get_fd(client.display);
        bool is_terminal = client.app_id == LUMO_APP_MESSAGES &&
            client.pty_fd >= 0;
        /* Clock must redraw every second to keep the displayed time current.
         * Settings polls every 5 s so status values (battery, wifi, etc.) stay
         * reasonably fresh without hammering the compositor. */
        bool needs_periodic = client.app_id == LUMO_APP_CLOCK ||
            client.app_id == LUMO_APP_SETTINGS || is_terminal;
        int timeout_ms = client.app_id == LUMO_APP_CLOCK ? 1000 :
            client.app_id == LUMO_APP_SETTINGS ? 5000 :
            is_terminal ? 500 : -1;

        while (client.running) {
            struct pollfd pfds[2];
            int nfds = 1;
            int ret;

            pfds[0].fd = display_fd;
            pfds[0].events = POLLIN;
            pfds[0].revents = 0;
            if (is_terminal) {
                pfds[1].fd = client.pty_fd;
                pfds[1].events = POLLIN;
                pfds[1].revents = 0;
                nfds = 2;
            }

            if (wl_display_dispatch_pending(client.display) == -1) {
                fprintf(stderr, "lumo-app: dispatch_pending failed\n");
                break;
            }
            if (wl_display_flush(client.display) < 0 && errno != EAGAIN) {
                fprintf(stderr, "lumo-app: display flush failed: %s\n",
                    strerror(errno));
                break;
            }

            ret = poll(pfds, (nfds_t)nfds, timeout_ms);
            if (ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "lumo-app: poll failed: %s\n",
                    strerror(errno));
                break;
            }

            if (ret == 0 && needs_periodic) {
                if (is_terminal) {
                    lumo_app_pty_read(&client);
                }
                /* always redraw on timeout for cursor blink and clock updates */
                (void)lumo_app_client_redraw(&client);
                continue;
            }

            if (pfds[0].revents & POLLIN) {
                if (wl_display_dispatch(client.display) == -1) {
                    fprintf(stderr, "lumo-app: display dispatch failed\n");
                    break;
                }
            }
            if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "lumo-app: display fd error (revents=0x%x)\n",
                    pfds[0].revents);
                break;
            }
            if (is_terminal && (pfds[1].revents & POLLIN)) {
                lumo_app_pty_read(&client);
            }
            if (is_terminal && (pfds[1].revents & POLLHUP)) {
                /* shell exited; is_terminal, nfds, timeout_ms and
                 * needs_periodic are declared outside the while loop so
                 * clearing them here correctly persists across subsequent
                 * loop iterations -- no stale flag risk. */
                fprintf(stderr, "lumo-app: PTY shell exited\n");
                lumo_app_term_add_line(&client, "[shell exited]");
                (void)lumo_app_client_redraw(&client);
                close(client.pty_fd);
                client.pty_fd = -1;
                is_terminal = false;
                nfds = 1;
                timeout_ms = -1;
                needs_periodic = false;
            }
        }
    }

    lumo_app_client_destroy(&client);
    return 0;
}

#include "lumo/shell.h"
#include "lumo/shell_protocol.h"
#include "lumo/version.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
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
    bool busy;
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
    struct lumo_shell_buffer *buffers[2];
    struct lumo_shell_surface_config config;
    int state_fd;
    char state_socket_path[PATH_MAX];
    struct lumo_shell_protocol_stream protocol_stream;
    uint32_t configured_width;
    uint32_t configured_height;
    uint32_t output_width_hint;
    uint32_t output_height_hint;
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
    bool compositor_quick_settings_visible;
    bool compositor_time_panel_visible;
    enum lumo_shell_remote_scrim_state compositor_scrim_state;
    uint32_t compositor_rotation_degrees;
    double compositor_gesture_threshold;
    uint32_t compositor_gesture_timeout_ms;
    bool compositor_keyboard_resize_pending;
    bool compositor_keyboard_resize_acked;
    uint32_t compositor_keyboard_resize_serial;
    bool compositor_touch_audit_active;
    bool compositor_touch_audit_saved;
    uint32_t compositor_touch_audit_step;
    uint32_t compositor_touch_audit_completed_mask;
    char compositor_touch_audit_profile[128];
    bool touch_debug_seen;
    bool touch_debug_active;
    double touch_debug_x;
    double touch_debug_y;
    uint32_t touch_debug_id;
    enum lumo_shell_touch_debug_phase touch_debug_phase;
    enum lumo_shell_touch_debug_target touch_debug_target;
    bool target_visible;
    bool surface_hidden;
    bool animation_active;
    double animation_from;
    double animation_to;
    uint64_t animation_started_msec;
    uint32_t animation_duration_msec;
    struct lumo_shell_target active_target;
    char weather_condition[32];
    char weather_humidity[16];
    char weather_wind[24];
    int weather_code;
    int weather_temp_c;
    uint32_t volume_pct;
    uint32_t brightness_pct;
    char toast_message[128];
    uint32_t toast_time_low;
    uint32_t toast_duration_ms;
};

static bool lumo_shell_client_redraw(struct lumo_shell_client *client);
static void lumo_draw_quick_settings_panel(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int bar_height, const struct lumo_shell_client *client);
static void lumo_draw_status(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_shell_client *client);

static uint32_t lumo_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) |
        ((uint32_t)r << 16) |
        ((uint32_t)g << 8) |
        (uint32_t)b;
}

static uint32_t lumo_u32_min(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

static uint32_t lumo_u32_max(uint32_t lhs, uint32_t rhs) {
    return lhs > rhs ? lhs : rhs;
}

static double lumo_clamp_unit(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

/* Material Design decelerate curve: cubic-bezier(0.0, 0.0, 0.2, 1.0)
 * Approximated as cubic ease-out for panel/drawer open */
static double lumo_ease_decelerate(double value) {
    double t = 1.0 - lumo_clamp_unit(value);
    return 1.0 - t * t * t;
}

/* Material Design standard curve: cubic-bezier(0.4, 0.0, 0.2, 1.0)
 * Smooth ease-in-out for general transitions */
static double lumo_ease_standard(double value) {
    double t = lumo_clamp_unit(value);
    if (t < 0.5) {
        return 4.0 * t * t * t;
    }
    double u = -2.0 * t + 2.0;
    return 1.0 - u * u * u / 2.0;
}

/* backward compat alias */
static double lumo_ease_out_cubic(double value) {
    return lumo_ease_decelerate(value);
}

static uint64_t lumo_now_msec(void) {
    struct timespec now = {0};

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void lumo_clear_pixels(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    if (pixels == NULL) {
        return;
    }

    memset(pixels, 0, (size_t)width * (size_t)height * sizeof(uint32_t));
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

    if (pixels == NULL || width == 0 || height == 0 || rect_width <= 0 ||
            rect_height <= 0) {
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

static void lumo_fill_vertical_gradient(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    uint32_t top_color,
    uint32_t bottom_color
) {
    uint8_t top_a;
    uint8_t top_r;
    uint8_t top_g;
    uint8_t top_b;
    uint8_t bottom_a;
    uint8_t bottom_r;
    uint8_t bottom_g;
    uint8_t bottom_b;

    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return;
    }

    top_a = (uint8_t)(top_color >> 24);
    top_r = (uint8_t)(top_color >> 16);
    top_g = (uint8_t)(top_color >> 8);
    top_b = (uint8_t)top_color;
    bottom_a = (uint8_t)(bottom_color >> 24);
    bottom_r = (uint8_t)(bottom_color >> 16);
    bottom_g = (uint8_t)(bottom_color >> 8);
    bottom_b = (uint8_t)bottom_color;

    for (int row = 0; row < rect->height; row++) {
        double t = rect->height > 1
            ? (double)row / (double)(rect->height - 1)
            : 0.0;
        uint32_t color = lumo_argb(
            (uint8_t)(top_a + (int)((bottom_a - top_a) * t)),
            (uint8_t)(top_r + (int)((bottom_r - top_r) * t)),
            (uint8_t)(top_g + (int)((bottom_g - top_g) * t)),
            (uint8_t)(top_b + (int)((bottom_b - top_b) * t))
        );

        lumo_fill_rect(pixels, width, height,
            rect->x, rect->y + row, rect->width, 1, color);
    }
}

static bool lumo_rounded_rect_contains(
    const struct lumo_rect *rect,
    int radius,
    int x,
    int y
) {
    int local_x;
    int local_y;
    int max_x;
    int max_y;
    int dx;
    int dy;

    if (rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return false;
    }
    if (radius <= 0) {
        return x >= rect->x && y >= rect->y &&
            x < rect->x + rect->width && y < rect->y + rect->height;
    }

    local_x = x - rect->x;
    local_y = y - rect->y;
    if (local_x < 0 || local_y < 0 ||
            local_x >= rect->width || local_y >= rect->height) {
        return false;
    }

    if ((local_x >= radius && local_x < rect->width - radius) ||
            (local_y >= radius && local_y < rect->height - radius)) {
        return true;
    }

    max_x = rect->width - radius - 1;
    max_y = rect->height - radius - 1;
    dx = local_x < radius ? local_x - radius : local_x - max_x;
    dy = local_y < radius ? local_y - radius : local_y - max_y;
    return dx * dx + dy * dy <= radius * radius;
}

static void lumo_fill_rounded_rect(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    uint32_t radius,
    uint32_t color
) {
    int y0;
    int y1;
    int x0;
    int x1;

    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return;
    }

    x0 = rect->x < 0 ? 0 : rect->x;
    y0 = rect->y < 0 ? 0 : rect->y;
    x1 = rect->x + rect->width;
    y1 = rect->y + rect->height;
    if (x1 > (int)width) {
        x1 = (int)width;
    }
    if (y1 > (int)height) {
        y1 = (int)height;
    }

    for (int row = y0; row < y1; row++) {
        int local_y = row - rect->y;
        bool in_corner_band = local_y < (int)radius ||
            local_y >= rect->height - (int)radius;

        if (!in_corner_band) {
            for (int col = x0; col < x1; col++) {
                pixels[row * (int)width + col] = color;
            }
            continue;
        }

        for (int col = x0; col < x1; col++) {
            if (lumo_rounded_rect_contains(rect, (int)radius, col, row)) {
                pixels[row * (int)width + col] = color;
            }
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

static bool lumo_glyph_rows(char ch, uint8_t rows[7]) {
    static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};

    if (rows == NULL) {
        return false;
    }

    switch ((unsigned char)toupper((unsigned char)ch)) {
    case 'A':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, 7);
        return true;
    case 'B':
        memcpy(rows, (uint8_t[]){0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, 7);
        return true;
    case 'C':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, 7);
        return true;
    case 'D':
        memcpy(rows, (uint8_t[]){0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}, 7);
        return true;
    case 'E':
        memcpy(rows, (uint8_t[]){0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, 7);
        return true;
    case 'F':
        memcpy(rows, (uint8_t[]){0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, 7);
        return true;
    case 'G':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x10, 0x10, 0x13, 0x11, 0x0F}, 7);
        return true;
    case 'H':
        memcpy(rows, (uint8_t[]){0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, 7);
        return true;
    case 'I':
        memcpy(rows, (uint8_t[]){0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, 7);
        return true;
    case 'J':
        memcpy(rows, (uint8_t[]){0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C}, 7);
        return true;
    case 'K':
        memcpy(rows, (uint8_t[]){0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, 7);
        return true;
    case 'L':
        memcpy(rows, (uint8_t[]){0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, 7);
        return true;
    case 'M':
        memcpy(rows, (uint8_t[]){0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, 7);
        return true;
    case 'N':
        memcpy(rows, (uint8_t[]){0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, 7);
        return true;
    case 'O':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, 7);
        return true;
    case 'P':
        memcpy(rows, (uint8_t[]){0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, 7);
        return true;
    case 'Q':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, 7);
        return true;
    case 'R':
        memcpy(rows, (uint8_t[]){0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, 7);
        return true;
    case 'S':
        memcpy(rows, (uint8_t[]){0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, 7);
        return true;
    case 'T':
        memcpy(rows, (uint8_t[]){0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, 7);
        return true;
    case 'U':
        memcpy(rows, (uint8_t[]){0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, 7);
        return true;
    case 'V':
        memcpy(rows, (uint8_t[]){0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, 7);
        return true;
    case 'W':
        memcpy(rows, (uint8_t[]){0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, 7);
        return true;
    case 'X':
        memcpy(rows, (uint8_t[]){0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, 7);
        return true;
    case 'Y':
        memcpy(rows, (uint8_t[]){0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, 7);
        return true;
    case 'Z':
        memcpy(rows, (uint8_t[]){0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}, 7);
        return true;
    case ',':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08}, 7);
        return true;
    case '.':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04}, 7);
        return true;
    case '?':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, 7);
        return true;
    case '0':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, 7);
        return true;
    case '1':
        memcpy(rows, (uint8_t[]){0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, 7);
        return true;
    case '2':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, 7);
        return true;
    case '3':
        memcpy(rows, (uint8_t[]){0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, 7);
        return true;
    case '4':
        memcpy(rows, (uint8_t[]){0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, 7);
        return true;
    case '5':
        memcpy(rows, (uint8_t[]){0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}, 7);
        return true;
    case '6':
        memcpy(rows, (uint8_t[]){0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, 7);
        return true;
    case '7':
        memcpy(rows, (uint8_t[]){0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, 7);
        return true;
    case '8':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, 7);
        return true;
    case '9':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C}, 7);
        return true;
    case ':':
        memcpy(rows, (uint8_t[]){0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}, 7);
        return true;
    case '-':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, 7);
        return true;
    case '/':
        memcpy(rows, (uint8_t[]){0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00}, 7);
        return true;
    case ' ':
        memcpy(rows, blank, 7);
        return true;
    default:
        memcpy(rows, blank, 7);
        return false;
    }
}

static int lumo_text_width(const char *text, int scale) {
    int width = 0;

    if (text == NULL || scale <= 0) {
        return 0;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        if (i > 0) {
            width += scale;
        }
        width += 5 * scale;
    }

    return width;
}

static void lumo_draw_text(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int x,
    int y,
    int scale,
    uint32_t color,
    const char *text
) {
    uint8_t rows[7];
    int cursor_x = x;

    if (pixels == NULL || text == NULL || scale <= 0) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        lumo_glyph_rows(text[i], rows);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if ((rows[row] & (1u << (4 - col))) == 0) {
                    continue;
                }
                lumo_fill_rect(pixels, width, height,
                    cursor_x + col * scale,
                    y + row * scale,
                    scale, scale, color);
            }
        }
        cursor_x += 5 * scale + scale;
    }
}

static void lumo_draw_text_centered(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    int scale,
    uint32_t color,
    const char *text
) {
    int text_width;
    int text_height;
    int text_x;
    int text_y;

    if (rect == NULL || text == NULL || text[0] == '\0') {
        return;
    }

    text_width = lumo_text_width(text, scale);
    text_height = 7 * scale;
    text_x = rect->x + (rect->width - text_width) / 2;
    text_y = rect->y + (rect->height - text_height) / 2;
    lumo_draw_text(pixels, width, height, text_x, text_y, scale, color, text);
}

static double lumo_shell_client_animation_value(
    const struct lumo_shell_client *client
) {
    double value;
    double progress;
    uint64_t now;

    if (client == NULL) {
        return 0.0;
    }
    if (!client->animation_active || client->animation_duration_msec == 0) {
        return client->target_visible ? 1.0 : 0.0;
    }

    now = lumo_now_msec();
    if (now <= client->animation_started_msec) {
        return client->animation_from;
    }

    progress = (double)(now - client->animation_started_msec) /
        (double)client->animation_duration_msec;
    /* use standard ease-in-out for showing, decelerate for hiding */
    progress = client->target_visible
        ? lumo_ease_standard(progress)
        : lumo_ease_decelerate(progress);
    value = client->animation_from +
        (client->animation_to - client->animation_from) * progress;
    return lumo_clamp_unit(value);
}

static void lumo_draw_touch_audit(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client
) {
    const uint32_t shell_top = lumo_argb(0xF4, 0x12, 0x18, 0x24);
    const uint32_t shell_bottom = lumo_argb(0xF8, 0x07, 0x0B, 0x12);
    const uint32_t title_color = lumo_argb(0xFF, 0xF3, 0xF7, 0xFB);
    const uint32_t subtitle_color = lumo_argb(0xFF, 0x95, 0xAA, 0xBC);
    const uint32_t pending_fill = lumo_argb(0xD8, 0x12, 0x18, 0x24);
    const uint32_t pending_stroke = lumo_argb(0xFF, 0x2F, 0x43, 0x56);
    const uint32_t current_fill = lumo_argb(0xFF, 0x0E, 0x31, 0x3A);
    const uint32_t current_stroke = lumo_argb(0xFF, 0x6A, 0xD4, 0xFF);
    const uint32_t done_fill = lumo_argb(0xFF, 0x12, 0x34, 0x2A);
    const uint32_t done_stroke = lumo_argb(0xFF, 0x70, 0xE3, 0x97);
    const uint32_t debug_dot = lumo_argb(0xE0, 0xFF, 0xB8, 0x4D);
    struct lumo_rect full_rect = {
        .x = 0,
        .y = 0,
        .width = (int)width,
        .height = (int)height,
    };
    struct lumo_rect badge_rect = {
        .x = 28,
        .y = 22,
        .width = 176,
        .height = 28,
    };
    struct lumo_rect status_rect = {
        .x = 28,
        .y = 58,
        .width = (int)width - 56,
        .height = 28,
    };
    struct lumo_rect footer_rect = {
        .x = 28,
        .y = (int)height - 52,
        .width = (int)width - 56,
        .height = 22,
    };
    const char *expected_label = NULL;
    char progress_text[64];
    size_t count;

    if (client == NULL) {
        return;
    }

    lumo_fill_vertical_gradient(pixels, width, height, &full_rect,
        shell_top, shell_bottom);
    lumo_fill_rounded_rect(pixels, width, height, &badge_rect, 14,
        lumo_argb(0xFF, 0x0C, 0x12, 0x1D));
    lumo_draw_text(pixels, width, height, badge_rect.x + 16,
        badge_rect.y + 8, 2, subtitle_color, "TOUCH AUDIT");
    lumo_draw_text(pixels, width, height, 28, 96, 4, title_color,
        "Calibrate The Edges");

    count = lumo_shell_touch_audit_point_count();
    if (client->compositor_touch_audit_step < count) {
        expected_label = lumo_shell_touch_audit_point_label(
            client->compositor_touch_audit_step);
    }
    snprintf(progress_text, sizeof(progress_text), "STEP %u / %zu  %s",
        client->compositor_touch_audit_step + 1u,
        count,
        expected_label != NULL ? expected_label : "COMPLETE");
    lumo_draw_text(pixels, width, height, status_rect.x, status_rect.y, 2,
        subtitle_color, progress_text);

    for (uint32_t point_index = 0; point_index < count; point_index++) {
        struct lumo_rect point_rect = {0};
        struct lumo_rect label_rect;
        bool completed =
            (client->compositor_touch_audit_completed_mask & (1u << point_index)) != 0;
        bool current = point_index == client->compositor_touch_audit_step;
        uint32_t fill = pending_fill;
        uint32_t stroke = pending_stroke;
        const char *label = lumo_shell_touch_audit_point_label(point_index);

        if (!lumo_shell_touch_audit_point_rect(width, height, point_index,
                &point_rect)) {
            continue;
        }

        if (completed) {
            fill = done_fill;
            stroke = done_stroke;
        } else if (current) {
            fill = current_fill;
            stroke = current_stroke;
        }

        lumo_fill_rounded_rect(pixels, width, height, &point_rect, 18, fill);
        lumo_draw_outline(pixels, width, height, &point_rect, 2, stroke);

        label_rect = point_rect;
        lumo_draw_text_centered(pixels, width, height, &label_rect,
            label != NULL && strlen(label) > 8 ? 2 : 3,
            title_color, label != NULL ? label : "POINT");
    }

    if (client->touch_debug_seen) {
        struct lumo_rect dot_rect = {
            .x = (int)client->touch_debug_x - 14,
            .y = (int)client->touch_debug_y - 14,
            .width = 28,
            .height = 28,
        };

        lumo_fill_rounded_rect(pixels, width, height, &dot_rect, 14, debug_dot);
    }

    if (client->compositor_touch_audit_saved &&
            client->compositor_touch_audit_profile[0] != '\0' &&
            strcmp(client->compositor_touch_audit_profile, "none") != 0) {
        lumo_draw_text(pixels, width, height, footer_rect.x, footer_rect.y, 2,
            subtitle_color, client->compositor_touch_audit_profile);
    } else {
        lumo_draw_text(pixels, width, height, footer_rect.x, footer_rect.y, 2,
            subtitle_color, "Tap the glowing target and move clockwise.");
    }
}

static void lumo_draw_launcher(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target,
    double visibility
) {
    const uint32_t panel_top = lumo_argb(0xFF, 0x3B, 0x1F, 0x34);
    const uint32_t panel_bottom = lumo_argb(0xFF, 0x1D, 0x11, 0x22);
    const uint32_t panel_stroke = lumo_argb(0xFF, 0x77, 0x21, 0x6F);
    const uint32_t title_color = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
    const uint32_t subtitle_color = lumo_argb(0xFF, 0xAE, 0xA7, 0x9F);
    const uint32_t tile_fill = lumo_argb(0xFF, 0x2C, 0x16, 0x28);
    const uint32_t tile_stroke = lumo_argb(0xFF, 0x5E, 0x2C, 0x56);
    const uint32_t highlight = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
    const uint32_t close_fill = lumo_argb(0xFF, 0x3B, 0x1F, 0x34);
    const uint32_t close_label = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
    const uint32_t accent_colors[] = {
        lumo_argb(0xFF, 0xE9, 0x54, 0x20),
        lumo_argb(0xFF, 0x77, 0x21, 0x6F),
        lumo_argb(0xFF, 0xE9, 0x54, 0x20),
        lumo_argb(0xFF, 0x77, 0x21, 0x6F),
    };
    struct lumo_rect panel_rect;
    struct lumo_rect accent_rect;
    struct lumo_rect title_badge;
    struct lumo_rect close_rect = {0};
    struct lumo_rect close_label_rect = {0};
    size_t tile_count = lumo_shell_launcher_tile_count();
    int slide_y;

    if (visibility <= 0.0) {
        return;
    }

    if (client != NULL && client->compositor_touch_audit_active) {
        lumo_draw_touch_audit(pixels, width, height, client);
        return;
    }

    if (client != NULL && (client->compositor_quick_settings_visible ||
            client->compositor_time_panel_visible) &&
            !client->compositor_launcher_visible) {
        int bar_h = 48;

        if (client->compositor_quick_settings_visible) {
            lumo_draw_quick_settings_panel(pixels, width, height, bar_h,
                client);
        }
        if (client->compositor_time_panel_visible) {
            uint32_t tp_bg = lumo_argb(0xF0, 0x2C, 0x00, 0x1E);
            uint32_t tp_stroke = lumo_argb(0x60, 0x77, 0x21, 0x6F);
            uint32_t tp_label = lumo_argb(0xFF, 0xAE, 0xA7, 0x9F);
            uint32_t tp_text = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
            uint32_t tp_accent = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
            struct lumo_rect tp;
            time_t now = time(NULL);
            struct tm tm_now = {0};
            char tbuf[16], dbuf[32], wbuf[16];
            int pw = (int)(width / 2);

            localtime_r(&now, &tm_now);
            strftime(tbuf, sizeof(tbuf), "%H:%M", &tm_now);
            strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &tm_now);
            snprintf(wbuf, sizeof(wbuf), "WEEK %d",
                (tm_now.tm_yday / 7) + 1);

            if (pw < 240) pw = 240;
            tp.x = 8;
            tp.y = bar_h + 4;
            tp.width = pw;
            tp.height = 220;
            lumo_fill_rounded_rect(pixels, width, height, &tp, 18, tp_bg);
            lumo_draw_outline(pixels, width, height, &tp, 1, tp_stroke);

            /* large time */
            {
                int tw = lumo_text_width(tbuf, 6);
                lumo_draw_text(pixels, width, height,
                    tp.x + tp.width / 2 - tw / 2, tp.y + 12,
                    6, tp_accent, tbuf);
            }

            /* date and day */
            {
                char day_name[16];
                strftime(day_name, sizeof(day_name), "%A", &tm_now);
                char full_date[48];
                snprintf(full_date, sizeof(full_date), "%s %s",
                    day_name, dbuf);
                int dw = lumo_text_width(full_date, 2);
                lumo_draw_text(pixels, width, height,
                    tp.x + tp.width / 2 - dw / 2, tp.y + 62,
                    2, tp_text, full_date);
            }

            /* week number */
            {
                int ww = lumo_text_width(wbuf, 2);
                lumo_draw_text(pixels, width, height,
                    tp.x + tp.width / 2 - ww / 2, tp.y + 82,
                    2, tp_label, wbuf);
            }

            /* weather section — separator line */
            {
                struct lumo_rect sep = {
                    tp.x + 16, tp.y + 100, tp.width - 32, 1};
                lumo_fill_rounded_rect(pixels, width, height,
                    &sep, 0, tp_stroke);
            }

            /* weather: temp + condition */
            if (client->weather_condition[0] != '\0' &&
                    strcmp(client->weather_condition, "unknown") != 0) {
                char temp_str[16];
                snprintf(temp_str, sizeof(temp_str), "%dF",
                    client->weather_temp_c);
                lumo_draw_text(pixels, width, height,
                    tp.x + 16, tp.y + 110, 4, tp_accent, temp_str);

                int tx = tp.x + 16 + lumo_text_width(temp_str, 4) + 12;
                lumo_draw_text(pixels, width, height,
                    tx, tp.y + 116, 2, tp_text,
                    client->weather_condition);

                /* humidity + wind on next line */
                int wy = tp.y + 148;
                if (client->weather_humidity[0] != '\0' &&
                        strcmp(client->weather_humidity, "--") != 0) {
                    char hum[24];
                    snprintf(hum, sizeof(hum), "HUM %s",
                        client->weather_humidity);
                    lumo_draw_text(pixels, width, height,
                        tp.x + 16, wy, 2, tp_label, hum);
                }
                if (client->weather_wind[0] != '\0' &&
                        strcmp(client->weather_wind, "--") != 0) {
                    char wnd[32];
                    snprintf(wnd, sizeof(wnd), "WIND %s",
                        client->weather_wind);
                    lumo_draw_text(pixels, width, height,
                        tp.x + tp.width / 2 + 4, wy, 2,
                        tp_label, wnd);
                }
            } else {
                lumo_draw_text(pixels, width, height,
                    tp.x + 16, tp.y + 116, 2, tp_label,
                    "WEATHER LOADING...");
            }
        }
        return;
    }

    slide_y = (int)((1.0 - visibility) * (height / 8));
    if (!lumo_shell_launcher_panel_rect(width, height, &panel_rect)) {
        return;
    }
    panel_rect.y += slide_y;

    lumo_fill_vertical_gradient(pixels, width, height, &panel_rect,
        panel_top, panel_bottom);
    lumo_draw_outline(pixels, width, height, &panel_rect, 2, panel_stroke);

    title_badge.x = panel_rect.x + 26;
    title_badge.y = panel_rect.y + 22;
    title_badge.width = lumo_u32_min((uint32_t)(panel_rect.width / 3), 180);
    title_badge.height = 28;
    lumo_fill_rounded_rect(pixels, width, height, &title_badge, 14,
        lumo_argb(0xFF, 0x2C, 0x00, 0x1E));
    lumo_draw_text(pixels, width, height, title_badge.x + 14,
        title_badge.y + 8, 2, subtitle_color, "APP DRAWER");
    lumo_draw_text(pixels, width, height, panel_rect.x + 26,
        panel_rect.y + 64, 3, title_color, "LUMO");

    (void)close_fill;
    (void)close_label;
    (void)close_rect;
    (void)close_label_rect;
    for (uint32_t tile_index = 0; tile_index < tile_count; tile_index++) {
        struct lumo_rect tile_rect;
        struct lumo_rect icon_rect;
        struct lumo_rect label_rect;
        struct lumo_rect accent_bar;
        bool active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_LAUNCHER_TILE &&
            active_target->index == tile_index;
        const char *label = lumo_shell_launcher_tile_label(tile_index);
        uint32_t accent = accent_colors[tile_index %
            (sizeof(accent_colors) / sizeof(accent_colors[0]))];

        if (!lumo_shell_launcher_tile_rect(width, height, tile_index, &tile_rect)) {
            continue;
        }

        tile_rect.y += slide_y;
        lumo_fill_rounded_rect(pixels, width, height, &tile_rect, 18, tile_fill);
        lumo_draw_outline(pixels, width, height, &tile_rect, 2,
            active ? highlight : tile_stroke);

        accent_bar.x = tile_rect.x + 14;
        accent_bar.y = tile_rect.y + 14;
        accent_bar.width = tile_rect.width - 28;
        accent_bar.height = 10;
        lumo_fill_rounded_rect(pixels, width, height, &accent_bar, 5, accent);

        icon_rect.x = tile_rect.x + 18;
        icon_rect.y = tile_rect.y + 34;
        icon_rect.width = 44;
        icon_rect.height = 44;
        lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 12,
            lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));

        {
            int ix = icon_rect.x + 8;
            int iy = icon_rect.y + 8;
            int isz = 28;
            uint32_t ic = accent;

            switch (tile_index) {
            case 0: /* Phone - handset */
                lumo_fill_rect(pixels, width, height, ix+10, iy, 8, 6, ic);
                lumo_fill_rect(pixels, width, height, ix+8, iy+6, 12, 2, ic);
                lumo_fill_rect(pixels, width, height, ix+12, iy+8, 4, 14, ic);
                lumo_fill_rect(pixels, width, height, ix+8, iy+22, 12, 2, ic);
                lumo_fill_rect(pixels, width, height, ix+10, iy+24, 8, 4, ic);
                break;
            case 1: /* Terminal - prompt */
                lumo_fill_rect(pixels, width, height, ix, iy, isz, 2, ic);
                lumo_fill_rect(pixels, width, height, ix, iy, 2, isz, ic);
                lumo_fill_rect(pixels, width, height, ix, iy+isz-2, isz, 2, ic);
                lumo_fill_rect(pixels, width, height, ix+isz-2, iy, 2, isz, ic);
                lumo_fill_rect(pixels, width, height, ix+6, iy+10, 4, 4, ic);
                lumo_fill_rect(pixels, width, height, ix+10, iy+14, 4, 4, ic);
                lumo_fill_rect(pixels, width, height, ix+16, iy+20, 8, 2, ic);
                break;
            case 2: /* Browser - globe */
                lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 22, ic);
                lumo_fill_rect(pixels, width, height, ix+12, iy, 4, isz, lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                lumo_fill_rect(pixels, width, height, ix, iy+12, isz, 4, lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                break;
            case 3: /* Camera - lens */
                lumo_fill_rect(pixels, width, height, ix+4, iy, isz-8, 4, ic);
                lumo_fill_rect(pixels, width, height, ix, iy+4, isz, isz-4, ic);
                {
                    struct lumo_rect lens = {ix+8, iy+10, 12, 12};
                    lumo_fill_rounded_rect(pixels, width, height, &lens, 6,
                        lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                }
                break;
            case 4: /* Maps - pin */
                lumo_fill_rect(pixels, width, height, ix+10, iy+2, 8, 14, ic);
                lumo_fill_rect(pixels, width, height, ix+8, iy+4, 12, 10, ic);
                lumo_fill_rect(pixels, width, height, ix+12, iy+16, 4, 10, ic);
                break;
            case 5: /* Music - note */
                lumo_fill_rect(pixels, width, height, ix+18, iy+2, 4, 20, ic);
                lumo_fill_rect(pixels, width, height, ix+8, iy+4, 14, 4, ic);
                lumo_fill_rect(pixels, width, height, ix+4, iy+6, 4, 18, ic);
                {
                    struct lumo_rect n1 = {ix, iy+20, 10, 8};
                    struct lumo_rect n2 = {ix+14, iy+18, 10, 8};
                    lumo_fill_rounded_rect(pixels, width, height, &n1, 4, ic);
                    lumo_fill_rounded_rect(pixels, width, height, &n2, 4, ic);
                }
                break;
            case 6: /* Photos - mountain */
                lumo_fill_rect(pixels, width, height, ix, iy+isz-8, isz, 8, ic);
                lumo_fill_rect(pixels, width, height, ix+4, iy+12, 4, 8, ic);
                lumo_fill_rect(pixels, width, height, ix+8, iy+8, 4, 12, ic);
                lumo_fill_rect(pixels, width, height, ix+12, iy+14, 4, 6, ic);
                lumo_fill_rect(pixels, width, height, ix+16, iy+6, 4, 14, ic);
                lumo_fill_rect(pixels, width, height, ix+20, iy+10, 4, 10, ic);
                break;
            case 7: /* Videos - play triangle */
                lumo_fill_rect(pixels, width, height, ix+8, iy+4, 4, isz-8, ic);
                lumo_fill_rect(pixels, width, height, ix+12, iy+6, 4, isz-12, ic);
                lumo_fill_rect(pixels, width, height, ix+16, iy+8, 4, isz-16, ic);
                lumo_fill_rect(pixels, width, height, ix+20, iy+10, 4, isz-20, ic);
                break;
            case 8: /* Clock - clock face */
                lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 22, ic);
                {
                    struct lumo_rect inner = {ix+4, iy+4, isz-8, isz-8};
                    lumo_fill_rounded_rect(pixels, width, height, &inner, 10,
                        lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                }
                lumo_fill_rect(pixels, width, height, ix+13, iy+6, 2, 10, ic);
                lumo_fill_rect(pixels, width, height, ix+13, iy+13, 8, 2, ic);
                break;
            case 9: /* Notes - lines */
                lumo_fill_rect(pixels, width, height, ix+2, iy+4, isz-4, 3, ic);
                lumo_fill_rect(pixels, width, height, ix+2, iy+10, isz-8, 3, ic);
                lumo_fill_rect(pixels, width, height, ix+2, iy+16, isz-4, 3, ic);
                lumo_fill_rect(pixels, width, height, ix+2, iy+22, isz-12, 3, ic);
                break;
            case 10: /* Files - folder */
                lumo_fill_rect(pixels, width, height, ix+2, iy+4, 10, 4, ic);
                lumo_fill_rect(pixels, width, height, ix+2, iy+8, isz-4, isz-12, ic);
                lumo_fill_rect(pixels, width, height, ix+2, iy+8, isz-4, 4,
                    lumo_argb(0xFF, 0x77, 0x21, 0x6F));
                break;
            case 11: /* Settings - gear */
                lumo_fill_rect(pixels, width, height, ix+10, iy, 8, isz, ic);
                lumo_fill_rect(pixels, width, height, ix, iy+10, isz, 8, ic);
                lumo_fill_rect(pixels, width, height, ix+4, iy+4, 6, 6, ic);
                lumo_fill_rect(pixels, width, height, ix+18, iy+4, 6, 6, ic);
                lumo_fill_rect(pixels, width, height, ix+4, iy+18, 6, 6, ic);
                lumo_fill_rect(pixels, width, height, ix+18, iy+18, 6, 6, ic);
                {
                    struct lumo_rect ctr = {ix+8, iy+8, 12, 12};
                    lumo_fill_rounded_rect(pixels, width, height, &ctr, 6,
                        lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                }
                break;
            default:
                lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 12, accent);
                break;
            }
        }

        label_rect.x = tile_rect.x + 14;
        label_rect.y = tile_rect.y + tile_rect.height - 36;
        label_rect.width = tile_rect.width - 28;
        label_rect.height = 20;
        lumo_draw_text_centered(pixels, width, height, &label_rect, 2,
            title_color, label != NULL ? label : "APP");
    }
}

static void lumo_draw_osk(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target,
    double visibility
) {
    /* Lomiri / ubuntu-frame-osk inspired dark charcoal theme */
    const uint32_t bg_top = lumo_argb(0xFF, 0x2A, 0x2A, 0x2E);
    const uint32_t bg_bottom = lumo_argb(0xFF, 0x1E, 0x1E, 0x22);
    const uint32_t key_fill = lumo_argb(0xFF, 0x44, 0x44, 0x4A);
    const uint32_t key_border = lumo_argb(0xFF, 0x38, 0x38, 0x3E);
    const uint32_t special_fill = lumo_argb(0xFF, 0x38, 0x38, 0x3E);
    const uint32_t action_fill = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
    const uint32_t label_color = lumo_argb(0xFF, 0xF0, 0xF0, 0xF0);
    const uint32_t label_dim = lumo_argb(0xFF, 0xC0, 0xC0, 0xC4);
    const uint32_t active_fill = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
    const uint32_t active_border = lumo_argb(0xFF, 0xFF, 0x7B, 0x42);
    const uint32_t handle_color = lumo_argb(0xFF, 0x58, 0x58, 0x5E);
    struct lumo_rect panel_rect;
    struct lumo_rect handle_rect;
    size_t key_count = lumo_shell_osk_key_count();
    int translate_y;

    (void)client;
    if (visibility <= 0.0) {
        return;
    }

    translate_y = (int)((1.0 - visibility) * (height + 12));

    /* full-width panel background */
    panel_rect.x = 0;
    panel_rect.y = translate_y;
    panel_rect.width = (int)width;
    panel_rect.height = (int)height;
    lumo_fill_vertical_gradient(pixels, width, height, &panel_rect,
        bg_top, bg_bottom);

    /* subtle top edge line */
    {
        struct lumo_rect edge = {0, translate_y, (int)width, 1};
        lumo_fill_rounded_rect(pixels, width, height, &edge, 0,
            lumo_argb(0xFF, 0x50, 0x50, 0x56));
    }

    /* small grabber handle */
    handle_rect.x = (int)((width - 48) / 2);
    handle_rect.y = translate_y + 6;
    handle_rect.width = 48;
    handle_rect.height = 4;
    lumo_fill_rounded_rect(pixels, width, height, &handle_rect, 2,
        handle_color);

    for (uint32_t key_index = 0; key_index < key_count; key_index++) {
        struct lumo_rect key_rect;
        struct lumo_rect label_rect;
        const char *label = lumo_shell_osk_key_label(key_index);
        const char *commit = lumo_shell_osk_key_text(key_index);
        bool active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_OSK_KEY &&
            active_target->index == key_index;
        bool is_enter = label != NULL && strcmp(label, "ENTER") == 0;
        bool is_space = label != NULL && strcmp(label, "SPACE") == 0;
        bool is_special = label != NULL &&
            (strcmp(label, "<-") == 0 || strcmp(label, "^") == 0);
        int scale = 3;
        uint32_t fill;
        uint32_t border;
        uint32_t text_col;

        if (!lumo_shell_osk_key_rect(width, height, key_index, &key_rect)) {
            continue;
        }

        key_rect.y += translate_y;

        /* choose colors based on key type */
        if (active) {
            fill = active_fill;
            border = active_border;
            text_col = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
        } else if (is_enter) {
            fill = action_fill;
            border = lumo_argb(0xFF, 0xD0, 0x48, 0x1A);
            text_col = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
        } else if (is_space) {
            fill = lumo_argb(0xFF, 0x50, 0x50, 0x56);
            border = lumo_argb(0xFF, 0x42, 0x42, 0x48);
            text_col = label_dim;
        } else if (is_special) {
            fill = special_fill;
            border = key_border;
            text_col = label_dim;
        } else {
            fill = key_fill;
            border = key_border;
            text_col = label_color;
        }

        lumo_fill_rounded_rect(pixels, width, height, &key_rect, 10, fill);
        lumo_draw_outline(pixels, width, height, &key_rect, 1, border);

        label_rect = key_rect;
        if (label != NULL && strlen(label) > 4) {
            scale = 2;
        }
        lumo_draw_text_centered(pixels, width, height, &label_rect, scale,
            text_col,
            label != NULL ? label :
            (commit != NULL ? commit : ""));
    }
}

static void lumo_draw_gesture(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target
) {
    struct lumo_rect handle_rect = {0};
    uint32_t pill_w;
    uint32_t pill_h;

    (void)client;
    (void)active_target;

    if (!lumo_shell_gesture_handle_rect(width, height, &handle_rect)) {
        return;
    }

    pill_w = (uint32_t)(handle_rect.width - 20);
    pill_h = (uint32_t)handle_rect.height;

    for (uint32_t y = 0; y < pill_h; y++) {
        uint32_t alpha;
        uint32_t row_y_abs = (uint32_t)handle_rect.y + y;
        if (y < pill_h / 3) {
            alpha = 0x10 + (y * 0x60) / (pill_h / 3);
        } else if (y < pill_h * 2 / 3) {
            alpha = 0x70;
        } else {
            alpha = 0x70 - ((y - pill_h * 2 / 3) * 0x60) / (pill_h / 3);
        }

        uint32_t color = lumo_argb((uint8_t)alpha, 0xAE, 0xA7, 0x9F);
        uint32_t *row = pixels + row_y_abs * width;
        uint32_t start = (uint32_t)handle_rect.x + 10;
        uint32_t end = start + pill_w;
        if (end > width) end = width;
        for (uint32_t x = start; x < end; x++) {
            row[x] = color;
        }
    }
}

static void lumo_draw_wifi_bars(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int x,
    int y,
    int bar_count,
    uint32_t active_color,
    uint32_t dim_color
) {
    int total_bars = 4;
    int bar_gap = 3;
    int bar_w = 4;

    for (int i = 0; i < total_bars; i++) {
        int bh = 4 + i * 4;
        int bx = x + i * (bar_w + bar_gap);
        int by = y + (total_bars * 4) - bh;
        uint32_t color = i < bar_count ? active_color : dim_color;
        lumo_fill_rect(pixels, width, height, bx, by, bar_w, bh, color);
    }
}

static void lumo_draw_quick_settings_panel(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int bar_height,
    const struct lumo_shell_client *client
) {
    const uint32_t panel_bg = lumo_argb(0xF0, 0x2C, 0x00, 0x1E);
    const uint32_t panel_stroke = lumo_argb(0x60, 0x77, 0x21, 0x6F);
    const uint32_t label_color = lumo_argb(0xFF, 0xAE, 0xA7, 0x9F);
    const uint32_t value_color = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
    const uint32_t accent = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
    const uint32_t dim = lumo_argb(0x40, 0x77, 0x21, 0x6F);
    struct lumo_rect panel;
    int panel_w = (int)(width / 2);
    int row_y;

    if (panel_w < 200) {
        panel_w = 200;
    }

    panel.x = (int)width - panel_w - 8;
    panel.y = bar_height + 4;
    panel.width = panel_w;
    panel.height = (int)height - bar_height - 8;
    lumo_fill_rounded_rect(pixels, width, height, &panel, 14, panel_bg);
    lumo_draw_outline(pixels, width, height, &panel, 1, panel_stroke);

    row_y = panel.y + 12;
    lumo_draw_text(pixels, width, height, panel.x + 16, row_y, 2,
        label_color, "QUICK SETTINGS");

    row_y += 24;
    lumo_fill_rect(pixels, width, height, panel.x + 12, row_y,
        panel.width - 24, 1, dim);

    {
        int val_x = panel.x + panel.width / 3;

        row_y += 10;
        lumo_draw_text(pixels, width, height, panel.x + 16, row_y, 2,
            label_color, "WI-FI");
        lumo_draw_wifi_bars(pixels, width, height,
            panel.x + panel.width - 50, row_y - 2, 3, accent, dim);
        lumo_draw_text(pixels, width, height, val_x, row_y, 2,
            value_color, "CONNECTED");

        row_y += 22;
        lumo_draw_text(pixels, width, height, panel.x + 16, row_y, 2,
            label_color, "DISPLAY");
        {
            uint32_t rot = client != NULL ?
                client->compositor_rotation_degrees : 0;
            const char *rot_val = rot == 0 ? "NORMAL" :
                rot == 90 ? "90 DEG" : rot == 180 ? "180 DEG" : "270 DEG";
            lumo_draw_text(pixels, width, height, val_x, row_y, 2,
                value_color, rot_val);
        }

        row_y += 22;
        lumo_draw_text(pixels, width, height, panel.x + 16, row_y, 2,
            label_color, "SESSION");
        lumo_draw_text(pixels, width, height, val_x, row_y, 2,
            value_color, "LUMO " LUMO_VERSION_STRING);

        row_y += 22;
        lumo_draw_text(pixels, width, height, panel.x + 16, row_y, 2,
            label_color, "DEVICE");
        lumo_draw_text(pixels, width, height, val_x, row_y, 2,
            value_color, "ORANGEPI RV2");

        row_y += 28;
        lumo_fill_rect(pixels, width, height, panel.x + 12, row_y,
            panel.width - 24, 1, dim);

        /* volume slider */
        row_y += 12;
        {
            char vol_str[16];
            int track_x = panel.x + 16;
            int track_w = panel.width - 32;
            int track_h = 8;
            uint32_t vol = client != NULL ? client->volume_pct : 50;
            int fill_w = (int)(vol * (uint32_t)track_w / 100);
            struct lumo_rect track_bg = {track_x, row_y + 6,
                track_w, track_h};
            struct lumo_rect track_fill = {track_x, row_y + 6,
                fill_w, track_h};
            struct lumo_rect knob = {track_x + fill_w - 6, row_y + 2,
                12, 16};
            snprintf(vol_str, sizeof(vol_str), "VOL %u%%", vol);
            lumo_draw_text(pixels, width, height, track_x, row_y - 6,
                2, label_color, vol_str);
            lumo_fill_rounded_rect(pixels, width, height, &track_bg,
                4, dim);
            lumo_fill_rounded_rect(pixels, width, height, &track_fill,
                4, accent);
            lumo_fill_rounded_rect(pixels, width, height, &knob,
                6, value_color);
        }

        /* brightness slider */
        row_y += 32;
        {
            char bri_str[16];
            int track_x = panel.x + 16;
            int track_w = panel.width - 32;
            int track_h = 8;
            uint32_t bri = client != NULL ? client->brightness_pct : 50;
            int fill_w = (int)(bri * (uint32_t)track_w / 100);
            struct lumo_rect track_bg = {track_x, row_y + 6,
                track_w, track_h};
            struct lumo_rect track_fill = {track_x, row_y + 6,
                fill_w, track_h};
            struct lumo_rect knob = {track_x + fill_w - 6, row_y + 2,
                12, 16};
            uint32_t bri_color = lumo_argb(0xFF, 0xFF, 0xD1, 0x66);
            snprintf(bri_str, sizeof(bri_str), "BRT %u%%", bri);
            lumo_draw_text(pixels, width, height, track_x, row_y - 6,
                2, label_color, bri_str);
            lumo_fill_rounded_rect(pixels, width, height, &track_bg,
                4, dim);
            lumo_fill_rounded_rect(pixels, width, height, &track_fill,
                4, bri_color);
            lumo_fill_rounded_rect(pixels, width, height, &knob,
                6, value_color);
        }

        row_y += 28;
        lumo_fill_rect(pixels, width, height, panel.x + 12, row_y,
            panel.width - 24, 1, dim);

        row_y += 10;
        {
            int btn_w = (panel.width - 36) / 2;
            struct lumo_rect reload_btn = {
                panel.x + 12, row_y, btn_w, 28
            };
            struct lumo_rect rotate_btn = {
                panel.x + 12 + btn_w + 12, row_y, btn_w, 28
            };
            lumo_fill_rounded_rect(pixels, width, height, &reload_btn,
                8, accent);
            lumo_draw_text_centered(pixels, width, height, &reload_btn, 2,
                value_color, "RELOAD");
            lumo_fill_rounded_rect(pixels, width, height, &rotate_btn,
                8, lumo_argb(0xFF, 0x77, 0x21, 0x6F));
            lumo_draw_text_centered(pixels, width, height, &rotate_btn, 2,
                value_color, "ROTATE");
        }
    }
}

static void lumo_draw_status(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client
) {
    const uint32_t bar_top = lumo_argb(0xE0, 0x2C, 0x00, 0x1E);
    const uint32_t bar_bottom = lumo_argb(0xE0, 0x1D, 0x00, 0x14);
    const uint32_t separator = lumo_argb(0x40, 0x77, 0x21, 0x6F);
    const uint32_t text_color = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
    const uint32_t accent_color = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
    const uint32_t wifi_dim = lumo_argb(0x30, 0xAE, 0xA7, 0x9F);
    int bar_height;
    struct lumo_rect bar_rect;
    struct lumo_rect sep_rect;
    char time_buf[32];
    time_t now;
    struct tm tm_now;
    bar_height = (int)height;

    bar_rect.x = 0;
    bar_rect.y = 0;
    bar_rect.width = (int)width;
    bar_rect.height = bar_height;
    lumo_fill_vertical_gradient(pixels, width, height, &bar_rect,
        bar_top, bar_bottom);

    sep_rect.x = 0;
    sep_rect.y = bar_height - 1;
    sep_rect.width = (int)width;
    sep_rect.height = 1;
    lumo_fill_rect(pixels, width, height, sep_rect.x, sep_rect.y,
        sep_rect.width, sep_rect.height, separator);

    now = time(NULL);
    localtime_r(&now, &tm_now);
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d",
        tm_now.tm_hour, tm_now.tm_min);

    {
        int time_width = lumo_text_width(time_buf, 3);
        int time_x = (int)(width / 2) - time_width / 2;
        int time_y = bar_height / 2 - 10;
        lumo_draw_text(pixels, width, height, time_x, time_y, 3,
            text_color, time_buf);
    }

    lumo_draw_text(pixels, width, height, 14, bar_height / 2 - 7,
        2, accent_color, "LUMO");

    {
        int wifi_bars = 0;
        FILE *wfp = fopen("/proc/net/wireless", "r");
        if (wfp != NULL) {
            char wline[256];
            while (fgets(wline, sizeof(wline), wfp) != NULL) {
                char ifn[32] = {0};
                float quality = 0;
                float signal = 0;
                if (sscanf(wline, " %31[^:]: %*d %f %f",
                        ifn, &quality, &signal) >= 2 &&
                        ifn[0] != '\0' && ifn[0] != '|') {
                    if (signal < 0) {
                        if (signal > -50) wifi_bars = 4;
                        else if (signal > -60) wifi_bars = 3;
                        else if (signal > -70) wifi_bars = 2;
                        else wifi_bars = 1;
                    } else {
                        if (quality > 50) wifi_bars = 4;
                        else if (quality > 35) wifi_bars = 3;
                        else if (quality > 20) wifi_bars = 2;
                        else if (quality > 0) wifi_bars = 1;
                    }
                    break;
                }
            }
            fclose(wfp);
        }
        lumo_draw_wifi_bars(pixels, width, height,
            (int)width - 42, bar_height / 2 - 8, wifi_bars,
            accent_color, wifi_dim);
    }
}

static uint32_t bg_row_cache[2048];
static uint32_t bg_cache_height;
static uint32_t bg_cache_hour = 0xFF;

static int bg_cache_weather_code = -1;

static void lumo_draw_animated_bg(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int weather_code
) {
    struct timespec mono_ts;
    time_t wall_now;
    struct tm tm_now;
    uint32_t frame;
    uint32_t hour;

    clock_gettime(CLOCK_MONOTONIC, &mono_ts);
    frame = (uint32_t)(mono_ts.tv_sec * 5 + mono_ts.tv_nsec / 200000000);

    wall_now = time(NULL);
    localtime_r(&wall_now, &tm_now);
    hour = (uint32_t)tm_now.tm_hour;

    if (hour != bg_cache_hour || height != bg_cache_height ||
            weather_code != bg_cache_weather_code) {
        uint32_t base_r, base_g, base_b;

        /* Multi-palette time-of-day colors:
         * Ubuntu (aubergine/orange), Sailfish (teal/petrol blue),
         * webOS (warm charcoal/slate) blended by time period.
         *
         * Dawn/morning  : Sailfish teal-blue → Ubuntu warm purple
         * Midday        : Ubuntu aubergine core
         * Afternoon     : webOS warm slate-grey
         * Sunset        : Ubuntu orange-red warmth
         * Evening       : Sailfish deep petrol blue
         * Night         : blend of all three — deep dark */
        if (hour >= 5 && hour < 7) {
            /* dawn — Sailfish teal with warm hint */
            base_r = 0x14; base_g = 0x28; base_b = 0x38;
        } else if (hour >= 7 && hour < 10) {
            /* morning — Ubuntu warm purple + Sailfish blue */
            base_r = 0x30; base_g = 0x10; base_b = 0x28;
        } else if (hour >= 10 && hour < 14) {
            /* midday — Ubuntu aubergine core */
            base_r = 0x2C; base_g = 0x00; base_b = 0x1E;
        } else if (hour >= 14 && hour < 17) {
            /* afternoon — webOS warm charcoal */
            base_r = 0x28; base_g = 0x14; base_b = 0x18;
        } else if (hour >= 17 && hour < 19) {
            /* sunset — Ubuntu orange-red warmth */
            base_r = 0x42; base_g = 0x0C; base_b = 0x16;
        } else if (hour >= 19 && hour < 21) {
            /* evening — Sailfish deep petrol */
            base_r = 0x10; base_g = 0x18; base_b = 0x30;
        } else {
            /* night — deep blend of all three */
            base_r = 0x12; base_g = 0x08; base_b = 0x1A;
        }

        /* weather hue shift:
         * 0=clear 1=partly_cloudy 2=cloudy 3=rain 4=storm 5=snow 6=fog */
        switch (weather_code) {
        case 1: /* partly cloudy — Sailfish cool blue push */
            base_g += 0x06; base_b += 0x0A;
            break;
        case 2: /* cloudy — webOS grey-slate overlay */
            base_r = (base_r * 3 + 0x30) / 4;
            base_g = (base_g * 3 + 0x28) / 4;
            base_b = (base_b * 3 + 0x28) / 4;
            break;
        case 3: /* rain — Sailfish deep teal-blue */
            base_r = base_r * 2 / 3;
            base_g += 0x08; base_b += 0x1A;
            break;
        case 4: /* storm — deep purple-indigo */
            base_r = base_r / 2 + 0x08;
            base_g = base_g / 2;
            base_b += 0x24;
            break;
        case 5: /* snow — Sailfish ice blue-white */
            base_r += 0x14; base_g += 0x1A; base_b += 0x24;
            break;
        case 6: /* fog — webOS warm grey wash */
            base_r = (base_r + 0x28) / 2;
            base_g = (base_g + 0x24) / 2;
            base_b = (base_b + 0x22) / 2;
            break;
        default: /* clear — keep time-of-day palette */
            break;
        }
        if (base_r > 0xFF) base_r = 0xFF;
        if (base_g > 0xFF) base_g = 0xFF;
        if (base_b > 0xFF) base_b = 0xFF;

        uint32_t max_h = height < 2048 ? height : 2048;
        for (uint32_t y = 0; y < max_h; y++) {
            uint32_t r = base_r + (y * 0x20) / height;
            uint32_t g = base_g + (y * 0x08) / height;
            uint32_t b = base_b + (y * 0x06) / height;
            if (r > 0xFF) r = 0xFF;
            bg_row_cache[y] = lumo_argb(0xFF, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
        bg_cache_height = height;
        bg_cache_hour = hour;
        bg_cache_weather_code = weather_code;
    }

    for (uint32_t y = 0; y < height; y++) {
        uint32_t row_color = y < 2048 ? bg_row_cache[y] :
            bg_row_cache[2047];
        uint32_t *row_ptr = pixels + y * width;

        uint32_t phase = (y * 3 + frame * 7) % 512;
        uint32_t wave = phase < 256 ? phase : 511 - phase;
        uint32_t glow = (wave * wave) >> 14;

        if (glow > 4) {
            uint8_t cr = (uint8_t)(row_color >> 16);
            uint8_t cg = (uint8_t)(row_color >> 8);
            uint8_t cb = (uint8_t)row_color;
            uint32_t r = cr + (glow * 0x30 >> 8);
            uint32_t g = cg + (glow * 0x14 >> 8);
            uint32_t b = cb + (glow * 0x08 >> 8);
            if (r > 0xFF) r = 0xFF;
            if (g > 0xFF) g = 0xFF;
            if (b > 0xFF) b = 0xFF;
            row_color = lumo_argb(0xFF, (uint8_t)r, (uint8_t)g, (uint8_t)b);
        }

        for (uint32_t x = 0; x < width; x++) {
            row_ptr[x] = row_color;
        }

        if (glow > 12) {
            uint32_t streak_x = (frame * 3 + y * 2) % (width + 200);
            if (streak_x < width) {
                uint8_t sr = (uint8_t)(row_color >> 16);
                uint8_t sg = (uint8_t)(row_color >> 8);
                uint8_t sb = (uint8_t)row_color;
                uint32_t streak_len = 60 + (y % 40);
                uint32_t nr = sr + 0x14 > 0xFF ? 0xFF : sr + 0x14;
                uint32_t ng = sg + 0x0A > 0xFF ? 0xFF : sg + 0x0A;
                uint32_t nb = sb + 0x04 > 0xFF ? 0xFF : sb + 0x04;
                uint32_t streak_color = lumo_argb(0xFF,
                    (uint8_t)nr, (uint8_t)ng, (uint8_t)nb);
                uint32_t end = streak_x + streak_len;
                if (end > width) end = width;
                for (uint32_t sx = streak_x; sx < end; sx++) {
                    row_ptr[sx] = streak_color;
                }
            }
        }
    }
}

static void lumo_render_surface(
    struct lumo_shell_client *client,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_target *active_target
) {
    double visibility;

    if (client == NULL || pixels == NULL) {
        return;
    }

    lumo_clear_pixels(pixels, width, height);
    visibility = client->mode == LUMO_SHELL_MODE_GESTURE
        || client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND ||
            client->mode == LUMO_SHELL_MODE_BACKGROUND
        ? 1.0
        : lumo_shell_client_animation_value(client);

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        lumo_draw_launcher(pixels, width, height, client, active_target, visibility);
        /* draw toast notification overlay (Android-style pill under status bar) */
        if (client->toast_message[0] != '\0' &&
                client->toast_duration_ms > 0) {
            uint64_t now_low = lumo_now_msec() & 0xFFFFFFFF;
            uint64_t elapsed;
            if (now_low >= client->toast_time_low) {
                elapsed = now_low - client->toast_time_low;
            } else {
                elapsed = (0xFFFFFFFF - client->toast_time_low) + now_low;
            }
            if (elapsed < client->toast_duration_ms) {
                uint32_t toast_bg = lumo_argb(0xE0, 0x30, 0x30, 0x34);
                uint32_t toast_text = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
                int tw = lumo_text_width(client->toast_message, 2);
                int pad = 20;
                int th = 28;
                struct lumo_rect toast_rect;
                toast_rect.width = tw + pad * 2;
                toast_rect.height = th;
                toast_rect.x = (int)width - toast_rect.width - 12;
                toast_rect.y = 56;
                /* fade in/out */
                double alpha = 1.0;
                if (elapsed < 200) {
                    alpha = (double)elapsed / 200.0;
                } else if (elapsed > client->toast_duration_ms - 300) {
                    alpha = (double)(client->toast_duration_ms - elapsed) / 300.0;
                }
                if (alpha < 0.0) alpha = 0.0;
                toast_bg = lumo_argb((uint8_t)(0xE0 * alpha),
                    0x30, 0x30, 0x34);
                toast_text = lumo_argb((uint8_t)(0xFF * alpha),
                    0xFF, 0xFF, 0xFF);
                lumo_fill_rounded_rect(pixels, width, height,
                    &toast_rect, 14, toast_bg);
                lumo_draw_text(pixels, width, height,
                    toast_rect.x + pad, toast_rect.y + 8,
                    2, toast_text, client->toast_message);
            }
        }
        return;
    case LUMO_SHELL_MODE_OSK:
        lumo_draw_osk(pixels, width, height, client, active_target, visibility);
        return;
    case LUMO_SHELL_MODE_GESTURE:
        lumo_draw_gesture(pixels, width, height, client, active_target);
        return;
    case LUMO_SHELL_MODE_STATUS:
        lumo_draw_status(pixels, width, height, client);
        return;
    case LUMO_SHELL_MODE_BACKGROUND:
        lumo_draw_animated_bg(pixels, width, height, client->weather_code);
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

static void lumo_shell_buffer_destroy(struct lumo_shell_buffer *buffer) {
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
    free(buffer);
}

static void lumo_shell_buffer_release(
    void *data,
    struct wl_buffer *wl_buffer
) {
    struct lumo_shell_buffer *buffer = data;

    (void)wl_buffer;
    if (buffer != NULL) {
        buffer->busy = false;
    }
}

static bool lumo_shell_surface_config_equal(
    const struct lumo_shell_surface_config *lhs,
    const struct lumo_shell_surface_config *rhs
) {
    if (lhs == NULL || rhs == NULL) {
        return false;
    }

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
    if (client == NULL) {
        return false;
    }

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER: {
        bool has_toast = client->toast_message[0] != '\0' &&
            client->toast_duration_ms > 0;
        if (has_toast) {
            uint64_t now_low = lumo_now_msec() & 0xFFFFFFFF;
            uint64_t elapsed = (now_low >= client->toast_time_low)
                ? now_low - client->toast_time_low
                : (0xFFFFFFFF - client->toast_time_low) + now_low;
            has_toast = elapsed < client->toast_duration_ms;
        }
        return client->compositor_launcher_visible ||
            client->compositor_touch_audit_active ||
            client->compositor_quick_settings_visible ||
            client->compositor_time_panel_visible ||
            has_toast;
    }
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
    const struct lumo_shell_client *client,
    bool visible,
    struct lumo_shell_surface_config *config
) {
    uint32_t output_width = 1280;
    uint32_t output_height = 800;

    if (client == NULL || config == NULL) {
        return false;
    }

    if (!visible && client->mode != LUMO_SHELL_MODE_GESTURE &&
            client->mode != LUMO_SHELL_MODE_STATUS && client->mode != LUMO_SHELL_MODE_BACKGROUND) {
        return lumo_shell_surface_bootstrap_config(client->mode, config);
    }

    if (client->output_width_hint > 0) {
        output_width = client->output_width_hint;
    } else if (client->configured_width > 0) {
        output_width = client->configured_width;
    }

    if (client->output_height_hint > 0) {
        output_height = client->output_height_hint;
    } else if (client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            client->configured_height > 0) {
        output_height = client->configured_height;
    }

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        if (!lumo_shell_surface_bootstrap_config(client->mode, config)) {
            return false;
        }
        config->width = 0;
        config->height = 0;
        config->anchor = LUMO_SHELL_ANCHOR_TOP |
            LUMO_SHELL_ANCHOR_BOTTOM |
            LUMO_SHELL_ANCHOR_LEFT |
            LUMO_SHELL_ANCHOR_RIGHT;
        config->keyboard_interactive = true;
        return true;
    case LUMO_SHELL_MODE_OSK:
        if (!lumo_shell_surface_config_for_mode(client->mode, output_width,
                output_height, config)) {
            return false;
        }
        config->width = 0;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_GESTURE:
        if (!lumo_shell_surface_config_for_mode(client->mode, output_width,
                output_height, config)) {
            return lumo_shell_surface_bootstrap_config(client->mode, config);
        }
        config->width = 0;
        config->background_rgba = 0x00000000;
        return true;
    case LUMO_SHELL_MODE_STATUS:
    case LUMO_SHELL_MODE_BACKGROUND:
        if (!lumo_shell_surface_config_for_mode(client->mode, output_width,
                output_height, config)) {
            return lumo_shell_surface_bootstrap_config(client->mode, config);
        }
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
            client->surface == NULL) {
        return false;
    }

    client->config = *config;
    zwlr_layer_surface_v1_set_size(client->layer_surface,
        config->width, config->height);
    zwlr_layer_surface_v1_set_anchor(client->layer_surface, config->anchor);
    zwlr_layer_surface_v1_set_exclusive_zone(client->layer_surface,
        config->exclusive_zone);
    zwlr_layer_surface_v1_set_margin(client->layer_surface,
        config->margin_top,
        config->margin_right,
        config->margin_bottom,
        config->margin_left);

    keyboard_interactive = config->keyboard_interactive
        ? ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE
        : ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
    zwlr_layer_surface_v1_set_keyboard_interactivity(client->layer_surface,
        keyboard_interactive);
    wl_surface_commit(client->surface);
    return true;
}

static void lumo_shell_client_update_input_region(
    struct lumo_shell_client *client,
    uint32_t width,
    uint32_t height
) {
    struct wl_region *region;
    struct lumo_rect rect = {0};

    if (client == NULL || client->surface == NULL || client->compositor == NULL ||
            width == 0 || height == 0) {
        return;
    }

    region = wl_compositor_create_region(client->compositor);
    if (region == NULL) {
        return;
    }

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        if (!client->compositor_touch_audit_active &&
                client->compositor_launcher_visible &&
                lumo_shell_launcher_panel_rect(width, height, &rect)) {
            wl_region_add(region, rect.x, rect.y, rect.width, rect.height);
        }
        if (!client->compositor_launcher_visible &&
                (client->compositor_quick_settings_visible ||
                    client->compositor_time_panel_visible)) {
            int bar_h = 48;
            int pw = (int)width / 2;
            if (pw < 200) pw = 200;
            if (client->compositor_quick_settings_visible) {
                wl_region_add(region, (int)width - pw - 8, bar_h,
                    pw + 8, (int)height - bar_h);
            }
            if (client->compositor_time_panel_visible) {
                wl_region_add(region, 0, bar_h, pw + 16, 200);
            }
        }
        break;
    case LUMO_SHELL_MODE_OSK:
        wl_region_add(region, 0, 0, (int)width, (int)height);
        break;
    case LUMO_SHELL_MODE_GESTURE:
        if (lumo_shell_gesture_handle_rect(width, height, &rect)) {
            wl_region_add(region, rect.x, rect.y, rect.width, rect.height);
        }
        break;
    case LUMO_SHELL_MODE_STATUS:
        if (client->compositor_quick_settings_visible ||
                client->compositor_time_panel_visible) {
            wl_region_add(region, 0, 0, (int)width, (int)height);
        }
        break;
    case LUMO_SHELL_MODE_BACKGROUND:
        break;
    default:
        break;
    }

    wl_surface_set_input_region(client->surface, region);
    wl_region_destroy(region);
}

static void lumo_shell_client_finish_hide_if_needed(
    struct lumo_shell_client *client
) {
    struct lumo_shell_surface_config hidden_config;

    if (client == NULL || client->mode == LUMO_SHELL_MODE_GESTURE ||
            client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND ||
            client->target_visible || client->surface_hidden) {
        return;
    }

    if (lumo_shell_client_build_config(client, false, &hidden_config) &&
            !lumo_shell_surface_config_equal(&client->config, &hidden_config)) {
        (void)lumo_shell_client_apply_config(client, &hidden_config);
    }
    client->surface_hidden = true;
}

static void lumo_shell_client_begin_transition(
    struct lumo_shell_client *client,
    bool visible
) {
    struct lumo_shell_surface_config config;
    double current_value;

    if (client == NULL) {
        return;
    }

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
                    !lumo_shell_surface_config_equal(&client->config, &config))) {
            (void)lumo_shell_client_apply_config(client, &config);
        }
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
    client->animation_active = client->animation_from != client->animation_to;

    if (!client->animation_active && !visible) {
        lumo_shell_client_finish_hide_if_needed(client);
    }
}

static void lumo_shell_client_sync_surface_state(
    struct lumo_shell_client *client,
    bool force_layout
) {
    struct lumo_shell_surface_config config;
    bool desired_visible;

    if (client == NULL) {
        return;
    }

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

static int lumo_shell_client_animation_timeout(
    const struct lumo_shell_client *client
) {
    uint64_t now;
    uint64_t end_time;

    if (client == NULL || !client->animation_active) {
        if (client != NULL && client->mode == LUMO_SHELL_MODE_BACKGROUND) {
            return 2000;
        }
        if (client != NULL && client->mode == LUMO_SHELL_MODE_STATUS) {
            if (client->compositor_time_panel_visible) {
                return 1000;
            }
            return 30000;
        }
        return -1;
    }

    now = lumo_now_msec();
    end_time = client->animation_started_msec + client->animation_duration_msec;
    if (end_time <= now) {
        return 0;
    }

    if (end_time - now > 24u) {
        return 24;
    }

    return (int)(end_time - now);
}

static void lumo_shell_client_tick_animation(
    struct lumo_shell_client *client
) {
    if (client == NULL || !client->animation_active) {
        return;
    }

    if (lumo_now_msec() >= client->animation_started_msec +
            client->animation_duration_msec) {
        client->animation_active = false;
        if (!client->target_visible) {
            lumo_shell_client_finish_hide_if_needed(client);
        }
    }

    (void)lumo_shell_client_redraw(client);
}

static struct lumo_shell_buffer *lumo_shell_alloc_buffer(
    struct lumo_shell_client *client,
    uint32_t width,
    uint32_t height
) {
    struct lumo_shell_buffer *buffer;
    size_t stride = width * 4u;
    size_t size = stride * height;
    int fd;

    fd = lumo_create_shm_file(size);
    if (fd < 0) {
        return NULL;
    }

    buffer = calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        close(fd);
        return NULL;
    }

    buffer->client = client;
    buffer->fd = fd;
    buffer->size = size;
    buffer->width = width;
    buffer->height = height;
    buffer->busy = false;
    buffer->data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->data == MAP_FAILED) {
        close(fd);
        free(buffer);
        return NULL;
    }

    buffer->pool = wl_shm_create_pool(client->shm, fd, (int)size);
    if (buffer->pool == NULL) {
        munmap(buffer->data, size);
        close(fd);
        free(buffer);
        return NULL;
    }

    buffer->buffer = wl_shm_pool_create_buffer(buffer->pool, 0, (int)width,
        (int)height, (int)(width * 4u), WL_SHM_FORMAT_ARGB8888);
    if (buffer->buffer == NULL) {
        wl_shm_pool_destroy(buffer->pool);
        munmap(buffer->data, size);
        close(fd);
        free(buffer);
        return NULL;
    }

    buffer->release.release = lumo_shell_buffer_release;
    wl_buffer_add_listener(buffer->buffer, &buffer->release, buffer);
    return buffer;
}

static struct lumo_shell_buffer *lumo_shell_get_free_buffer(
    struct lumo_shell_client *client,
    uint32_t width,
    uint32_t height
) {
    for (int i = 0; i < 2; i++) {
        struct lumo_shell_buffer *buf = client->buffers[i];
        if (buf != NULL && !buf->busy &&
                buf->width == width && buf->height == height) {
            return buf;
        }
    }

    for (int i = 0; i < 2; i++) {
        if (client->buffers[i] == NULL || !client->buffers[i]->busy) {
            if (client->buffers[i] != NULL) {
                lumo_shell_buffer_destroy(client->buffers[i]);
                client->buffers[i] = NULL;
            }
            client->buffers[i] = lumo_shell_alloc_buffer(client, width, height);
            if (client->buffers[i] == NULL) {
                return NULL;
            }
            return client->buffers[i];
        }
    }

    /* both busy — force-recycle the non-current buffer so the surface
     * can still render during size transitions (e.g. OSK bootstrap→full) */
    {
        int victim = (client->buffer == client->buffers[0]) ? 1 : 0;
        lumo_shell_buffer_destroy(client->buffers[victim]);
        client->buffers[victim] = NULL;
        client->buffers[victim] = lumo_shell_alloc_buffer(client, width, height);
        if (client->buffers[victim] == NULL) {
            return NULL;
        }
        return client->buffers[victim];
    }
}

static bool lumo_shell_draw_buffer(
    struct lumo_shell_client *client,
    uint32_t width,
    uint32_t height
) {
    struct lumo_shell_buffer *buffer;
    const struct lumo_shell_target *active_target;

    if (client == NULL || client->shm == NULL || client->surface == NULL ||
            width == 0 || height == 0) {
        return false;
    }

    buffer = lumo_shell_get_free_buffer(client, width, height);
    if (buffer == NULL || buffer->data == NULL) {
        return false;
    }

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
        fprintf(stderr, "lumo-shell: note_target %s %s idx=%u at %.0f,%.0f "
            "in %ux%u\n",
            lumo_shell_mode_name(client->mode),
            lumo_shell_target_kind_name(target.kind),
            target.index, x, y,
            client->configured_width, client->configured_height);
        lumo_shell_client_set_active_target(client, &target);
        return;
    }

    fprintf(stderr, "lumo-shell: note_target %s MISS at %.0f,%.0f "
        "in %ux%u\n",
        lumo_shell_mode_name(client->mode), x, y,
        client->configured_width, client->configured_height);
    lumo_shell_client_clear_active_target(client);
}

static bool lumo_shell_client_send_frame(
    struct lumo_shell_client *client,
    const struct lumo_shell_protocol_frame *frame
);

static void lumo_shell_client_send_cycle_rotation(
    struct lumo_shell_client *client
) {
    struct lumo_shell_protocol_frame frame;

    if (client == NULL) {
        return;
    }

    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "cycle_rotation",
            client->next_request_id++)) {
        return;
    }

    (void)lumo_shell_client_send_frame(client, &frame);
    fprintf(stderr, "lumo-shell: sent cycle_rotation request\n");
}

static void lumo_shell_client_send_reload(struct lumo_shell_client *client) {
    struct lumo_shell_protocol_frame frame;

    if (client == NULL) {
        return;
    }

    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "reload_session",
            client->next_request_id++)) {
        return;
    }

    (void)lumo_shell_client_send_frame(client, &frame);
    fprintf(stderr, "lumo-shell: sent reload_session request\n");
}

/* returns: 1=reload, 2=rotate, 3=vol_slider, 4=bri_slider */
static int lumo_shell_status_button_hit(
    const struct lumo_shell_client *client,
    double x,
    double y
) {
    int panel_w, bar_h, panel_x, btn_w;

    if (client == NULL || !client->compositor_quick_settings_visible) {
        return 0;
    }

    panel_w = (int)client->configured_width / 2;
    if (panel_w < 200) panel_w = 200;
    bar_h = 40;
    panel_x = (int)client->configured_width - panel_w - 8;

    /* layout offsets — must match the drawing code above:
     * title(12) + sep(24) + wifi(10+22) + display(22) + session(22) +
     * device(22) + sep(28) + vol_label(12) + vol_track(20) +
     * bri_label(32) + bri_track(28) + sep(28) + buttons(10) */
    int base_y = bar_h + 4;
    int track_x = panel_x + 16;
    int track_w = panel_w - 32;

    /* volume slider zone: row starts at base + 12+24+10+22+22+22+28+12 */
    int vol_y = base_y + 12 + 24 + 10 + 22 + 22 + 22 + 28 + 6;
    if (y >= vol_y && y <= vol_y + 20 &&
            x >= track_x && x <= track_x + track_w) {
        return 3;
    }

    /* brightness slider zone: vol + 32 */
    int bri_y = vol_y + 32;
    if (y >= bri_y && y <= bri_y + 20 &&
            x >= track_x && x <= track_x + track_w) {
        return 4;
    }

    /* buttons: bri + 28 + 28 + 10 */
    int btn_y = bri_y + 28 + 28 + 10;
    int btn_h = 28;
    btn_w = (panel_w - 36) / 2;
    if (y >= btn_y && y <= btn_y + btn_h) {
        if (x >= panel_x + 12 && x <= panel_x + 12 + btn_w) {
            return 1; /* reload */
        }
        if (x >= panel_x + 12 + btn_w + 12 &&
                x <= panel_x + 12 + btn_w + 12 + btn_w) {
            return 2; /* rotate */
        }
    }
    return 0;
}

static uint32_t lumo_shell_slider_pct_from_touch(
    const struct lumo_shell_client *client,
    double x
) {
    int panel_w = (int)client->configured_width / 2;
    if (panel_w < 200) panel_w = 200;
    int panel_x = (int)client->configured_width - panel_w - 8;
    int track_x = panel_x + 16;
    int track_w = panel_w - 32;
    int rel = (int)x - track_x;
    if (rel < 0) rel = 0;
    if (rel > track_w) rel = track_w;
    return (uint32_t)(rel * 100 / track_w);
}

static void lumo_shell_send_set_u32(
    struct lumo_shell_client *client,
    const char *name,
    const char *key,
    uint32_t value
) {
    struct lumo_shell_protocol_frame frame;
    if (!lumo_shell_protocol_frame_init(&frame,
            LUMO_SHELL_PROTOCOL_FRAME_REQUEST, name,
            client->next_request_id++)) {
        return;
    }
    lumo_shell_protocol_frame_add_u32(&frame, key, value);
    (void)lumo_shell_client_send_frame(client, &frame);
}

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
    uint32_t output_size;
    double double_value;
    uint32_t timeout_value;
    bool changed = false;
    bool layout_changed = false;
    const char *value;

    if (client == NULL || frame == NULL) {
        return;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "launcher_visible",
            &bool_value)) {
        if (client->compositor_launcher_visible != bool_value) {
            client->compositor_launcher_visible = bool_value;
            fprintf(stderr, "lumo-shell: launcher visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "keyboard_visible",
            &bool_value)) {
        if (client->compositor_keyboard_visible != bool_value) {
            client->compositor_keyboard_visible = bool_value;
            fprintf(stderr, "lumo-shell: keyboard visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "quick_settings_visible",
            &bool_value)) {
        if (client->compositor_quick_settings_visible != bool_value) {
            client->compositor_quick_settings_visible = bool_value;
            fprintf(stderr, "lumo-shell: quick_settings visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
            if (client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND || client->mode == LUMO_SHELL_MODE_BACKGROUND) {
                layout_changed = true;
            }
        }
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "time_panel_visible",
            &bool_value)) {
        if (client->compositor_time_panel_visible != bool_value) {
            client->compositor_time_panel_visible = bool_value;
            fprintf(stderr, "lumo-shell: time_panel visible=%s\n",
                bool_value ? "true" : "false");
            changed = true;
            if (client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND || client->mode == LUMO_SHELL_MODE_BACKGROUND) {
                layout_changed = true;
            }
        }
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

    if (lumo_shell_protocol_frame_get_bool(frame, "touch_audit_active",
            &bool_value) &&
            client->compositor_touch_audit_active != bool_value) {
        client->compositor_touch_audit_active = bool_value;
        fprintf(stderr, "lumo-shell: touch audit active=%s\n",
            bool_value ? "true" : "false");
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "touch_audit_saved",
            &bool_value) &&
            client->compositor_touch_audit_saved != bool_value) {
        client->compositor_touch_audit_saved = bool_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "touch_audit_step",
            &timeout_value) &&
            client->compositor_touch_audit_step != timeout_value) {
        client->compositor_touch_audit_step = timeout_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "touch_audit_completed_mask",
            &timeout_value) &&
            client->compositor_touch_audit_completed_mask != timeout_value) {
        client->compositor_touch_audit_completed_mask = timeout_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get(frame, "touch_audit_profile", &value) &&
            strcmp(client->compositor_touch_audit_profile, value) != 0) {
        snprintf(client->compositor_touch_audit_profile,
            sizeof(client->compositor_touch_audit_profile), "%s", value);
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_bool(frame, "touch_debug_active",
            &bool_value) &&
            client->touch_debug_active != bool_value) {
        client->touch_debug_active = bool_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "touch_debug_id",
            &timeout_value) &&
            client->touch_debug_id != timeout_value) {
        client->touch_debug_id = timeout_value;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_double(frame, "touch_debug_x",
            &double_value) &&
            client->touch_debug_x != double_value) {
        client->touch_debug_x = double_value;
        client->touch_debug_seen = true;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get_double(frame, "touch_debug_y",
            &double_value) &&
            client->touch_debug_y != double_value) {
        client->touch_debug_y = double_value;
        client->touch_debug_seen = true;
        changed = true;
    }

    if (lumo_shell_protocol_frame_get(frame, "touch_debug_phase", &value)) {
        enum lumo_shell_touch_debug_phase phase =
            LUMO_SHELL_TOUCH_DEBUG_NONE;

        if (lumo_shell_touch_debug_phase_parse(value, &phase) &&
                client->touch_debug_phase != phase) {
            client->touch_debug_phase = phase;
            client->touch_debug_seen = true;
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get(frame, "touch_debug_target", &value)) {
        enum lumo_shell_touch_debug_target target =
            LUMO_SHELL_TOUCH_DEBUG_TARGET_NONE;

        if (lumo_shell_touch_debug_target_parse(value, &target) &&
                client->touch_debug_target != target) {
            client->touch_debug_target = target;
            client->touch_debug_seen = true;
            changed = true;
        }
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "output_width", &output_size) &&
            client->output_width_hint != output_size) {
        client->output_width_hint = output_size;
        layout_changed = true;
        fprintf(stderr, "lumo-shell: output width=%u\n", output_size);
    }

    if (lumo_shell_protocol_frame_get_u32(frame, "output_height", &output_size) &&
            client->output_height_hint != output_size) {
        client->output_height_hint = output_size;
        layout_changed = true;
        fprintf(stderr, "lumo-shell: output height=%u\n", output_size);

        if (client->mode == LUMO_SHELL_MODE_BACKGROUND ||
                client->mode == LUMO_SHELL_MODE_GESTURE ||
                client->mode == LUMO_SHELL_MODE_STATUS) {
            if (client->layer_surface != NULL && client->surface != NULL) {
                wl_surface_commit(client->surface);
            }
        }
    }

    if (lumo_shell_protocol_frame_get(frame, "weather_condition", &value)) {
        if (strcmp(client->weather_condition, value) != 0) {
            strncpy(client->weather_condition, value,
                sizeof(client->weather_condition) - 1);
            changed = true;
        }
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "weather_code",
            &timeout_value)) {
        if (client->weather_code != (int)timeout_value) {
            client->weather_code = (int)timeout_value;
            changed = true;
        }
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "weather_temp",
            &timeout_value)) {
        int temp = (int)timeout_value - 100;
        if (client->weather_temp_c != temp) {
            client->weather_temp_c = temp;
            changed = true;
        }
    }
    if (lumo_shell_protocol_frame_get(frame, "weather_humidity", &value)) {
        strncpy(client->weather_humidity, value,
            sizeof(client->weather_humidity) - 1);
    }
    if (lumo_shell_protocol_frame_get(frame, "weather_wind", &value)) {
        strncpy(client->weather_wind, value,
            sizeof(client->weather_wind) - 1);
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "volume_pct",
            &timeout_value)) {
        client->volume_pct = timeout_value;
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "brightness_pct",
            &timeout_value)) {
        client->brightness_pct = timeout_value;
    }
    if (lumo_shell_protocol_frame_get(frame, "toast_msg", &value)) {
        strncpy(client->toast_message, value,
            sizeof(client->toast_message) - 1);
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "toast_time",
            &timeout_value)) {
        if (client->toast_time_low != timeout_value) {
            client->toast_time_low = timeout_value;
            changed = true; /* trigger redraw for new toast */
        }
    }
    if (lumo_shell_protocol_frame_get_u32(frame, "toast_dur",
            &timeout_value)) {
        client->toast_duration_ms = timeout_value;
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

    if (changed || layout_changed) {
        lumo_shell_client_sync_surface_state(client, layout_changed);
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
    int timeout_ms;

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

        timeout_ms = lumo_shell_client_animation_timeout(client);
        poll_result = poll(fds, nfds, timeout_ms);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (poll_result == 0) {
            if (client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND || client->mode == LUMO_SHELL_MODE_BACKGROUND) {
                (void)lumo_shell_client_redraw(client);
            }
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
            if (wl_display_dispatch(client->display) == -1) {
                break;
            }
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }

        if (client->animation_active) {
            lumo_shell_client_tick_animation(client);
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
    client->pointer_x = wl_fixed_to_double(x);
    client->pointer_y = wl_fixed_to_double(y);

    if (client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            !client->compositor_launcher_visible &&
            (client->compositor_quick_settings_visible ||
                client->compositor_time_panel_visible)) {
        lumo_shell_client_clear_active_target(client);
    } else {
        lumo_shell_client_note_target(client, client->pointer_x,
            client->pointer_y);
    }
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

    {
        int btn = lumo_shell_status_button_hit(client,
            client->pointer_x, client->pointer_y);
        if (btn == 1) {
            lumo_shell_client_send_reload(client);
            return;
        }
        if (btn == 2) {
            lumo_shell_client_send_cycle_rotation(client);
            return;
        }
        if (btn == 3) {
            uint32_t pct = lumo_shell_slider_pct_from_touch(client,
                client->pointer_x);
            lumo_shell_send_set_u32(client, "set_volume", "pct", pct);
            return;
        }
        if (btn == 4) {
            uint32_t pct = lumo_shell_slider_pct_from_touch(client,
                client->pointer_x);
            lumo_shell_send_set_u32(client, "set_brightness", "pct", pct);
            return;
        }
    }

    if (client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            !client->compositor_launcher_visible &&
            (client->compositor_quick_settings_visible ||
                client->compositor_time_panel_visible)) {
        lumo_shell_client_clear_active_target(client);
    } else {
        lumo_shell_client_activate_target(client);
        if (!client->pointer_pressed) {
            lumo_shell_client_clear_active_target(client);
        }
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

    fprintf(stderr, "lumo-shell: %s configure %ux%u\n",
        lumo_shell_mode_name(client->mode), draw_width, draw_height);

    if (client->mode == LUMO_SHELL_MODE_LAUNCHER) {
        client->output_width_hint = draw_width;
        client->output_height_hint = draw_height;
    } else if (client->output_width_hint == 0) {
        client->output_width_hint = draw_width;
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
    if (strcmp(value, "status") == 0) {
        *mode = LUMO_SHELL_MODE_STATUS;
        return true;
    }
    if (strcmp(value, "background") == 0) {
        *mode = LUMO_SHELL_MODE_BACKGROUND;
        return true;
    }

    return false;
}

static bool lumo_shell_create_surface(struct lumo_shell_client *client) {
    if (client == NULL || client->compositor == NULL || client->layer_shell == NULL) {
        return false;
    }

    client->target_visible = client->mode == LUMO_SHELL_MODE_GESTURE ||
        client->mode == LUMO_SHELL_MODE_STATUS || client->mode == LUMO_SHELL_MODE_BACKGROUND ||
        client->mode == LUMO_SHELL_MODE_BACKGROUND;
    client->surface_hidden = client->mode != LUMO_SHELL_MODE_GESTURE &&
        client->mode != LUMO_SHELL_MODE_STATUS &&
        client->mode != LUMO_SHELL_MODE_BACKGROUND;
    client->animation_active = false;
    client->animation_from = client->target_visible ? 1.0 : 0.0;
    client->animation_to = client->animation_from;
    client->animation_duration_msec = 0;

    if (!lumo_shell_surface_bootstrap_config(client->mode,
            &client->config)) {
        return false;
    }

    client->surface = wl_compositor_create_surface(client->compositor);
    if (client->surface == NULL) {
        return false;
    }

    {
        uint32_t layer;
        if (client->mode == LUMO_SHELL_MODE_LAUNCHER ||
                client->mode == LUMO_SHELL_MODE_OSK) {
            layer = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
        } else if (client->mode == LUMO_SHELL_MODE_BACKGROUND) {
            layer = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
        } else {
            layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
        }
        client->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            client->layer_shell,
            client->surface,
            NULL,
            layer,
            client->config.name != NULL ? client->config.name : "lumo-shell"
        );
    }
    if (client->layer_surface == NULL) {
        return false;
    }

    zwlr_layer_surface_v1_add_listener(client->layer_surface,
        &lumo_shell_surface_listener, client);
    return lumo_shell_client_apply_config(client, &client->config);
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

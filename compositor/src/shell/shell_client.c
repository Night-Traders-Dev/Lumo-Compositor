#include "lumo/shell.h"
#include "lumo/shell_protocol.h"

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
};

static bool lumo_shell_client_redraw(struct lumo_shell_client *client);

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

static double lumo_ease_out_cubic(double value) {
    double t = 1.0 - lumo_clamp_unit(value);
    return 1.0 - t * t * t;
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
    progress = lumo_ease_out_cubic(progress);
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
    const uint32_t panel_top = lumo_argb(0xFF, 0x18, 0x20, 0x30);
    const uint32_t panel_bottom = lumo_argb(0xFF, 0x09, 0x0E, 0x17);
    const uint32_t panel_stroke = lumo_argb(0xFF, 0x46, 0x6D, 0x89);
    const uint32_t title_color = lumo_argb(0xFF, 0xF4, 0xF7, 0xFB);
    const uint32_t subtitle_color = lumo_argb(0xFF, 0x8F, 0xA5, 0xBA);
    const uint32_t tile_fill = lumo_argb(0xFF, 0x14, 0x1A, 0x26);
    const uint32_t tile_stroke = lumo_argb(0xFF, 0x2A, 0x38, 0x4B);
    const uint32_t highlight = lumo_argb(0xFF, 0xF3, 0xF6, 0xFD);
    const uint32_t accent_colors[] = {
        lumo_argb(0xFF, 0x40, 0x6B, 0xFF),
        lumo_argb(0xFF, 0x22, 0xD3, 0xEE),
        lumo_argb(0xFF, 0xFF, 0xB8, 0x4D),
        lumo_argb(0xFF, 0xF8, 0x5C, 0x7A),
    };
    struct lumo_rect panel_rect;
    struct lumo_rect accent_rect;
    struct lumo_rect title_badge;
    size_t tile_count = lumo_shell_launcher_tile_count();
    int slide_y;

    if (visibility <= 0.0) {
        return;
    }

    if (client != NULL && client->compositor_touch_audit_active) {
        lumo_draw_touch_audit(pixels, width, height, client);
        return;
    }

    panel_rect.x = (int)lumo_u32_max(width / 24, 24);
    panel_rect.y = (int)(height / 18 + (1.0 - visibility) * (height / 7));
    panel_rect.width = (int)(width - panel_rect.x * 2);
    panel_rect.height = (int)(height - panel_rect.y - (int)lumo_u32_max(height / 28, 18));
    if (panel_rect.width <= 0 || panel_rect.height <= 0) {
        return;
    }

    lumo_fill_vertical_gradient(pixels, width, height, &panel_rect,
        panel_top, panel_bottom);
    lumo_draw_outline(pixels, width, height, &panel_rect, 2, panel_stroke);

    title_badge.x = panel_rect.x + 26;
    title_badge.y = panel_rect.y + 22;
    title_badge.width = lumo_u32_min((uint32_t)(panel_rect.width / 3), 180);
    title_badge.height = 28;
    lumo_fill_rounded_rect(pixels, width, height, &title_badge, 14,
        lumo_argb(0xFF, 0x0F, 0x15, 0x22));
    lumo_draw_text(pixels, width, height, title_badge.x + 14,
        title_badge.y + 8, 2, subtitle_color, "APP DRAWER");
    lumo_draw_text(pixels, width, height, panel_rect.x + 26,
        panel_rect.y + 64, 3, title_color, "LUMO");

    accent_rect.x = panel_rect.x + panel_rect.width - 126;
    accent_rect.y = panel_rect.y + 24;
    accent_rect.width = 92;
    accent_rect.height = 10;
    lumo_fill_rounded_rect(pixels, width, height, &accent_rect, 5,
        lumo_argb(0xFF, 0x57, 0xD2, 0xFF));

    slide_y = (int)((1.0 - visibility) * (height / 8));
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
        lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 12, accent);

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
    const uint32_t shell_top = lumo_argb(0xFF, 0x1E, 0x24, 0x30);
    const uint32_t shell_bottom = lumo_argb(0xFF, 0x10, 0x14, 0x1C);
    const uint32_t shell_stroke = lumo_argb(0xFF, 0x3B, 0x4F, 0x62);
    const uint32_t key_fill = lumo_argb(0xFF, 0xCF, 0xD5, 0xDD);
    const uint32_t special_key_fill = lumo_argb(0xFF, 0x7C, 0xD3, 0xFF);
    const uint32_t label_dark = lumo_argb(0xFF, 0x0C, 0x14, 0x20);
    const uint32_t label_light = lumo_argb(0xFF, 0xF8, 0xFB, 0xFF);
    const uint32_t accent = lumo_argb(0xFF, 0x6C, 0xC9, 0xFF);
    const uint32_t highlight = lumo_argb(0xFF, 0xF4, 0xF7, 0xFC);
    struct lumo_rect panel_rect;
    struct lumo_rect grabber_rect;
    size_t key_count = lumo_shell_osk_key_count();
    int translate_y;

    (void)client;
    if (visibility <= 0.0) {
        return;
    }

    translate_y = (int)((1.0 - visibility) * (height + 12));
    panel_rect.x = (int)lumo_u32_max(width / 48, 12);
    panel_rect.y = translate_y;
    panel_rect.width = (int)(width - panel_rect.x * 2);
    panel_rect.height = (int)(height - lumo_u32_max(height / 36, 8));

    lumo_fill_vertical_gradient(pixels, width, height, &panel_rect,
        shell_top, shell_bottom);
    lumo_draw_outline(pixels, width, height, &panel_rect, 2, shell_stroke);

    grabber_rect.x = (width - 112) / 2;
    grabber_rect.y = panel_rect.y + 14;
    grabber_rect.width = 112;
    grabber_rect.height = 10;
    lumo_fill_rounded_rect(pixels, width, height, &grabber_rect, 5, accent);

    for (uint32_t key_index = 0; key_index < key_count; key_index++) {
        struct lumo_rect key_rect;
        struct lumo_rect label_rect;
        const char *label = lumo_shell_osk_key_label(key_index);
        const char *commit = lumo_shell_osk_key_text(key_index);
        bool active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_OSK_KEY &&
            active_target->index == key_index;
        bool special = label != NULL &&
            (strcmp(label, "SPACE") == 0 || strcmp(label, "RETURN") == 0);
        int scale = 3;

        if (!lumo_shell_osk_key_rect(width, height, key_index, &key_rect)) {
            continue;
        }

        key_rect.y += translate_y;
        lumo_fill_rounded_rect(pixels, width, height, &key_rect, 14,
            special ? special_key_fill : key_fill);
        lumo_draw_outline(pixels, width, height, &key_rect, 2,
            active ? highlight : lumo_argb(0xFF, 0x8B, 0x95, 0xA2));

        label_rect = key_rect;
        if (label != NULL && strlen(label) > 4) {
            scale = 2;
        }
        lumo_draw_text_centered(pixels, width, height, &label_rect, scale,
            special ? label_light : label_dark,
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
    uint32_t accent = lumo_argb(0xFF, 0x69, 0xD1, 0xFF);
    uint32_t base = lumo_argb(0xFF, 0x0E, 0x16, 0x22);
    uint32_t highlight = lumo_argb(0xFF, 0xF3, 0xF6, 0xFD);
    uint32_t debug_color = lumo_argb(0xC0, 0xFF, 0xB8, 0x4D);
    struct lumo_rect handle_rect = {0};
    double local_x = 0.0;
    double local_y = 0.0;

    if (!lumo_shell_gesture_handle_rect(width, height, &handle_rect)) {
        return;
    }

    if (client != NULL &&
            client->compositor_scrim_state == LUMO_SHELL_REMOTE_SCRIM_MODAL) {
        base = lumo_argb(0xFF, 0x09, 0x0F, 0x17);
    } else if (client != NULL &&
            client->compositor_scrim_state == LUMO_SHELL_REMOTE_SCRIM_DIMMED) {
        base = lumo_argb(0xFF, 0x11, 0x18, 0x22);
    }

    lumo_fill_rounded_rect(pixels, width, height, &handle_rect, handle_rect.height / 2,
        base);
    handle_rect.x += 10;
    handle_rect.width -= 20;
    lumo_fill_rounded_rect(pixels, width, height, &handle_rect, handle_rect.height / 2,
        accent);

    if (active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_GESTURE_HANDLE) {
        lumo_draw_outline(pixels, width, height, &handle_rect, 2, highlight);
    }

    if (client == NULL || !client->touch_debug_seen ||
            client->output_width_hint == 0 || client->output_height_hint == 0 ||
            !lumo_shell_surface_local_coords(LUMO_SHELL_MODE_GESTURE,
                client->output_width_hint, client->output_height_hint,
                width, height, client->touch_debug_x, client->touch_debug_y,
                &local_x, &local_y)) {
        return;
    }

    if (client->touch_debug_target == LUMO_SHELL_TOUCH_DEBUG_TARGET_SURFACE) {
        debug_color = lumo_argb(client->touch_debug_active ? 0xD8 : 0xB0,
            0x6C, 0xF0, 0x8D);
    } else if (client->touch_debug_target ==
            LUMO_SHELL_TOUCH_DEBUG_TARGET_HITBOX) {
        debug_color = lumo_argb(client->touch_debug_active ? 0xD8 : 0xB0,
            0xFF, 0xB8, 0x4D);
    } else {
        debug_color = lumo_argb(client->touch_debug_active ? 0xD8 : 0xB0,
            0xFF, 0x6B, 0x6B);
    }

    {
        struct lumo_rect track_rect = {
            .x = (int)local_x - 1,
            .y = 0,
            .width = 3,
            .height = (int)height,
        };
        struct lumo_rect dot_rect = {
            .x = (int)local_x - 12,
            .y = (int)local_y - 12,
            .width = 24,
            .height = 24,
        };
        struct lumo_rect badge_rect = {
            .x = 12,
            .y = 6,
            .width = 96,
            .height = 14,
        };

        lumo_fill_rect(pixels, width, height, track_rect.x, track_rect.y,
            track_rect.width, track_rect.height, debug_color);
        lumo_fill_rounded_rect(pixels, width, height, &dot_rect, 12,
            debug_color);
        lumo_fill_rounded_rect(pixels, width, height, &badge_rect, 7,
            debug_color);
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
        ? 1.0
        : lumo_shell_client_animation_value(client);

    switch (client->mode) {
    case LUMO_SHELL_MODE_LAUNCHER:
        lumo_draw_launcher(pixels, width, height, client, active_target, visibility);
        return;
    case LUMO_SHELL_MODE_OSK:
        lumo_draw_osk(pixels, width, height, client, active_target, visibility);
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
    case LUMO_SHELL_MODE_LAUNCHER:
        return client->compositor_launcher_visible ||
            client->compositor_touch_audit_active;
    case LUMO_SHELL_MODE_OSK:
        return client->compositor_keyboard_visible;
    case LUMO_SHELL_MODE_GESTURE:
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

    if (!visible && client->mode != LUMO_SHELL_MODE_GESTURE) {
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
        break;
    case LUMO_SHELL_MODE_OSK:
        wl_region_add(region, 0, 0, (int)width, (int)height);
        break;
    case LUMO_SHELL_MODE_GESTURE:
        if (lumo_shell_gesture_handle_rect(width, height, &rect)) {
            wl_region_add(region, rect.x, rect.y, rect.width, rect.height);
        }
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

    if (client->mode == LUMO_SHELL_MODE_GESTURE) {
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

    client->animation_from = current_value;
    client->animation_to = visible ? 1.0 : 0.0;
    client->animation_started_msec = lumo_now_msec();
    client->animation_duration_msec = client->mode == LUMO_SHELL_MODE_LAUNCHER
        ? 240
        : 190;
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
            (client->mode == LUMO_SHELL_MODE_GESTURE && client->surface_hidden)) {
        lumo_shell_client_begin_transition(client, desired_visible);
        return;
    }

    if ((desired_visible || client->mode == LUMO_SHELL_MODE_GESTURE) &&
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
        return -1;
    }

    now = lumo_now_msec();
    end_time = client->animation_started_msec + client->animation_duration_msec;
    if (end_time <= now) {
        return 0;
    }

    if (end_time - now > 16u) {
        return 16;
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
        (int)height, (int)stride, WL_SHM_FORMAT_ARGB8888);
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
    lumo_render_surface(client, buffer->data, width, height, active_target);
    lumo_shell_client_update_input_region(client, width, height);
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

    return false;
}

static bool lumo_shell_create_surface(struct lumo_shell_client *client) {
    if (client == NULL || client->compositor == NULL || client->layer_shell == NULL) {
        return false;
    }

    client->target_visible = client->mode == LUMO_SHELL_MODE_GESTURE;
    client->surface_hidden = client->mode != LUMO_SHELL_MODE_GESTURE;
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

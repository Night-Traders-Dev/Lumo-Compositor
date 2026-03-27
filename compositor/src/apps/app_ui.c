#define _DEFAULT_SOURCE
#include "lumo/app.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

static uint32_t lumo_app_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) |
        ((uint32_t)r << 16) |
        ((uint32_t)g << 8) |
        (uint32_t)b;
}

static void lumo_app_fill_rect(
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

    if (pixels == NULL || rect_width <= 0 || rect_height <= 0 ||
            width == 0 || height == 0) {
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

static void lumo_app_fill_gradient(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    uint32_t top_color,
    uint32_t bottom_color
) {
    uint8_t top_a = (uint8_t)(top_color >> 24);
    uint8_t top_r = (uint8_t)(top_color >> 16);
    uint8_t top_g = (uint8_t)(top_color >> 8);
    uint8_t top_b = (uint8_t)top_color;
    uint8_t bottom_a = (uint8_t)(bottom_color >> 24);
    uint8_t bottom_r = (uint8_t)(bottom_color >> 16);
    uint8_t bottom_g = (uint8_t)(bottom_color >> 8);
    uint8_t bottom_b = (uint8_t)bottom_color;

    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return;
    }

    for (int row = 0; row < rect->height; row++) {
        double t = rect->height > 1
            ? (double)row / (double)(rect->height - 1)
            : 0.0;
        uint32_t color = lumo_app_argb(
            (uint8_t)(top_a + (int)((bottom_a - top_a) * t)),
            (uint8_t)(top_r + (int)((bottom_r - top_r) * t)),
            (uint8_t)(top_g + (int)((bottom_g - top_g) * t)),
            (uint8_t)(top_b + (int)((bottom_b - top_b) * t))
        );
        lumo_app_fill_rect(pixels, width, height, rect->x, rect->y + row,
            rect->width, 1, color);
    }
}

static bool lumo_app_rounded_rect_contains(
    const struct lumo_rect *rect,
    int radius,
    int x,
    int y
) {
    int local_x;
    int local_y;
    int dx;
    int dy;
    int max_x;
    int max_y;

    if (rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return false;
    }

    local_x = x - rect->x;
    local_y = y - rect->y;
    if (local_x < 0 || local_y < 0 ||
            local_x >= rect->width || local_y >= rect->height) {
        return false;
    }

    if (radius <= 0) {
        return true;
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

static void lumo_app_fill_rounded_rect(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    uint32_t radius,
    uint32_t color
) {
    int x0;
    int y0;
    int x1;
    int y1;

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
            if (lumo_app_rounded_rect_contains(rect, (int)radius, col, row)) {
                pixels[row * (int)width + col] = color;
            }
        }
    }
}

static void lumo_app_draw_outline(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    int thickness,
    uint32_t color
) {
    lumo_app_fill_rect(pixels, width, height, rect->x, rect->y,
        rect->width, thickness, color);
    lumo_app_fill_rect(pixels, width, height, rect->x,
        rect->y + rect->height - thickness, rect->width, thickness, color);
    lumo_app_fill_rect(pixels, width, height, rect->x, rect->y,
        thickness, rect->height, color);
    lumo_app_fill_rect(pixels, width, height,
        rect->x + rect->width - thickness, rect->y,
        thickness, rect->height, color);
}

static bool lumo_app_glyph_rows(char ch, uint8_t rows[7]) {
    switch ((unsigned char)toupper((unsigned char)ch)) {
    case 'A': memcpy(rows, (uint8_t[]){0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, 7); return true;
    case 'B': memcpy(rows, (uint8_t[]){0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, 7); return true;
    case 'C': memcpy(rows, (uint8_t[]){0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, 7); return true;
    case 'D': memcpy(rows, (uint8_t[]){0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, 7); return true;
    case 'E': memcpy(rows, (uint8_t[]){0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, 7); return true;
    case 'F': memcpy(rows, (uint8_t[]){0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, 7); return true;
    case 'G': memcpy(rows, (uint8_t[]){0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, 7); return true;
    case 'H': memcpy(rows, (uint8_t[]){0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, 7); return true;
    case 'I': memcpy(rows, (uint8_t[]){0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, 7); return true;
    case 'K': memcpy(rows, (uint8_t[]){0x11,0x12,0x14,0x18,0x14,0x12,0x11}, 7); return true;
    case 'L': memcpy(rows, (uint8_t[]){0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, 7); return true;
    case 'M': memcpy(rows, (uint8_t[]){0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, 7); return true;
    case 'N': memcpy(rows, (uint8_t[]){0x11,0x19,0x15,0x13,0x11,0x11,0x11}, 7); return true;
    case 'O': memcpy(rows, (uint8_t[]){0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, 7); return true;
    case 'P': memcpy(rows, (uint8_t[]){0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, 7); return true;
    case 'R': memcpy(rows, (uint8_t[]){0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, 7); return true;
    case 'S': memcpy(rows, (uint8_t[]){0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, 7); return true;
    case 'T': memcpy(rows, (uint8_t[]){0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, 7); return true;
    case 'U': memcpy(rows, (uint8_t[]){0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, 7); return true;
    case 'V': memcpy(rows, (uint8_t[]){0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, 7); return true;
    case 'W': memcpy(rows, (uint8_t[]){0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, 7); return true;
    case 'Y': memcpy(rows, (uint8_t[]){0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, 7); return true;
    case '0': memcpy(rows, (uint8_t[]){0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, 7); return true;
    case '1': memcpy(rows, (uint8_t[]){0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, 7); return true;
    case '2': memcpy(rows, (uint8_t[]){0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, 7); return true;
    case '3': memcpy(rows, (uint8_t[]){0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, 7); return true;
    case '4': memcpy(rows, (uint8_t[]){0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, 7); return true;
    case '5': memcpy(rows, (uint8_t[]){0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}, 7); return true;
    case '6': memcpy(rows, (uint8_t[]){0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, 7); return true;
    case '7': memcpy(rows, (uint8_t[]){0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, 7); return true;
    case '8': memcpy(rows, (uint8_t[]){0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, 7); return true;
    case '9': memcpy(rows, (uint8_t[]){0x0E,0x11,0x11,0x0F,0x01,0x02,0x1C}, 7); return true;
    case ':': memcpy(rows, (uint8_t[]){0x00,0x04,0x04,0x00,0x04,0x04,0x00}, 7); return true;
    case '-': memcpy(rows, (uint8_t[]){0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, 7); return true;
    case '/': memcpy(rows, (uint8_t[]){0x01,0x02,0x04,0x08,0x10,0x00,0x00}, 7); return true;
    case ' ': memset(rows, 0, 7); return true;
    default: memset(rows, 0, 7); return false;
    }
}

static void lumo_app_draw_text(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int x,
    int y,
    int scale,
    uint32_t color,
    const char *text
) {
    int cursor_x = x;
    uint8_t rows[7];

    if (pixels == NULL || text == NULL || scale <= 0) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        lumo_app_glyph_rows(text[i], rows);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if ((rows[row] & (1u << (4 - col))) == 0) {
                    continue;
                }
                lumo_app_fill_rect(pixels, width, height,
                    cursor_x + col * scale, y + row * scale,
                    scale, scale, color);
            }
        }
        cursor_x += scale * 6;
    }
}

static void lumo_app_draw_text_centered(
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

    if (rect == NULL || text == NULL) {
        return;
    }

    text_width = (int)strlen(text) * scale * 6 - scale;
    text_height = scale * 7;
    lumo_app_draw_text(pixels, width, height,
        rect->x + (rect->width - text_width) / 2,
        rect->y + (rect->height - text_height) / 2,
        scale, color, text);
}

static void lumo_app_card_text(
    enum lumo_app_id app_id,
    uint32_t card_index,
    const char **title,
    const char **body
) {
    static const char *const phone_titles[] = {"Favorites", "Recents", "Dialer"};
    static const char *const phone_bodies[] = {"Mira, Kai, and Dev",
        "Last call 2m ago", "Tap to open keypad"};
    static const char *const messages_titles[] = {"Unread", "Pinned", "Compose"};
    static const char *const messages_bodies[] = {"2 new threads", "Family and team",
        "Start a new message"};
    static const char *const browser_titles[] = {"Quick Start", "Tabs", "Reading"};
    static const char *const browser_bodies[] = {"Open your saved starts", "3 tabs waiting",
        "Continue later list"};
    static const char *const camera_titles[] = {"Photo", "Video", "Gallery"};
    static const char *const camera_bodies[] = {"Ready for quick capture", "Last mode used",
        "Recent shots saved"};
    static const char *const maps_titles[] = {"Home", "Nearby", "Route"};
    static const char *const maps_bodies[] = {"18 min away", "Coffee and fuel", "Fastest arrival"};
    static const char *const music_titles[] = {"Now Playing", "Library", "Mixes"};
    static const char *const music_bodies[] = {"Resume favorite album", "Recent artists", "Fresh queue"};
    static const char *const photos_titles[] = {"Memories", "Albums", "Shared"};
    static const char *const photos_bodies[] = {"Highlights for today", "Trips and camera roll", "Latest updates"};
    static const char *const videos_titles[] = {"Continue", "Downloads", "Queue"};
    static const char *const videos_bodies[] = {"Resume in one tap", "Offline ready", "Watch later list"};
    static const char *const clock_titles[] = {"Local Time", "Alarm", "Timer"};
    static const char *const clock_bodies[] = {"Updated live", "06:30 tomorrow", "Quick 15 minutes"};
    static const char *const notes_titles[] = {"Pinned", "Today", "Checklist"};
    static const char *const notes_bodies[] = {"Launch tasks", "Ideas and scribbles", "Things to ship"};
    static const char *const files_titles[] = {"Recent", "Downloads", "Storage"};
    static const char *const files_bodies[] = {"Latest files nearby", "Ready to open", "OrangePi and SD card"};
    static const char *const settings_titles[] = {"Display", "Sound", "Privacy"};
    static const char *const settings_bodies[] = {"Brightness and rotation", "Speakers and mic", "Permissions and lock"};

    if (title == NULL || body == NULL || card_index > 2) {
        return;
    }

    switch (app_id) {
    case LUMO_APP_PHONE:
        *title = phone_titles[card_index];
        *body = phone_bodies[card_index];
        return;
    case LUMO_APP_MESSAGES:
        *title = messages_titles[card_index];
        *body = messages_bodies[card_index];
        return;
    case LUMO_APP_BROWSER:
        *title = browser_titles[card_index];
        *body = browser_bodies[card_index];
        return;
    case LUMO_APP_CAMERA:
        *title = camera_titles[card_index];
        *body = camera_bodies[card_index];
        return;
    case LUMO_APP_MAPS:
        *title = maps_titles[card_index];
        *body = maps_bodies[card_index];
        return;
    case LUMO_APP_MUSIC:
        *title = music_titles[card_index];
        *body = music_bodies[card_index];
        return;
    case LUMO_APP_PHOTOS:
        *title = photos_titles[card_index];
        *body = photos_bodies[card_index];
        return;
    case LUMO_APP_VIDEOS:
        *title = videos_titles[card_index];
        *body = videos_bodies[card_index];
        return;
    case LUMO_APP_CLOCK:
        *title = clock_titles[card_index];
        *body = clock_bodies[card_index];
        return;
    case LUMO_APP_NOTES:
        *title = notes_titles[card_index];
        *body = notes_bodies[card_index];
        return;
    case LUMO_APP_FILES:
        *title = files_titles[card_index];
        *body = files_bodies[card_index];
        return;
    case LUMO_APP_SETTINGS:
    default:
        *title = settings_titles[card_index];
        *body = settings_bodies[card_index];
        return;
    }
}

static const int lumo_files_row_height = 44;
static const int lumo_files_header_height = 148;

int lumo_app_files_entry_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
) {
    int row_y = lumo_files_header_height;
    int max_rows = ((int)height - row_y - 60) / lumo_files_row_height;

    (void)width;
    if (x < 28.0 || x > (double)width - 28.0) {
        return -1;
    }
    if (y < (double)row_y) {
        return -1;
    }

    int index = (int)(y - row_y) / lumo_files_row_height;
    if (index < 0 || index >= max_rows) {
        return -1;
    }

    return index;
}

int lumo_app_clock_card_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
) {
    int card1_y = (int)height / 2 + 20;
    int card2_y = (int)height / 2 + 130;

    (void)width;
    if (x < 28.0 || x > (double)width - 28.0) {
        return -1;
    }
    if (y >= card1_y && y < card1_y + 90) {
        return 1;
    }
    if (y >= card2_y && y < card2_y + 90) {
        return 2;
    }
    return -1;
}

int lumo_app_settings_row_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
) {
    int row0_y = 120;

    (void)width;
    (void)height;
    if (x < 28.0 || x > (double)width - 28.0) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        int ry = row0_y + i * 86;
        if (y >= ry && y < ry + 70) {
            return i;
        }
    }
    return -1;
}

int lumo_app_notes_row_at(
    uint32_t width,
    uint32_t height,
    double x,
    double y
) {
    int header_y = 130;
    int row_h = 44;
    int max_rows = ((int)height - header_y - 80) / row_h;

    (void)width;
    if (x < 28.0 || x > (double)width - 28.0) {
        return -1;
    }
    if (y >= (double)((int)height - 60) && y < (double)height - 20) {
        return -2;
    }
    if (y < header_y) {
        return -1;
    }
    int idx = (int)(y - header_y) / row_h;
    if (idx < 0 || idx >= max_rows) {
        return -1;
    }
    return idx;
}

static void lumo_app_render_clock(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    bool close_active = ctx != NULL ? ctx->close_active : false;
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    struct lumo_rect close_rect = {0};
    uint32_t accent = lumo_app_accent_argb(LUMO_APP_CLOCK);
    uint32_t bg_top = lumo_app_argb(0xFF, 0x06, 0x0B, 0x12);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x0E, 0x16, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xF2, 0xF6, 0xFB);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0x95, 0xA6, 0xB9);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x12, 0x1A, 0x27);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x2B, 0x3D, 0x52);
    time_t now = time(NULL);
    struct tm tm_now = {0};
    char time_buf[16] = {0};
    char date_buf[32] = {0};
    char alarm_buf[32] = "06:30 TOMORROW";
    int cx = (int)width / 2;

    localtime_r(&now, &tm_now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_now);
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_now);

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    {
        int tw = (int)strlen(time_buf) * 8 * 6 - 8;
        lumo_app_draw_text(pixels, width, height, cx - tw / 2,
            (int)height / 4 - 28, 8, accent, time_buf);
    }
    {
        int dw = (int)strlen(date_buf) * 3 * 6 - 3;
        lumo_app_draw_text(pixels, width, height, cx - dw / 2,
            (int)height / 4 + 40, 3, text_secondary, date_buf);
    }

    {
        struct lumo_rect alarm_card = {
            .x = 28, .y = (int)height / 2 + 20,
            .width = (int)width - 56, .height = 90
        };
        lumo_app_fill_rounded_rect(pixels, width, height, &alarm_card, 18,
            panel_fill);
        lumo_app_draw_outline(pixels, width, height, &alarm_card, 2,
            panel_stroke);
        lumo_app_draw_text(pixels, width, height, alarm_card.x + 20,
            alarm_card.y + 16, 2, text_secondary, "ALARM");
        lumo_app_draw_text(pixels, width, height, alarm_card.x + 20,
            alarm_card.y + 46, 3, text_primary, alarm_buf);
        lumo_app_draw_text(pixels, width, height,
            alarm_card.x + alarm_card.width - 80,
            alarm_card.y + 50, 2, text_secondary, "06:30");
    }
    {
        struct lumo_rect timer_card = {
            .x = 28, .y = (int)height / 2 + 130,
            .width = (int)width - 56, .height = 90
        };
        lumo_app_fill_rounded_rect(pixels, width, height, &timer_card, 18,
            panel_fill);
        lumo_app_draw_outline(pixels, width, height, &timer_card, 2,
            panel_stroke);
        {
            uint64_t sw_ms = ctx != NULL ? ctx->stopwatch_elapsed_ms : 0;
            bool sw_run = ctx != NULL && ctx->stopwatch_running;
            uint32_t sw_secs = (uint32_t)(sw_ms / 1000);
            uint32_t sw_mins = sw_secs / 60;
            uint32_t sw_hrs = sw_mins / 60;
            char sw_buf[16];
            snprintf(sw_buf, sizeof(sw_buf), "%02u:%02u:%02u",
                sw_hrs, sw_mins % 60, sw_secs % 60);
            lumo_app_draw_text(pixels, width, height, timer_card.x + 20,
                timer_card.y + 16, 2, text_secondary,
                sw_run ? "STOPWATCH  RUNNING" : "STOPWATCH  TAP TO START");
            lumo_app_draw_text(pixels, width, height, timer_card.x + 20,
                timer_card.y + 46, 3, sw_run ? accent : text_primary, sw_buf);
            lumo_app_draw_text(pixels, width, height,
                timer_card.x + timer_card.width - 100,
                timer_card.y + 50, 2, text_secondary, "TAP:RESET");
        }
    }

    if (lumo_app_close_rect(width, height, &close_rect)) {
        lumo_app_fill_rounded_rect(pixels, width, height, &close_rect, 18,
            lumo_app_argb(0xFF, 0x16, 0x20, 0x2E));
        lumo_app_draw_outline(pixels, width, height, &close_rect, 2,
            close_active ? text_primary : panel_stroke);
        lumo_app_draw_text_centered(pixels, width, height, &close_rect, 2,
            text_primary, "CLOSE");
    }
}

static void lumo_app_render_files(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    bool close_active = ctx != NULL ? ctx->close_active : false;
    const char *browse_path_override = ctx != NULL ? ctx->browse_path : NULL;
    int scroll_off = ctx != NULL ? ctx->scroll_offset : 0;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    struct lumo_rect close_rect = {0};
    uint32_t accent = lumo_app_accent_argb(LUMO_APP_FILES);
    uint32_t bg_top = lumo_app_argb(0xFF, 0x06, 0x0B, 0x12);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x0E, 0x16, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xF2, 0xF6, 0xFB);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0x95, 0xA6, 0xB9);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x12, 0x1A, 0x27);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x2B, 0x3D, 0x52);
    uint32_t folder_color = lumo_app_argb(0xFF, 0xFF, 0xD1, 0x66);
    uint32_t file_color = lumo_app_argb(0xFF, 0x7B, 0xA3, 0xFF);
    int row_y;
    DIR *dir;
    const char *browse_path;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    lumo_app_draw_text(pixels, width, height, 28, 28, 2,
        text_secondary, "FILE MANAGER");
    lumo_app_draw_text(pixels, width, height, 28, 60, 4, text_primary,
        "Files");

    if (browse_path_override != NULL && browse_path_override[0] != '\0') {
        browse_path = browse_path_override;
    } else {
        browse_path = getenv("HOME");
        if (browse_path == NULL) {
            browse_path = "/home";
        }
    }

    lumo_app_draw_text(pixels, width, height, 28, 108, 2,
        accent, browse_path);

    {
        struct lumo_rect sep = {28, 130, (int)width - 56, 1};
        lumo_app_fill_rect(pixels, width, height, sep.x, sep.y,
            sep.width, sep.height, panel_stroke);
    }

    row_y = 148;
    dir = opendir(browse_path);
    if (dir != NULL) {
        struct dirent *entry;
        int count = 0;
        int skipped = 0;
        int total_visible = 0;
        int max_rows = ((int)height - row_y - 60) / 44;

        if (max_rows < 1) {
            max_rows = 1;
        }
        while ((entry = readdir(dir)) != NULL) {
            bool is_dir;
            struct lumo_rect row_rect;

            if (entry->d_name[0] == '.') {
                continue;
            }
            if (skipped < scroll_off) {
                skipped++;
                total_visible++;
                continue;
            }
            if (count >= max_rows) {
                total_visible++;
                continue;
            }
            is_dir = entry->d_type == DT_DIR;

            row_rect.x = 28;
            row_rect.y = row_y;
            row_rect.width = (int)width - 56;
            row_rect.height = 40;

            if (selected == total_visible) {
                lumo_app_fill_rounded_rect(pixels, width, height, &row_rect,
                    10, lumo_app_argb(0xFF, 0x3B, 0x1F, 0x34));
                lumo_app_draw_outline(pixels, width, height, &row_rect, 1,
                    lumo_app_argb(0xFF, 0xE9, 0x54, 0x20));
            } else {
                lumo_app_fill_rounded_rect(pixels, width, height, &row_rect,
                    10, panel_fill);
            }

            {
                struct lumo_rect icon = {
                    row_rect.x + 10, row_rect.y + 10, 20, 20
                };
                lumo_app_fill_rounded_rect(pixels, width, height, &icon,
                    4, is_dir ? folder_color : file_color);
            }

            lumo_app_draw_text(pixels, width, height, row_rect.x + 42,
                row_rect.y + 12, 2, text_primary, entry->d_name);
            lumo_app_draw_text(pixels, width, height,
                row_rect.x + row_rect.width - 60, row_rect.y + 12, 2,
                text_secondary, is_dir ? "DIR" : "FILE");

            row_y += 44;
            count++;
            total_visible++;
        }
        closedir(dir);

        if (total_visible > max_rows) {
            char scroll_buf[32];
            snprintf(scroll_buf, sizeof(scroll_buf), "%d-%d / %d",
                scroll_off + 1, scroll_off + count, total_visible);
            lumo_app_draw_text(pixels, width, height,
                (int)width - 160, (int)height - 40, 2,
                text_secondary, scroll_buf);
        }

        if (count == 0) {
            lumo_app_draw_text(pixels, width, height, 28, row_y, 2,
                text_secondary, "EMPTY DIRECTORY");
        }
    } else {
        lumo_app_draw_text(pixels, width, height, 28, row_y, 2,
            text_secondary, "CANNOT OPEN DIRECTORY");
    }

    {
        struct statvfs st;
        if (statvfs("/", &st) == 0) {
            char storage_buf[64];
            unsigned long free_mb = (unsigned long)(st.f_bavail *
                (st.f_frsize / 1024)) / 1024;
            unsigned long total_mb = (unsigned long)(st.f_blocks *
                (st.f_frsize / 1024)) / 1024;
            snprintf(storage_buf, sizeof(storage_buf),
                "%lu / %lu MB FREE", free_mb, total_mb);
            lumo_app_draw_text(pixels, width, height, 28,
                (int)height - 40, 2, text_secondary, storage_buf);
        }
    }

    if (lumo_app_close_rect(width, height, &close_rect)) {
        lumo_app_fill_rounded_rect(pixels, width, height, &close_rect, 18,
            lumo_app_argb(0xFF, 0x16, 0x20, 0x2E));
        lumo_app_draw_outline(pixels, width, height, &close_rect, 2,
            close_active ? text_primary : panel_stroke);
        lumo_app_draw_text_centered(pixels, width, height, &close_rect, 2,
            text_primary, "CLOSE");
    }
}

static void lumo_app_render_settings(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    bool close_active = ctx != NULL ? ctx->close_active : false;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    struct lumo_rect close_rect = {0};
    uint32_t accent = lumo_app_accent_argb(LUMO_APP_SETTINGS);
    uint32_t bg_top = lumo_app_argb(0xFF, 0x06, 0x0B, 0x12);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x0E, 0x16, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xF2, 0xF6, 0xFB);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0x95, 0xA6, 0xB9);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x12, 0x1A, 0x27);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x2B, 0x3D, 0x52);
    int row_y;
    char hostname_buf[64] = "orangepi";
    char kernel_buf[128] = "unknown";

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    lumo_app_draw_text(pixels, width, height, 28, 28, 2,
        text_secondary, "SYSTEM SETTINGS");
    lumo_app_draw_text(pixels, width, height, 28, 60, 4, text_primary,
        "Settings");

    gethostname(hostname_buf, sizeof(hostname_buf) - 1);
    {
        FILE *fp = fopen("/proc/version", "r");
        if (fp != NULL) {
            if (fgets(kernel_buf, sizeof(kernel_buf), fp) != NULL) {
                char *space = strchr(kernel_buf, ' ');
                if (space != NULL) {
                    space = strchr(space + 1, ' ');
                }
                if (space != NULL) {
                    char *end = strchr(space + 1, ' ');
                    if (end != NULL) {
                        *end = '\0';
                    }
                }
                if (space != NULL && space[0] != '\0') {
                    memmove(kernel_buf, space + 1,
                        strlen(space + 1) + 1);
                }
            }
            fclose(fp);
        }
    }

    row_y = 120;

    {
        struct lumo_rect card = {28, row_y, (int)width - 56, 70};
        int val_x = card.x + card.width / 3;
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 14,
            panel_fill);
        lumo_app_draw_outline(pixels, width, height, &card, 1,
            panel_stroke);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 14, 2, text_secondary, "HOSTNAME");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 14, 2, text_primary, hostname_buf);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 42, 2, text_secondary, "KERNEL");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 42, 2, text_primary, kernel_buf);
    }
    row_y += 86;

    {
        struct lumo_rect card = {28, row_y, (int)width - 56, 70};
        int val_x = card.x + card.width / 3;
        char uptime_buf[64] = "unknown";
        char mem_buf[64] = "unknown";
        FILE *fp;

        fp = fopen("/proc/uptime", "r");
        if (fp != NULL) {
            double up_secs = 0.0;
            if (fscanf(fp, "%lf", &up_secs) == 1) {
                int hours = (int)(up_secs / 3600);
                int mins = (int)((up_secs - hours * 3600) / 60);
                snprintf(uptime_buf, sizeof(uptime_buf),
                    "%dH %dM", hours, mins);
            }
            fclose(fp);
        }

        fp = fopen("/proc/meminfo", "r");
        if (fp != NULL) {
            unsigned long total = 0;
            unsigned long avail = 0;
            char line[128];
            while (fgets(line, sizeof(line), fp) != NULL) {
                if (sscanf(line, "MemTotal: %lu", &total) == 1) {
                    continue;
                }
                sscanf(line, "MemAvailable: %lu", &avail);
            }
            fclose(fp);
            if (total > 0) {
                snprintf(mem_buf, sizeof(mem_buf), "%lu / %lu MB",
                    avail / 1024, total / 1024);
            }
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &card, 14,
            panel_fill);
        lumo_app_draw_outline(pixels, width, height, &card, 1,
            panel_stroke);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 14, 2, text_secondary, "UPTIME");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 14, 2, text_primary, uptime_buf);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 42, 2, text_secondary, "MEMORY");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 42, 2, text_primary, mem_buf);
    }
    row_y += 86;

    {
        struct lumo_rect card = {28, row_y, (int)width - 56, 70};
        int val_x = card.x + card.width / 3;
        char wifi_buf[64] = "NOT CONNECTED";

        {
            FILE *wfp = fopen("/proc/net/wireless", "r");
            if (wfp != NULL) {
                char wline[256];
                while (fgets(wline, sizeof(wline), wfp) != NULL) {
                    char ifname[32] = {0};
                    int status = 0;
                    if (sscanf(wline, " %31[^:]: %d", ifname, &status) >= 1 &&
                            ifname[0] != '\0' && ifname[0] != '|') {
                        snprintf(wifi_buf, sizeof(wifi_buf), "%s UP", ifname);
                        break;
                    }
                }
                fclose(wfp);
            }
            if (strcmp(wifi_buf, "NOT CONNECTED") == 0) {
                FILE *ofp = fopen("/sys/class/net/wlan0/operstate", "r");
                if (ofp != NULL) {
                    char state[32] = {0};
                    if (fgets(state, sizeof(state), ofp) != NULL) {
                        char *nl = strchr(state, '\n');
                        if (nl) *nl = '\0';
                        if (strcmp(state, "up") == 0) {
                            snprintf(wifi_buf, sizeof(wifi_buf),
                                "WLAN0 CONNECTED");
                        }
                    }
                    fclose(ofp);
                }
            }
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &card, 14,
            panel_fill);
        lumo_app_draw_outline(pixels, width, height, &card, 1,
            panel_stroke);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 14, 2, text_secondary, "WI-FI");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 14, 2, text_primary, wifi_buf);
        lumo_app_draw_text(pixels, width, height, card.x + 20,
            card.y + 42, 2, text_secondary, "COMPOSITOR");
        lumo_app_draw_text(pixels, width, height, val_x,
            card.y + 42, 2, accent, "LUMO 0.0.48");
    }

    if (lumo_app_close_rect(width, height, &close_rect)) {
        lumo_app_fill_rounded_rect(pixels, width, height, &close_rect, 18,
            lumo_app_argb(0xFF, 0x16, 0x20, 0x2E));
        lumo_app_draw_outline(pixels, width, height, &close_rect, 2,
            close_active ? text_primary : panel_stroke);
        lumo_app_draw_text_centered(pixels, width, height, &close_rect, 2,
            text_primary, "CLOSE");
    }
}

static void lumo_app_render_notes(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    struct lumo_rect close_rect = {0};
    bool close_active = ctx != NULL ? ctx->close_active : false;
    int selected = ctx != NULL ? ctx->selected_row : -1;
    int note_count = ctx != NULL ? ctx->note_count : 0;
    uint32_t bg_top = lumo_app_argb(0xFF, 0x06, 0x0B, 0x12);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x0E, 0x16, 0x22);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);
    uint32_t accent = lumo_app_argb(0xFF, 0xE9, 0x54, 0x20);
    int row_y;

    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    lumo_app_draw_text(pixels, width, height, 28, 28, 2,
        text_secondary, "NOTES");
    lumo_app_draw_text(pixels, width, height, 28, 60, 4, text_primary,
        "Notes");
    lumo_app_draw_text(pixels, width, height, 28, 108, 2, text_secondary,
        "TAP A NOTE TO SELECT  /  TAP + TO ADD");

    row_y = 130;
    for (int i = 0; i < note_count && i < 8; i++) {
        struct lumo_rect row = {28, row_y, (int)width - 56, 40};
        bool is_sel = (selected == i);
        const char *text = (ctx != NULL && ctx->notes[i][0] != '\0')
            ? ctx->notes[i] : "EMPTY NOTE";

        lumo_app_fill_rounded_rect(pixels, width, height, &row, 10,
            is_sel ? lumo_app_argb(0xFF, 0x3B, 0x1F, 0x34) : panel_fill);
        if (is_sel) {
            lumo_app_draw_outline(pixels, width, height, &row, 1, accent);
        }
        lumo_app_draw_text(pixels, width, height, row.x + 16, row.y + 12,
            2, is_sel ? text_primary : text_secondary, text);
        row_y += 44;
    }

    if (note_count == 0) {
        lumo_app_draw_text(pixels, width, height, 28, row_y + 20, 2,
            text_secondary, "NO NOTES YET");
    }

    {
        struct lumo_rect add_btn = {28, (int)height - 60, (int)width - 56, 36};
        lumo_app_fill_rounded_rect(pixels, width, height, &add_btn, 12,
            accent);
        lumo_app_draw_text_centered(pixels, width, height, &add_btn, 2,
            text_primary, "+ ADD NOTE");
    }

    if (lumo_app_close_rect(width, height, &close_rect)) {
        lumo_app_fill_rounded_rect(pixels, width, height, &close_rect, 18,
            lumo_app_argb(0xFF, 0x2C, 0x16, 0x28));
        lumo_app_draw_outline(pixels, width, height, &close_rect, 2,
            close_active ? text_primary : panel_stroke);
        lumo_app_draw_text_centered(pixels, width, height, &close_rect, 2,
            text_primary, "CLOSE");
    }
}

void lumo_app_render(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    enum lumo_app_id app_id;
    bool close_active;

    if (ctx == NULL) {
        return;
    }

    app_id = ctx->app_id;
    close_active = ctx->close_active;

    if (app_id == LUMO_APP_CLOCK) {
        lumo_app_render_clock(ctx, pixels, width, height);
        return;
    }
    if (app_id == LUMO_APP_FILES) {
        lumo_app_render_files(ctx, pixels, width, height);
        return;
    }
    if (app_id == LUMO_APP_SETTINGS) {
        lumo_app_render_settings(ctx, pixels, width, height);
        return;
    }
    if (app_id == LUMO_APP_NOTES) {
        lumo_app_render_notes(ctx, pixels, width, height);
        return;
    }
    {
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    struct lumo_rect hero;
    struct lumo_rect close_rect = {0};
    struct lumo_rect label_rect = {0};
    struct lumo_rect badge_rect = {0};
    uint32_t accent = lumo_app_accent_argb(app_id);
    uint32_t bg_top = lumo_app_argb(0xFF, 0x06, 0x0B, 0x12);
    uint32_t bg_bottom = lumo_app_argb(0xFF, 0x0E, 0x16, 0x22);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x12, 0x1A, 0x27);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x2B, 0x3D, 0x52);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xF2, 0xF6, 0xFB);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0x95, 0xA6, 0xB9);
    char clock_buf[16] = {0};

    if (pixels == NULL || width == 0 || height == 0) {
        return;
    }

    memset(pixels, 0, (size_t)width * (size_t)height * sizeof(uint32_t));
    lumo_app_fill_gradient(pixels, width, height, &full, bg_top, bg_bottom);

    badge_rect.x = 28;
    badge_rect.y = 28;
    badge_rect.width = 180;
    badge_rect.height = 28;
    lumo_app_fill_rounded_rect(pixels, width, height, &badge_rect, 14,
        lumo_app_argb(0xFF, 0x0D, 0x14, 0x20));
    lumo_app_draw_text(pixels, width, height, badge_rect.x + 14,
        badge_rect.y + 8, 2, text_secondary, "LUMO NATIVE");

    lumo_app_draw_text(pixels, width, height, 28, 86, 4, text_primary,
        lumo_app_title(app_id));
    lumo_app_draw_text(pixels, width, height, 28, 128, 2, text_secondary,
        lumo_app_subtitle(app_id));

    hero.x = 28;
    hero.y = 170;
    hero.width = (int)width - 56;
    hero.height = (int)(height / 4);
    lumo_app_fill_rounded_rect(pixels, width, height, &hero, 28, panel_fill);
    lumo_app_draw_outline(pixels, width, height, &hero, 2, panel_stroke);

    label_rect.x = hero.x + 24;
    label_rect.y = hero.y + 24;
    label_rect.width = hero.width - 48;
    label_rect.height = 24;
    lumo_app_draw_text(pixels, width, height, label_rect.x, label_rect.y, 2,
        text_secondary, "NOW READY");

    lumo_app_fill_rect(pixels, width, height, hero.x + 24, hero.y + 62,
        hero.width - 48, 8, accent);
    lumo_app_draw_text(pixels, width, height, hero.x + 24, hero.y + 88, 3,
        text_primary, lumo_app_title(app_id));

    if (app_id == LUMO_APP_CLOCK) {
        time_t now = time(NULL);
        struct tm tm_now = {0};

        localtime_r(&now, &tm_now);
        if (strftime(clock_buf, sizeof(clock_buf), "%H:%M", &tm_now) > 0) {
            lumo_app_draw_text(pixels, width, height, hero.x + 24, hero.y + 126,
                4, accent, clock_buf);
        }
    } else {
        lumo_app_draw_text(pixels, width, height, hero.x + 24, hero.y + 130, 2,
            text_secondary, "Touch-first native client");
    }

    for (uint32_t card_index = 0; card_index < 3; card_index++) {
        struct lumo_rect card = {
            .x = 28 + (int)card_index * ((int)width - 84) / 3,
            .y = hero.y + hero.height + 24,
            .width = ((int)width - 112) / 3,
            .height = (int)height - (hero.y + hero.height + 56),
        };
        const char *card_title = NULL;
        const char *card_body = NULL;

        if (card.width <= 0 || card.height <= 0) {
            continue;
        }

        lumo_app_fill_rounded_rect(pixels, width, height, &card, 22,
            lumo_app_argb(0xFF, 0x10, 0x17, 0x22));
        lumo_app_draw_outline(pixels, width, height, &card, 2,
            lumo_app_argb(0xFF, 0x23, 0x33, 0x47));
        lumo_app_fill_rect(pixels, width, height, card.x + 18, card.y + 18,
            card.width - 36, 6, accent);

        lumo_app_card_text(app_id, card_index, &card_title, &card_body);
        lumo_app_draw_text(pixels, width, height, card.x + 18, card.y + 36, 2,
            text_secondary, card_title != NULL ? card_title : "PANEL");
        lumo_app_draw_text(pixels, width, height, card.x + 18, card.y + 68, 2,
            text_primary, card_body != NULL ? card_body : "Ready");
    }

    if (lumo_app_close_rect(width, height, &close_rect)) {
        lumo_app_fill_rounded_rect(pixels, width, height, &close_rect, 18,
            lumo_app_argb(0xFF, 0x16, 0x20, 0x2E));
        lumo_app_draw_outline(pixels, width, height, &close_rect, 2,
            close_active ? text_primary : panel_stroke);
        lumo_app_draw_text_centered(pixels, width, height, &close_rect, 2,
            text_primary, "CLOSE");
    }
    } /* end fallback block */
}

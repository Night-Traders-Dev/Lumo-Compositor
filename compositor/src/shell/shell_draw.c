/*
 * shell_draw.c — graphics primitives for the Lumo shell client.
 *
 * Extracted from shell_client.c so that drawing helpers live in their
 * own translation unit while staying accessible to all shell_client
 * modules via shell_client_internal.h.
 */

#include "shell_client_internal.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

uint32_t lumo_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) |
        ((uint32_t)r << 16) |
        ((uint32_t)g << 8) |
        (uint32_t)b;
}

uint32_t lumo_u32_min(uint32_t lhs, uint32_t rhs) {
    return lhs < rhs ? lhs : rhs;
}

double lumo_clamp_unit(double value) {
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
double lumo_ease_decelerate(double value) {
    double t = 1.0 - lumo_clamp_unit(value);
    return 1.0 - t * t * t;
}

/* Material Design standard curve: cubic-bezier(0.4, 0.0, 0.2, 1.0)
 * Smooth ease-in-out for general transitions */
double lumo_ease_standard(double value) {
    double t = lumo_clamp_unit(value);
    if (t < 0.5) {
        return 4.0 * t * t * t;
    }
    double u = -2.0 * t + 2.0;
    return 1.0 - u * u * u / 2.0;
}

uint64_t lumo_now_msec(void) {
    struct timespec now = {0};

    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

void lumo_clear_pixels(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height
) {
    if (pixels == NULL) {
        return;
    }

    memset(pixels, 0, (size_t)width * (size_t)height * sizeof(uint32_t));
}

void lumo_fill_rect(
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

void lumo_fill_vertical_gradient(
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

bool lumo_rounded_rect_contains(
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

void lumo_fill_rounded_rect(
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

void lumo_draw_outline(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_rect *rect,
    int thickness,
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

    switch ((unsigned char)ch) {
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
    case 'a':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F}, 7);
        return true;
    case 'b':
        memcpy(rows, (uint8_t[]){0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E}, 7);
        return true;
    case 'c':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x0E, 0x10, 0x10, 0x10, 0x0E}, 7);
        return true;
    case 'd':
        memcpy(rows, (uint8_t[]){0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F}, 7);
        return true;
    case 'e':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E}, 7);
        return true;
    case 'f':
        memcpy(rows, (uint8_t[]){0x06, 0x08, 0x08, 0x1E, 0x08, 0x08, 0x08}, 7);
        return true;
    case 'g':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E}, 7);
        return true;
    case 'h':
        memcpy(rows, (uint8_t[]){0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x11}, 7);
        return true;
    case 'i':
        memcpy(rows, (uint8_t[]){0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E}, 7);
        return true;
    case 'j':
        memcpy(rows, (uint8_t[]){0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C}, 7);
        return true;
    case 'k':
        memcpy(rows, (uint8_t[]){0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}, 7);
        return true;
    case 'l':
        memcpy(rows, (uint8_t[]){0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, 7);
        return true;
    case 'm':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11}, 7);
        return true;
    case 'n':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x1E, 0x11, 0x11, 0x11, 0x11}, 7);
        return true;
    case 'o':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E}, 7);
        return true;
    case 'p':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10}, 7);
        return true;
    case 'q':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01}, 7);
        return true;
    case 'r':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}, 7);
        return true;
    case 's':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E}, 7);
        return true;
    case 't':
        memcpy(rows, (uint8_t[]){0x08, 0x08, 0x1E, 0x08, 0x08, 0x08, 0x06}, 7);
        return true;
    case 'u':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0E}, 7);
        return true;
    case 'v':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04}, 7);
        return true;
    case 'w':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A}, 7);
        return true;
    case 'x':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11}, 7);
        return true;
    case 'y':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E}, 7);
        return true;
    case 'z':
        memcpy(rows, (uint8_t[]){0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F}, 7);
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
    case '<':
        memcpy(rows, (uint8_t[]){0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}, 7);
        return true;
    case '>':
        memcpy(rows, (uint8_t[]){0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}, 7);
        return true;
    case '^':
        memcpy(rows, (uint8_t[]){0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}, 7);
        return true;
    case ' ':
        memcpy(rows, blank, 7);
        return true;
    default:
        memcpy(rows, blank, 7);
        return false;
    }
}

int lumo_text_width(const char *text, int scale) {
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

void lumo_draw_text(
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

void lumo_draw_text_centered(
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

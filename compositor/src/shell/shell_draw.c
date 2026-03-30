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

#if defined(__riscv) && defined(__riscv_v_intrinsic)
#include <riscv_vector.h>
#define LUMO_HAS_RVV 1
#endif

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

/* Fill a horizontal span of pixels with a single color.
 * This is the innermost hot loop for all rect/rounded/gradient fills. */
void lumo_fill_span(uint32_t *row_ptr, int count, uint32_t color) {
    if (count <= 0) {
        return;
    }

    /* For short spans, scalar fill is fastest (avoids setup overhead). */
    if (count <= 8) {
        for (int i = 0; i < count; i++) {
            row_ptr[i] = color;
        }
        return;
    }

    /* Use memset for black (all zero bytes) */
    if (color == 0) {
        memset(row_ptr, 0, (size_t)count * sizeof(uint32_t));
        return;
    }

    /* Check if all 4 bytes are the same — then memset works */
    {
        uint8_t b0 = (uint8_t)color;
        if (b0 == (uint8_t)(color >> 8) && b0 == (uint8_t)(color >> 16) &&
                b0 == (uint8_t)(color >> 24)) {
            memset(row_ptr, b0, (size_t)count * sizeof(uint32_t));
            return;
        }
    }

    /* Unrolled 8-wide scalar fill — benchmarks show this is fastest
     * on the SpacemiT X1 (beats both RVV intrinsics and memcpy
     * doubling due to low loop overhead and good store coalescing). */
    {
        int i = 0;
        int bulk = count - (count & 7);
        for (; i < bulk; i += 8) {
            row_ptr[i]     = color;
            row_ptr[i + 1] = color;
            row_ptr[i + 2] = color;
            row_ptr[i + 3] = color;
            row_ptr[i + 4] = color;
            row_ptr[i + 5] = color;
            row_ptr[i + 6] = color;
            row_ptr[i + 7] = color;
        }
        for (; i < count; i++) {
            row_ptr[i] = color;
        }
    }
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

    {
        int span = x1 - x0;
        for (int row = y0; row < y1; row++) {
            lumo_fill_span(pixels + row * (int)width + x0, span, color);
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
    int x0, y0, x1, y1, span, denom;
    int32_t a, r, g, b, da, dr, dg, db;

    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return;
    }

    x0 = rect->x < 0 ? 0 : rect->x;
    y0 = rect->y < 0 ? 0 : rect->y;
    x1 = rect->x + rect->width;
    y1 = rect->y + rect->height;
    if (x0 >= (int)width || y0 >= (int)height) return;
    if (x1 > (int)width) x1 = (int)width;
    if (y1 > (int)height) y1 = (int)height;
    span = x1 - x0;
    if (span <= 0) return;

    /* 16.16 fixed-point interpolation — no floating point per row */
    denom = rect->height > 1 ? rect->height - 1 : 1;
    a = (int32_t)(top_color >> 24) << 16;
    r = (int32_t)((top_color >> 16) & 0xFF) << 16;
    g = (int32_t)((top_color >> 8) & 0xFF) << 16;
    b = (int32_t)(top_color & 0xFF) << 16;
    da = (((int32_t)(bottom_color >> 24) - (int32_t)(top_color >> 24)) << 16) / denom;
    dr = (((int32_t)((bottom_color >> 16) & 0xFF) - (int32_t)((top_color >> 16) & 0xFF)) << 16) / denom;
    dg = (((int32_t)((bottom_color >> 8) & 0xFF) - (int32_t)((top_color >> 8) & 0xFF)) << 16) / denom;
    db = (((int32_t)(bottom_color & 0xFF) - (int32_t)(top_color & 0xFF)) << 16) / denom;

    /* advance to first visible row */
    {
        int skip = y0 - rect->y;
        a += da * skip;
        r += dr * skip;
        g += dg * skip;
        b += db * skip;
    }

    for (int row = y0; row < y1; row++) {
        uint32_t color = ((uint32_t)(a >> 16) << 24) |
            ((uint32_t)(r >> 16) << 16) |
            ((uint32_t)(g >> 16) << 8) |
            (uint32_t)(b >> 16);
        lumo_fill_span(pixels + row * (int)width + x0, span, color);
        a += da;
        r += dr;
        g += dg;
        b += db;
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
    int r2;

    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0) {
        return;
    }

    if (radius == 0) {
        lumo_fill_rect(pixels, width, height,
            rect->x, rect->y, rect->width, rect->height, color);
        return;
    }

    x0 = rect->x < 0 ? 0 : rect->x;
    y0 = rect->y < 0 ? 0 : rect->y;
    x1 = rect->x + rect->width;
    y1 = rect->y + rect->height;
    if (x1 > (int)width) x1 = (int)width;
    if (y1 > (int)height) y1 = (int)height;

    r2 = (int)(radius * radius);

    for (int row = y0; row < y1; row++) {
        int local_y = row - rect->y;
        int row_x0 = x0;
        int row_x1 = x1;

        /* Only compute corner insets for rows in the corner bands.
         * Use sqrt-free integer approach: find the horizontal offset
         * where dy^2 + dx^2 = r^2 → dx = sqrt(r^2 - dy^2). */
        if (local_y < (int)radius) {
            int dy = (int)radius - local_y;
            int dy2 = dy * dy;
            int dx2 = r2 - dy2;
            /* integer sqrt via bit scan */
            int inset = (int)radius;
            if (dx2 > 0) {
                int s = 0;
                while ((s + 1) * (s + 1) <= dx2) s++;
                inset = (int)radius - s;
            }
            if (rect->x + inset > row_x0) row_x0 = rect->x + inset;
            if (rect->x + rect->width - inset < row_x1)
                row_x1 = rect->x + rect->width - inset;
        } else if (local_y >= rect->height - (int)radius) {
            int dy = local_y - (rect->height - (int)radius - 1);
            int dy2 = dy * dy;
            int dx2 = r2 - dy2;
            int inset = (int)radius;
            if (dx2 > 0) {
                int s = 0;
                while ((s + 1) * (s + 1) <= dx2) s++;
                inset = (int)radius - s;
            }
            if (rect->x + inset > row_x0) row_x0 = rect->x + inset;
            if (rect->x + rect->width - inset < row_x1)
                row_x1 = rect->x + rect->width - inset;
        }

        if (row_x1 > row_x0) {
            lumo_fill_span(pixels + row * (int)width + row_x0,
                row_x1 - row_x0, color);
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
    case '!':
        memcpy(rows, (uint8_t[]){0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}, 7);
        return true;
    case '@':
        memcpy(rows, (uint8_t[]){0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}, 7);
        return true;
    case '#':
        memcpy(rows, (uint8_t[]){0x0A, 0x1F, 0x0A, 0x0A, 0x1F, 0x0A, 0x00}, 7);
        return true;
    case '$':
        memcpy(rows, (uint8_t[]){0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}, 7);
        return true;
    case '%':
        memcpy(rows, (uint8_t[]){0x11, 0x12, 0x04, 0x04, 0x04, 0x09, 0x11}, 7);
        return true;
    case '&':
        memcpy(rows, (uint8_t[]){0x0C, 0x12, 0x0C, 0x12, 0x11, 0x11, 0x0E}, 7);
        return true;
    case '+':
        memcpy(rows, (uint8_t[]){0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, 7);
        return true;
    case '(':
        memcpy(rows, (uint8_t[]){0x02, 0x04, 0x04, 0x04, 0x04, 0x04, 0x02}, 7);
        return true;
    case ')':
        memcpy(rows, (uint8_t[]){0x08, 0x04, 0x04, 0x04, 0x04, 0x04, 0x08}, 7);
        return true;
    case ';':
        memcpy(rows, (uint8_t[]){0x00, 0x04, 0x04, 0x00, 0x00, 0x04, 0x08}, 7);
        return true;
    case '\'':
        memcpy(rows, (uint8_t[]){0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}, 7);
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
    uint8_t glyph[7];
    int cursor_x = x;

    if (pixels == NULL || text == NULL || scale <= 0) {
        return;
    }

    for (size_t i = 0; text[i] != '\0'; i++) {
        lumo_glyph_rows(text[i], glyph);
        for (int grow = 0; grow < 7; grow++) {
            uint8_t bits = glyph[grow];
            if (bits == 0) continue;

            int py = y + grow * scale;
            if (py >= (int)height) break;
            if (py + scale <= 0) continue;

            /* scan bits left-to-right and merge adjacent set bits
             * into a single span to reduce fill calls */
            int col = 0;
            while (col < 5) {
                if ((bits & (1u << (4 - col))) == 0) {
                    col++;
                    continue;
                }
                int span_start = col;
                while (col < 5 && (bits & (1u << (4 - col))) != 0) {
                    col++;
                }
                /* span from span_start to col (exclusive) */
                int px = cursor_x + span_start * scale;
                int pw = (col - span_start) * scale;
                for (int sy = 0; sy < scale; sy++) {
                    int ry = py + sy;
                    if (ry < 0 || ry >= (int)height) continue;
                    int cx0 = px < 0 ? 0 : px;
                    int cx1 = px + pw > (int)width ? (int)width : px + pw;
                    if (cx1 > cx0) {
                        lumo_fill_span(pixels + ry * (int)width + cx0,
                            cx1 - cx0, color);
                    }
                }
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

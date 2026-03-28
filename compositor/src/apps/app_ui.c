#define _DEFAULT_SOURCE
#include "lumo/app_render.h"

#include <ctype.h>
#include <string.h>
#include <time.h>

uint32_t lumo_app_argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) |
        ((uint32_t)g << 8) | (uint32_t)b;
}

void lumo_app_fill_rect(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int rect_width, int rect_height, uint32_t color
) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + rect_width;
    int y1 = y + rect_height;

    if (pixels == NULL || rect_width <= 0 || rect_height <= 0 ||
            width == 0 || height == 0) return;
    if (x1 > (int)width) x1 = (int)width;
    if (y1 > (int)height) y1 = (int)height;

    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++)
            pixels[row * (int)width + col] = color;
}

void lumo_app_fill_gradient(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, uint32_t top_color, uint32_t bottom_color
) {
    uint8_t ta = (uint8_t)(top_color >> 24), tr = (uint8_t)(top_color >> 16),
        tg = (uint8_t)(top_color >> 8), tb = (uint8_t)top_color;
    uint8_t ba = (uint8_t)(bottom_color >> 24), br = (uint8_t)(bottom_color >> 16),
        bg = (uint8_t)(bottom_color >> 8), bb = (uint8_t)bottom_color;

    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0)
        return;

    for (int row = 0; row < rect->height; row++) {
        double t = rect->height > 1 ? (double)row / (double)(rect->height - 1) : 0.0;
        uint32_t c = lumo_app_argb(
            (uint8_t)(ta + (int)((ba - ta) * t)),
            (uint8_t)(tr + (int)((br - tr) * t)),
            (uint8_t)(tg + (int)((bg - tg) * t)),
            (uint8_t)(tb + (int)((bb - tb) * t)));
        lumo_app_fill_rect(pixels, width, height, rect->x, rect->y + row,
            rect->width, 1, c);
    }
}

static bool lumo_app_rounded_rect_contains(
    const struct lumo_rect *rect, int radius, int x, int y
) {
    int lx, ly, dx, dy, mx, my;
    if (rect == NULL || rect->width <= 0 || rect->height <= 0) return false;
    lx = x - rect->x; ly = y - rect->y;
    if (lx < 0 || ly < 0 || lx >= rect->width || ly >= rect->height) return false;
    if (radius <= 0) return true;
    if ((lx >= radius && lx < rect->width - radius) ||
            (ly >= radius && ly < rect->height - radius)) return true;
    mx = rect->width - radius - 1; my = rect->height - radius - 1;
    dx = lx < radius ? lx - radius : lx - mx;
    dy = ly < radius ? ly - radius : ly - my;
    return dx * dx + dy * dy <= radius * radius;
}

void lumo_app_fill_rounded_rect(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, uint32_t radius, uint32_t color
) {
    if (pixels == NULL || rect == NULL || rect->width <= 0 || rect->height <= 0)
        return;
    int x0 = rect->x < 0 ? 0 : rect->x;
    int y0 = rect->y < 0 ? 0 : rect->y;
    int x1 = rect->x + rect->width; if (x1 > (int)width) x1 = (int)width;
    int y1 = rect->y + rect->height; if (y1 > (int)height) y1 = (int)height;
    for (int row = y0; row < y1; row++) {
        int local_y = row - rect->y;
        bool in_corner = local_y < (int)radius ||
            local_y >= rect->height - (int)radius;
        if (!in_corner) {
            for (int col = x0; col < x1; col++)
                pixels[row * (int)width + col] = color;
        } else {
            for (int col = x0; col < x1; col++)
                if (lumo_app_rounded_rect_contains(rect, (int)radius, col, row))
                    pixels[row * (int)width + col] = color;
        }
    }
}

void lumo_app_draw_outline(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, int thickness, uint32_t color
) {
    lumo_app_fill_rect(pixels, width, height, rect->x, rect->y,
        rect->width, thickness, color);
    lumo_app_fill_rect(pixels, width, height, rect->x,
        rect->y + rect->height - thickness, rect->width, thickness, color);
    lumo_app_fill_rect(pixels, width, height, rect->x, rect->y,
        thickness, rect->height, color);
    lumo_app_fill_rect(pixels, width, height,
        rect->x + rect->width - thickness, rect->y, thickness, rect->height, color);
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
    case 'J': memcpy(rows, (uint8_t[]){0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, 7); return true;
    case 'K': memcpy(rows, (uint8_t[]){0x11,0x12,0x14,0x18,0x14,0x12,0x11}, 7); return true;
    case 'L': memcpy(rows, (uint8_t[]){0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, 7); return true;
    case 'M': memcpy(rows, (uint8_t[]){0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, 7); return true;
    case 'N': memcpy(rows, (uint8_t[]){0x11,0x19,0x15,0x13,0x11,0x11,0x11}, 7); return true;
    case 'O': memcpy(rows, (uint8_t[]){0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, 7); return true;
    case 'P': memcpy(rows, (uint8_t[]){0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, 7); return true;
    case 'Q': memcpy(rows, (uint8_t[]){0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, 7); return true;
    case 'R': memcpy(rows, (uint8_t[]){0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, 7); return true;
    case 'S': memcpy(rows, (uint8_t[]){0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, 7); return true;
    case 'T': memcpy(rows, (uint8_t[]){0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, 7); return true;
    case 'U': memcpy(rows, (uint8_t[]){0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, 7); return true;
    case 'V': memcpy(rows, (uint8_t[]){0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, 7); return true;
    case 'W': memcpy(rows, (uint8_t[]){0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, 7); return true;
    case 'X': memcpy(rows, (uint8_t[]){0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, 7); return true;
    case 'Y': memcpy(rows, (uint8_t[]){0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, 7); return true;
    case 'Z': memcpy(rows, (uint8_t[]){0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, 7); return true;
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
    case '+': memcpy(rows, (uint8_t[]){0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, 7); return true;
    case ' ': memset(rows, 0, 7); return true;
    case '.': memcpy(rows, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x04}, 7); return true;
    case '!': memcpy(rows, (uint8_t[]){0x04,0x04,0x04,0x04,0x04,0x00,0x04}, 7); return true;
    case '(': memcpy(rows, (uint8_t[]){0x02,0x04,0x04,0x04,0x04,0x04,0x02}, 7); return true;
    case ')': memcpy(rows, (uint8_t[]){0x08,0x04,0x04,0x04,0x04,0x04,0x08}, 7); return true;
    case '%': memcpy(rows, (uint8_t[]){0x11,0x12,0x04,0x04,0x04,0x09,0x11}, 7); return true;
    case '@': memcpy(rows, (uint8_t[]){0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, 7); return true;
    case '#': memcpy(rows, (uint8_t[]){0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, 7); return true;
    case '_': memcpy(rows, (uint8_t[]){0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, 7); return true;
    case '>': memcpy(rows, (uint8_t[]){0x08,0x04,0x02,0x01,0x02,0x04,0x08}, 7); return true;
    case '<': memcpy(rows, (uint8_t[]){0x02,0x04,0x08,0x10,0x08,0x04,0x02}, 7); return true;
    case '=': memcpy(rows, (uint8_t[]){0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, 7); return true;
    case '*': memcpy(rows, (uint8_t[]){0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}, 7); return true;
    case '~': memcpy(rows, (uint8_t[]){0x00,0x00,0x08,0x15,0x02,0x00,0x00}, 7); return true;
    case '$': memcpy(rows, (uint8_t[]){0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, 7); return true;
    case '&': memcpy(rows, (uint8_t[]){0x0C,0x12,0x0C,0x12,0x11,0x11,0x0E}, 7); return true;
    case '[': memcpy(rows, (uint8_t[]){0x06,0x04,0x04,0x04,0x04,0x04,0x06}, 7); return true;
    case ']': memcpy(rows, (uint8_t[]){0x0C,0x04,0x04,0x04,0x04,0x04,0x0C}, 7); return true;
    default: memset(rows, 0, 7); return false;
    }
}

void lumo_app_draw_text(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int scale, uint32_t color, const char *text
) {
    int cursor_x = x;
    uint8_t rows[7];
    if (pixels == NULL || text == NULL || scale <= 0) return;
    for (size_t i = 0; text[i] != '\0'; i++) {
        lumo_app_glyph_rows(text[i], rows);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if ((rows[row] & (1u << (4 - col))) != 0)
                    lumo_app_fill_rect(pixels, width, height,
                        cursor_x + col * scale, y + row * scale,
                        scale, scale, color);
        cursor_x += scale * 6;
    }
}

void lumo_app_draw_text_centered(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_rect *rect, int scale, uint32_t color, const char *text
) {
    if (rect == NULL || text == NULL) return;
    int tw = (int)strlen(text) * scale * 6 - scale;
    int th = scale * 7;
    lumo_app_draw_text(pixels, width, height,
        rect->x + (rect->width - tw) / 2,
        rect->y + (rect->height - th) / 2, scale, color, text);
}

void lumo_app_draw_close_button(
    uint32_t *pixels, uint32_t width, uint32_t height, bool close_active
) {
    struct lumo_rect close_rect = {0};
    uint32_t fg = close_active
        ? lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF)
        : lumo_app_argb(0xA0, 0xAE, 0xA7, 0x9F);
    uint32_t bg = close_active
        ? lumo_app_argb(0xFF, 0xE9, 0x54, 0x20)
        : lumo_app_argb(0x60, 0x2C, 0x00, 0x1E);

    if (lumo_app_close_rect(width, height, &close_rect)) {
        int cx = close_rect.x + close_rect.width / 2;
        int cy = close_rect.y + close_rect.height / 2;
        int r = close_rect.width < close_rect.height
            ? close_rect.width / 2 : close_rect.height / 2;
        struct lumo_rect circle = {cx - r, cy - r, r * 2, r * 2};

        lumo_app_fill_rounded_rect(pixels, width, height, &circle,
            (uint32_t)r, bg);

        /* NOTE: lumo_app_draw_close_button is currently dead code — all app
         * render paths removed their close-button call sites and navigation
         * was moved to a bottom-edge swipe gesture.  The x1+1 / x2+1 pixel
         * writes below are checked within (int)width bounds, but the
         * adjacent +1 pixel is only guarded by the original x1/x2 check and
         * not by a separate x1+1 < (int)width guard.  This is harmless while
         * the function is unused, but should be fixed if the close button is
         * ever re-enabled. */
        for (int i = -r / 3; i <= r / 3; i++) {
            int x1 = cx - r / 3 + i;
            int y1 = cy - r / 3 + i;
            int x2 = cx + r / 3 - i;
            int y2 = cy - r / 3 + i;
            if (x1 >= 0 && x1 < (int)width && y1 >= 0 && y1 < (int)height)
                pixels[y1 * (int)width + x1] = fg;
            if (x2 >= 0 && x2 < (int)width && y2 >= 0 && y2 < (int)height)
                pixels[y2 * (int)width + x2] = fg;
            if (x1+1 >= 0 && x1+1 < (int)width && y1 >= 0 && y1 < (int)height)
                pixels[y1 * (int)width + x1 + 1] = fg;
            if (x2+1 >= 0 && x2+1 < (int)width && y2 >= 0 && y2 < (int)height)
                pixels[y2 * (int)width + x2 + 1] = fg;
        }
    }
}

void lumo_app_draw_background(
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    struct lumo_rect full = {0, 0, (int)width, (int)height};
    memset(pixels, 0, (size_t)width * height * 4);
    lumo_app_fill_gradient(pixels, width, height, &full,
        lumo_app_argb(0xFF, 0x2C, 0x00, 0x1E),
        lumo_app_argb(0xFF, 0x1D, 0x11, 0x22));
}

static void lumo_app_card_text(
    enum lumo_app_id app_id, uint32_t card_index,
    const char **title, const char **body
) {
    static const char *const titles[][3] = {
        [LUMO_APP_PHONE] = {"Favorites", "Recents", "Dialer"},
        [LUMO_APP_MESSAGES] = {"Unread", "Pinned", "Compose"},
        [LUMO_APP_BROWSER] = {"Quick Start", "Tabs", "Reading"},
        [LUMO_APP_CAMERA] = {"Photo", "Video", "Gallery"},
        [LUMO_APP_MAPS] = {"Home", "Nearby", "Route"},
        [LUMO_APP_MUSIC] = {"Now Playing", "Library", "Mixes"},
        [LUMO_APP_PHOTOS] = {"Memories", "Albums", "Shared"},
        [LUMO_APP_VIDEOS] = {"Continue", "Downloads", "Queue"},
    };
    static const char *const bodies[][3] = {
        [LUMO_APP_PHONE] = {"Mira, Kai, and Dev", "Last call 2m ago", "Tap to open keypad"},
        [LUMO_APP_MESSAGES] = {"2 new threads", "Family and team", "Start a new message"},
        [LUMO_APP_BROWSER] = {"Open your saved starts", "3 tabs waiting", "Continue later list"},
        [LUMO_APP_CAMERA] = {"Ready for quick capture", "Last mode used", "Recent shots saved"},
        [LUMO_APP_MAPS] = {"18 min away", "Coffee and fuel", "Fastest arrival"},
        [LUMO_APP_MUSIC] = {"Resume favorite album", "Recent artists", "Fresh queue"},
        [LUMO_APP_PHOTOS] = {"Highlights for today", "Trips and camera roll", "Latest updates"},
        [LUMO_APP_VIDEOS] = {"Resume in one tap", "Offline ready", "Watch later list"},
    };

    if (title == NULL || body == NULL || card_index > 2) return;
    if (app_id < sizeof(titles) / sizeof(titles[0]) && titles[app_id][0] != NULL) {
        *title = titles[app_id][card_index];
        *body = bodies[app_id][card_index];
    } else {
        *title = "Panel";
        *body = "Ready";
    }
}

void lumo_app_render_stub(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    enum lumo_app_id app_id = ctx != NULL ? ctx->app_id : LUMO_APP_PHONE;
    bool close_active = ctx != NULL ? ctx->close_active : false;
    struct lumo_rect hero;
    uint32_t accent = lumo_app_accent_argb(app_id);
    uint32_t text_primary = lumo_app_argb(0xFF, 0xFF, 0xFF, 0xFF);
    uint32_t text_secondary = lumo_app_argb(0xFF, 0xAE, 0xA7, 0x9F);
    uint32_t panel_fill = lumo_app_argb(0xFF, 0x2C, 0x16, 0x28);
    uint32_t panel_stroke = lumo_app_argb(0xFF, 0x5E, 0x2C, 0x56);

    if (pixels == NULL || width == 0 || height == 0) return;
    lumo_app_draw_background(pixels, width, height);

    {
        struct lumo_rect badge = {28, 28, 180, 28};
        lumo_app_fill_rounded_rect(pixels, width, height, &badge, 14,
            lumo_app_argb(0xFF, 0x2C, 0x00, 0x1E));
        lumo_app_draw_text(pixels, width, height, badge.x + 14,
            badge.y + 8, 2, text_secondary, "LUMO NATIVE");
    }

    lumo_app_draw_text(pixels, width, height, 28, 86, 4, text_primary,
        lumo_app_title(app_id));
    lumo_app_draw_text(pixels, width, height, 28, 128, 2, text_secondary,
        lumo_app_subtitle(app_id));

    hero.x = 28; hero.y = 170; hero.width = (int)width - 56;
    hero.height = (int)(height / 4);
    lumo_app_fill_rounded_rect(pixels, width, height, &hero, 28, panel_fill);
    lumo_app_draw_outline(pixels, width, height, &hero, 2, panel_stroke);
    lumo_app_draw_text(pixels, width, height, hero.x + 24, hero.y + 24, 2,
        text_secondary, "NOW READY");
    lumo_app_fill_rect(pixels, width, height, hero.x + 24, hero.y + 62,
        hero.width - 48, 8, accent);
    lumo_app_draw_text(pixels, width, height, hero.x + 24, hero.y + 88, 3,
        text_primary, lumo_app_title(app_id));
    lumo_app_draw_text(pixels, width, height, hero.x + 24, hero.y + 130, 2,
        text_secondary, "Touch-first native client");

    for (uint32_t ci = 0; ci < 3; ci++) {
        struct lumo_rect card = {
            .x = 28 + (int)ci * ((int)width - 84) / 3,
            .y = hero.y + hero.height + 24,
            .width = ((int)width - 112) / 3,
            .height = (int)height - (hero.y + hero.height + 56),
        };
        const char *ct = NULL, *cb = NULL;
        if (card.width <= 0 || card.height <= 0) continue;
        lumo_app_fill_rounded_rect(pixels, width, height, &card, 22,
            lumo_app_argb(0xFF, 0x2C, 0x16, 0x28));
        lumo_app_draw_outline(pixels, width, height, &card, 2, panel_stroke);
        lumo_app_fill_rect(pixels, width, height, card.x + 18, card.y + 18,
            card.width - 36, 6, accent);
        lumo_app_card_text(app_id, ci, &ct, &cb);
        lumo_app_draw_text(pixels, width, height, card.x + 18, card.y + 36, 2,
            text_secondary, ct ? ct : "PANEL");
        lumo_app_draw_text(pixels, width, height, card.x + 18, card.y + 68, 2,
            text_primary, cb ? cb : "Ready");
    }

    /* close button removed — use bottom-edge swipe */
}

void lumo_app_render(
    const struct lumo_app_render_context *ctx,
    uint32_t *pixels, uint32_t width, uint32_t height
) {
    if (ctx == NULL) return;

    switch (ctx->app_id) {
    case LUMO_APP_CLOCK:
        lumo_app_render_clock(ctx, pixels, width, height); return;
    case LUMO_APP_FILES:
        lumo_app_render_files(ctx, pixels, width, height); return;
    case LUMO_APP_SETTINGS:
        lumo_app_render_settings(ctx, pixels, width, height); return;
    case LUMO_APP_NOTES:
        lumo_app_render_notes(ctx, pixels, width, height); return;
    case LUMO_APP_MESSAGES:
        lumo_app_render_terminal(ctx, pixels, width, height); return;
    default:
        lumo_app_render_stub(ctx, pixels, width, height); return;
    }
}

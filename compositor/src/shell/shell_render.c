/*
 * shell_render.c — mode renderers and main render dispatch for the
 * Lumo shell client.  Theme engine lives in shell_theme.c, animated
 * background in shell_background.c.
 */

#include "shell_render_internal.h"
#include "lumo/lumo_icon.h"
#include "lumo/app_icons.h"
#include "lumo/version.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── forward declarations (static helpers in this file) ───────────── */

static void lumo_draw_touch_audit(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_shell_client *client);
static void lumo_draw_launcher(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target,
    double visibility);
static void lumo_draw_osk(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target,
    double visibility);
static void lumo_draw_gesture(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target);
static void lumo_draw_wifi_bars(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int x, int y, int bar_count,
    uint32_t active_color, uint32_t dim_color);
static void lumo_draw_quick_settings_panel(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int bar_height, const struct lumo_shell_client *client);
static void lumo_draw_status(
    uint32_t *pixels, uint32_t width, uint32_t height,
    const struct lumo_shell_client *client);

/* ── animation helper (extern) ────────────────────────────────────── */

double lumo_shell_client_animation_value(
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

/* ── touch audit overlay ──────────────────────────────────────────── */

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

/* ── launcher (app grid) ──────────────────────────────────────────── */

static void lumo_draw_launcher(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target,
    double visibility
) {
    const uint32_t panel_top = lumo_argb(0xFF,
        (uint8_t)(lumo_theme.base_r + 0x14),
        (uint8_t)(lumo_theme.base_g + 0x0C),
        (uint8_t)(lumo_theme.base_b + 0x10));
    const uint32_t panel_bottom = lumo_argb(0xFF,
        (uint8_t)lumo_theme.base_r,
        (uint8_t)lumo_theme.base_g,
        (uint8_t)lumo_theme.base_b);
    const uint32_t panel_stroke = lumo_theme.panel_stroke;
    const uint32_t title_color = lumo_theme.text_primary;
    const uint32_t subtitle_color = lumo_theme.text_secondary;
    const uint32_t tile_fill = lumo_theme.tile_fill;
    const uint32_t tile_stroke = lumo_theme.tile_stroke;
    const uint32_t highlight = lumo_theme.accent;
    const uint32_t accent_colors[] = {
        lumo_argb(0xFF, 0xE9, 0x54, 0x20),
        lumo_argb(0xFF, 0x77, 0x21, 0x6F),
        lumo_argb(0xFF, 0xE9, 0x54, 0x20),
        lumo_argb(0xFF, 0x77, 0x21, 0x6F),
    };
    struct lumo_rect panel_rect = {0};
    struct lumo_rect accent_rect = {0};
    struct lumo_rect title_badge = {0};
    struct lumo_rect search_rect = {0};
    const char *query = client != NULL ? client->toast_message : NULL;
    bool has_query = query != NULL && query[0] != '\0' &&
        strcmp(query, "-") != 0;
    int slide_y;

    if (client != NULL && client->compositor_touch_audit_active) {
        lumo_draw_touch_audit(pixels, width, height, client);
        return;
    }

    /* panels are drawn on the launcher surface but don't depend on
     * the launcher slide-up animation — render at full opacity
     * immediately so panels appear without delay */
    if (client != NULL && (client->compositor_quick_settings_visible ||
            client->compositor_time_panel_visible ||
            client->compositor_notification_panel_visible) &&
            !client->compositor_launcher_visible) {
        int bar_h = 48;

        if (client->compositor_quick_settings_visible) {
            lumo_draw_quick_settings_panel(pixels, width, height, bar_h,
                client);
        }
        if (client->compositor_notification_panel_visible) {
            struct lumo_rect np = {0};
            if (lumo_shell_notification_panel_rect(width, height, &np)) {
                uint32_t np_bg = lumo_theme.panel_bg;
                uint32_t np_stroke = lumo_theme.panel_stroke;
                uint32_t np_label = lumo_theme.text_secondary;
                uint32_t np_text = lumo_theme.text_primary;
                uint32_t np_accent = lumo_theme.accent;
                int ny = np.y + 16;

                lumo_fill_rounded_rect(pixels, width, height, &np, 18,
                    np_bg);
                lumo_draw_outline(pixels, width, height, &np, 1,
                    np_stroke);

                /* header */
                lumo_draw_text(pixels, width, height, np.x + 16, ny, 3,
                    np_accent, "NOTIFICATIONS");
                ny += 30;
                {
                    struct lumo_rect sep = {np.x + 12, ny, np.width - 24, 1};
                    lumo_fill_rect(pixels, width, height, sep.x, sep.y,
                        sep.width, sep.height, np_stroke);
                }
                ny += 10;

                if (client->notification_count == 0) {
                    lumo_draw_text(pixels, width, height, np.x + 16, ny,
                        2, np_label, "NO NOTIFICATIONS");
                    ny += 22;
                    lumo_draw_text(pixels, width, height, np.x + 16, ny,
                        2, np_label, "ALL CLEAR");
                } else {
                    for (int i = client->notification_count - 1;
                            i >= 0 && ny + 44 < np.y + np.height; i--) {
                        struct lumo_rect card = {
                            np.x + 8, ny, np.width - 16, 40
                        };
                        lumo_fill_rounded_rect(pixels, width, height,
                            &card, 10, lumo_argb(0x30,
                                (uint8_t)lumo_theme.base_r,
                                (uint8_t)lumo_theme.base_g,
                                (uint8_t)lumo_theme.base_b));

                        /* notification dot */
                        lumo_fill_rect(pixels, width, height,
                            card.x + 8, card.y + 16, 6, 6, np_accent);

                        /* notification text */
                        lumo_draw_text(pixels, width, height,
                            card.x + 20, card.y + 12, 2, np_text,
                            client->notifications[i]);

                        ny += 48;
                    }
                }
            }
        }
        if (client->compositor_time_panel_visible) {
            uint32_t tp_bg = lumo_theme.panel_bg;
            uint32_t tp_stroke = lumo_theme.panel_stroke;
            uint32_t tp_label = lumo_theme.text_secondary;
            uint32_t tp_text = lumo_theme.text_primary;
            uint32_t tp_accent = lumo_theme.accent;
            struct lumo_rect tp = {0};
            time_t now = time(NULL);
            struct tm tm_now = {0};
            char tbuf[16], dbuf[32], wbuf[16];

            localtime_r(&now, &tm_now);
            strftime(tbuf, sizeof(tbuf), "%H:%M", &tm_now);
            strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &tm_now);
            snprintf(wbuf, sizeof(wbuf), "WEEK %d",
                (tm_now.tm_yday / 7) + 1);

            if (!lumo_shell_time_panel_rect(width, height, &tp)) {
                return;
            }
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

    if (visibility <= 0.0 ||
            (client != NULL && !client->compositor_launcher_visible)) {
        lumo_clear_pixels(pixels, width, height);
        return;
    }

    /* GNOME 3.x style fullscreen app grid */
    slide_y = (int)((1.0 - visibility) * (int)(height / 4));

    /* translucent dark overlay */
    {
        uint8_t alpha = (uint8_t)(0xE0 * visibility);
        uint32_t overlay = lumo_argb(alpha,
            (uint8_t)lumo_theme.base_r,
            (uint8_t)lumo_theme.base_g,
            (uint8_t)lumo_theme.base_b);
        struct lumo_rect full = {0, 0, (int)width, (int)height};
        lumo_fill_rounded_rect(pixels, width, height, &full, 0, overlay);
    }

    if (!lumo_shell_launcher_panel_rect(width, height, &panel_rect)) {
        return;
    }
    if (!lumo_shell_launcher_search_bar_rect(width, height, &search_rect)) {
        return;
    }

    panel_rect.y += slide_y;
    search_rect.y += slide_y;
    accent_rect.x = panel_rect.x + 24;
    accent_rect.y = panel_rect.y + 18;
    accent_rect.width = 56;
    accent_rect.height = 6;
    title_badge.x = panel_rect.x + 24;
    title_badge.y = panel_rect.y + 32;
    title_badge.width = 220;
    title_badge.height = 18;
    lumo_fill_vertical_gradient(pixels, width, height, &panel_rect,
        panel_top, panel_bottom);
    lumo_draw_outline(pixels, width, height, &panel_rect, 1, panel_stroke);
    lumo_fill_rounded_rect(pixels, width, height, &accent_rect, 3, highlight);
    lumo_draw_text(pixels, width, height, title_badge.x, title_badge.y, 2,
        title_color, "APPLICATIONS");

    /* close button removed — gesture-based navigation handles dismiss */

    lumo_fill_rounded_rect(pixels, width, height, &search_rect, 20,
        has_query ? lumo_theme.panel_bg : lumo_theme.tile_fill);
    lumo_draw_outline(pixels, width, height, &search_rect, 1,
        has_query ? highlight : lumo_theme.tile_stroke);
    if (has_query) {
        char display[40];

        snprintf(display, sizeof(display), "> %.36s", query);
        lumo_draw_text_centered(pixels, width, height, &search_rect, 2,
            title_color, display);
    } else {
        lumo_draw_text_centered(pixels, width, height, &search_rect, 2,
            subtitle_color, "TYPE TO SEARCH...");
    }

    /* paginate tiles — tiles_per_page from grid, page from client state */
    uint32_t tiles_per_page = (uint32_t)lumo_shell_launcher_tile_count();
    uint32_t total_tiles = (uint32_t)lumo_shell_launcher_filtered_tile_count(query);
    int page = (client != NULL) ? client->launcher_page : 0;
    int total_pages = (int)((total_tiles + tiles_per_page - 1) / tiles_per_page);
    if (total_pages < 1) total_pages = 1;
    if (page >= total_pages) page = total_pages - 1;
    if (page < 0) page = 0;
    uint32_t page_start = (uint32_t)page * tiles_per_page;
    uint32_t page_end = page_start + tiles_per_page;
    if (page_end > total_tiles) page_end = total_tiles;

    for (uint32_t visible_index = page_start;
            visible_index < page_end;
            visible_index++) {
        struct lumo_rect tile_rect = {0};
        struct lumo_rect icon_rect = {0};
        struct lumo_rect label_rect = {0};
        uint32_t tile_index = 0;
        int cx;
        int cy;
        bool active;
        const char *label;
        uint32_t accent;

        /* global index for tile data, page-relative for grid position */
        if (!lumo_shell_launcher_filtered_tile_rect(width, height, query,
                visible_index, &tile_index, &tile_rect)) {
            continue;
        }

        tile_rect.y += slide_y;
        cx = tile_rect.x + tile_rect.width / 2;
        cy = tile_rect.y +
            ((tile_rect.height - (56 + 8 + 20)) > 0
                ? (tile_rect.height - (56 + 8 + 20)) / 2
                : 0);
        active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_LAUNCHER_TILE &&
            active_target->index == tile_index;
        label = lumo_shell_launcher_tile_label(tile_index);
        accent = accent_colors[tile_index %
            (sizeof(accent_colors) / sizeof(accent_colors[0]))];

        if (active) {
            lumo_fill_rounded_rect(pixels, width, height, &tile_rect, 18,
                lumo_argb(0x28,
                    (uint8_t)((highlight >> 16) & 0xFF),
                    (uint8_t)((highlight >> 8) & 0xFF),
                    (uint8_t)(highlight & 0xFF)));
            lumo_draw_outline(pixels, width, height, &tile_rect, 1, highlight);
        }

        icon_rect.x = cx - 28;
        icon_rect.y = cy;
        icon_rect.width = 56;
        icon_rect.height = 56;
        lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 16,
            active ? highlight : tile_fill);
        lumo_draw_outline(pixels, width, height, &icon_rect, 1, tile_stroke);

        {
            int inner = 36;

            icon_rect.x = cx - inner / 2;
            icon_rect.y = cy + (56 - inner) / 2;
            icon_rect.width = inner;
            icon_rect.height = inner;
        }

        /* blit pre-rendered icon sprite if available */
        if (tile_index < LUMO_APP_ICON_COUNT) {
            const uint32_t *sprite = lumo_app_icons[tile_index];
            int ix = icon_rect.x + (icon_rect.width - LUMO_APP_ICON_W) / 2;
            int iy = icon_rect.y + (icon_rect.height - LUMO_APP_ICON_H) / 2;
            for (int sy = 0; sy < LUMO_APP_ICON_H; sy++) {
                int dy = iy + sy;
                if (dy < 0 || dy >= (int)height) continue;
                for (int sx = 0; sx < LUMO_APP_ICON_W; sx++) {
                    int dx = ix + sx;
                    if (dx < 0 || dx >= (int)width) continue;
                    uint32_t src = sprite[sy * LUMO_APP_ICON_W + sx];
                    uint32_t sa = (src >> 24) & 0xFF;
                    if (sa < 16) continue;
                    /* skip background color pixels (aubergine) */
                    uint32_t sr = (src >> 16) & 0xFF;
                    uint32_t sg = (src >> 8) & 0xFF;
                    uint32_t sb = src & 0xFF;
                    if (sr < 0x40 && sg < 0x10 && sb < 0x30) continue;
                    pixels[dy * width + dx] = src;
                }
            }
        } else {
            /* fallback: filled rounded rect for unknown icons */
            lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 12,
                accent);
        }

        /* legacy programmatic icons (kept as dead code reference) */
        if (0) {
            int ix = icon_rect.x + 4;
            int iy = icon_rect.y + 4;
            int isz = 28;
            uint32_t ic = accent;

            switch (tile_index) {
            case 0: /* Phone - handset */
                lumo_fill_rect(pixels, width, height, ix + 10, iy, 8, 6, ic);
                lumo_fill_rect(pixels, width, height, ix + 8, iy + 6, 12, 2, ic);
                lumo_fill_rect(pixels, width, height, ix + 12, iy + 8, 4, 14, ic);
                lumo_fill_rect(pixels, width, height, ix + 8, iy + 22, 12, 2, ic);
                lumo_fill_rect(pixels, width, height, ix + 10, iy + 24, 8, 4, ic);
                break;
            case 1: /* Terminal - prompt */
                lumo_fill_rect(pixels, width, height, ix, iy, isz, 2, ic);
                lumo_fill_rect(pixels, width, height, ix, iy, 2, isz, ic);
                lumo_fill_rect(pixels, width, height, ix, iy + isz - 2, isz, 2, ic);
                lumo_fill_rect(pixels, width, height, ix + isz - 2, iy, 2, isz, ic);
                lumo_fill_rect(pixels, width, height, ix + 6, iy + 10, 4, 4, ic);
                lumo_fill_rect(pixels, width, height, ix + 10, iy + 14, 4, 4, ic);
                lumo_fill_rect(pixels, width, height, ix + 16, iy + 20, 8, 2, ic);
                break;
            case 2: /* Browser - globe */
                lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 22, ic);
                lumo_fill_rect(pixels, width, height, ix + 12, iy, 4, isz,
                    lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                lumo_fill_rect(pixels, width, height, ix, iy + 12, isz, 4,
                    lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                break;
            case 3: /* Camera - lens */
                lumo_fill_rect(pixels, width, height, ix + 4, iy, isz - 8, 4, ic);
                lumo_fill_rect(pixels, width, height, ix, iy + 4, isz, isz - 4, ic);
                {
                    struct lumo_rect lens = {ix + 8, iy + 10, 12, 12};
                    lumo_fill_rounded_rect(pixels, width, height, &lens, 6,
                        lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                }
                break;
            case 4: /* Maps - pin */
                lumo_fill_rect(pixels, width, height, ix + 10, iy + 2, 8, 14, ic);
                lumo_fill_rect(pixels, width, height, ix + 8, iy + 4, 12, 10, ic);
                lumo_fill_rect(pixels, width, height, ix + 12, iy + 16, 4, 10, ic);
                break;
            case 5: /* Music - note */
                lumo_fill_rect(pixels, width, height, ix + 18, iy + 2, 4, 20, ic);
                lumo_fill_rect(pixels, width, height, ix + 8, iy + 4, 14, 4, ic);
                lumo_fill_rect(pixels, width, height, ix + 4, iy + 6, 4, 18, ic);
                {
                    struct lumo_rect n1 = {ix, iy + 20, 10, 8};
                    struct lumo_rect n2 = {ix + 14, iy + 18, 10, 8};
                    lumo_fill_rounded_rect(pixels, width, height, &n1, 4, ic);
                    lumo_fill_rounded_rect(pixels, width, height, &n2, 4, ic);
                }
                break;
            case 6: /* Photos - mountain */
                lumo_fill_rect(pixels, width, height, ix, iy + isz - 8, isz, 8, ic);
                lumo_fill_rect(pixels, width, height, ix + 4, iy + 12, 4, 8, ic);
                lumo_fill_rect(pixels, width, height, ix + 8, iy + 8, 4, 12, ic);
                lumo_fill_rect(pixels, width, height, ix + 12, iy + 14, 4, 6, ic);
                lumo_fill_rect(pixels, width, height, ix + 16, iy + 6, 4, 14, ic);
                lumo_fill_rect(pixels, width, height, ix + 20, iy + 10, 4, 10, ic);
                break;
            case 7: /* Videos - play triangle */
                lumo_fill_rect(pixels, width, height, ix + 8, iy + 4, 4, isz - 8, ic);
                lumo_fill_rect(pixels, width, height, ix + 12, iy + 6, 4, isz - 12, ic);
                lumo_fill_rect(pixels, width, height, ix + 16, iy + 8, 4, isz - 16, ic);
                lumo_fill_rect(pixels, width, height, ix + 20, iy + 10, 4, isz - 20, ic);
                break;
            case 8: /* Clock - clock face */
                lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 22, ic);
                {
                    struct lumo_rect inner = {ix + 4, iy + 4, isz - 8, isz - 8};
                    lumo_fill_rounded_rect(pixels, width, height, &inner, 10,
                        lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                }
                lumo_fill_rect(pixels, width, height, ix + 13, iy + 6, 2, 10, ic);
                lumo_fill_rect(pixels, width, height, ix + 13, iy + 13, 8, 2, ic);
                break;
            case 9: /* Notes - lines */
                lumo_fill_rect(pixels, width, height, ix + 2, iy + 4, isz - 4, 3, ic);
                lumo_fill_rect(pixels, width, height, ix + 2, iy + 10, isz - 8, 3, ic);
                lumo_fill_rect(pixels, width, height, ix + 2, iy + 16, isz - 4, 3, ic);
                lumo_fill_rect(pixels, width, height, ix + 2, iy + 22, isz - 12, 3, ic);
                break;
            case 10: /* Files - folder */
                lumo_fill_rect(pixels, width, height, ix + 2, iy + 4, 10, 4, ic);
                lumo_fill_rect(pixels, width, height, ix + 2, iy + 8, isz - 4,
                    isz - 12, ic);
                lumo_fill_rect(pixels, width, height, ix + 2, iy + 8, isz - 4, 4,
                    lumo_argb(0xFF, 0x77, 0x21, 0x6F));
                break;
            case 11: /* Settings - gear */
                lumo_fill_rect(pixels, width, height, ix + 10, iy, 8, isz, ic);
                lumo_fill_rect(pixels, width, height, ix, iy + 10, isz, 8, ic);
                lumo_fill_rect(pixels, width, height, ix + 4, iy + 4, 6, 6, ic);
                lumo_fill_rect(pixels, width, height, ix + 18, iy + 4, 6, 6, ic);
                lumo_fill_rect(pixels, width, height, ix + 4, iy + 18, 6, 6, ic);
                lumo_fill_rect(pixels, width, height, ix + 18, iy + 18, 6, 6, ic);
                {
                    struct lumo_rect ctr = {ix + 8, iy + 8, 12, 12};
                    lumo_fill_rounded_rect(pixels, width, height, &ctr, 6,
                        lumo_argb(0xFF, 0x1E, 0x0A, 0x1A));
                }
                break;
            default:
                lumo_fill_rounded_rect(pixels, width, height, &icon_rect, 12,
                    accent);
                break;
            }
        } /* end dead code */

        label_rect.x = tile_rect.x;
        label_rect.y = cy + 56 + 8;
        label_rect.width = tile_rect.width;
        label_rect.height = 20;
        lumo_draw_text_centered(pixels, width, height, &label_rect, 2,
            active ? highlight : subtitle_color,
            label != NULL ? label : "APP");
    }

    /* page indicator dots */
    if (total_pages > 1) {
        int dot_r = 4;
        int dot_gap = 16;
        int dots_w = total_pages * (dot_r * 2 + dot_gap) - dot_gap;
        int dot_x = (int)width / 2 - dots_w / 2;
        int dot_y = (int)height - 30 + slide_y;
        for (int p = 0; p < total_pages; p++) {
            struct lumo_rect dot = {
                dot_x + p * (dot_r * 2 + dot_gap),
                dot_y, dot_r * 2, dot_r * 2
            };
            lumo_fill_rounded_rect(pixels, width, height, &dot, dot_r,
                p == page ? highlight
                    : lumo_argb(0x40, 0xFF, 0xFF, 0xFF));
        }
    }
}

/* ── on-screen keyboard ───────────────────────────────────────────── */

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
    bool shift_active = client != NULL && client->compositor_osk_shift_active;

    if (visibility <= 0.0) {
        lumo_clear_pixels(pixels, width, height);
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
        bool is_shift = label != NULL && strcmp(label, "^") == 0;
        bool is_backspace = label != NULL && strcmp(label, "<-") == 0;
        bool is_close = label != NULL && strcmp(label, "v") == 0;
        bool is_page = commit != NULL && commit[0] == '\x01';
        bool is_special = is_shift || is_backspace || is_close || is_page;
        int scale = 3;
        uint32_t fill;
        uint32_t border;
        uint32_t text_col;
        char lower_label[2] = {0};
        const char *draw_label;

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
        } else if (is_shift && shift_active) {
            fill = active_fill;
            border = active_border;
            text_col = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
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

        /* show lowercase labels when shift is off (letters only) */
        draw_label = label;
        if (!shift_active && label != NULL && label[0] >= 'A' &&
                label[0] <= 'Z' && label[1] == '\0') {
            lower_label[0] = label[0] + ('a' - 'A');
            draw_label = lower_label;
        }

        label_rect = key_rect;
        if (draw_label != NULL && strlen(draw_label) > 4) {
            scale = 2;
        }
        lumo_draw_text_centered(pixels, width, height, &label_rect, scale,
            text_col,
            draw_label != NULL ? draw_label :
            (commit != NULL ? commit : ""));
    }
}

/* ── gesture handle ───────────────────────────────────────────────── */

static void lumo_draw_gesture(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target
) {
    /* invisible — gesture zone handles edge detection only,
     * no visual indicator needed */
    (void)pixels;
    (void)width;
    (void)height;
    (void)client;
    (void)active_target;
}

/* ── wifi signal bars ─────────────────────────────────────────────── */

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

/* ── quick settings panel ─────────────────────────────────────────── */

static void lumo_draw_quick_settings_panel(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int bar_height,
    const struct lumo_shell_client *client
) {
    const uint32_t panel_bg = lumo_theme.panel_bg;
    const uint32_t panel_stroke = lumo_theme.panel_stroke;
    const uint32_t label_color = lumo_theme.text_secondary;
    const uint32_t value_color = lumo_theme.text_primary;
    const uint32_t accent = lumo_theme.accent;
    const uint32_t dim = lumo_theme.dim;
    struct lumo_rect panel = {0};
    int row_y;

    (void)bar_height;
    if (!lumo_shell_quick_settings_panel_rect(width, height, &panel)) {
        return;
    }
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
            struct lumo_rect reload_btn = {0};
            struct lumo_rect rotate_btn = {0};
            struct lumo_rect screenshot_btn = {0};
            struct lumo_rect settings_btn = {0};

            (void)row_y;
            (void)lumo_shell_quick_settings_button_rect(width, height, 0,
                &reload_btn);
            (void)lumo_shell_quick_settings_button_rect(width, height, 1,
                &rotate_btn);
            (void)lumo_shell_quick_settings_button_rect(width, height, 2,
                &screenshot_btn);
            (void)lumo_shell_quick_settings_button_rect(width, height, 3,
                &settings_btn);
            /* icons: use symbolic chars for compact display */
            lumo_fill_rounded_rect(pixels, width, height, &reload_btn,
                8, accent);
            lumo_draw_text_centered(pixels, width, height, &reload_btn, 2,
                value_color, "(.) RELOAD");
            lumo_fill_rounded_rect(pixels, width, height, &rotate_btn,
                8, lumo_argb(0xFF, 0x77, 0x21, 0x6F));
            lumo_draw_text_centered(pixels, width, height, &rotate_btn, 2,
                value_color, "[>  ROTATE");
            lumo_fill_rounded_rect(pixels, width, height, &screenshot_btn,
                8, lumo_argb(0xFF, 0x1E, 0x68, 0x5B));
            lumo_draw_text_centered(pixels, width, height, &screenshot_btn, 2,
                value_color, "[] CAPTURE");
            lumo_fill_rounded_rect(pixels, width, height, &settings_btn,
                8, lumo_argb(0xFF, 0x3A, 0x3A, 0x5E));
            lumo_draw_text_centered(pixels, width, height, &settings_btn, 2,
                value_color, "{} SETTINGS");
        }
    }
}

/* ── status bar ───────────────────────────────────────────────────── */

static void lumo_draw_status(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client
) {
    /* hide status bar during boot splash (background prerendering) */
    if (access("/run/user/1001/lumo-boot-active", F_OK) == 0)
        return;

    const uint32_t bar_top = lumo_theme.bar_top;
    const uint32_t bar_bottom = lumo_theme.bar_bottom;
    const uint32_t separator = lumo_theme.dim;
    const uint32_t text_color = lumo_theme.text_primary;
    const uint32_t accent_color = lumo_theme.accent;
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
        /* cache wifi bars — async fork to avoid blocking the render
         * thread.  The result is stored by the forked child into a
         * pipe which we read non-blockingly. Refresh every 30 seconds. */
        static int cached_wifi_bars = 0;
        static uint64_t wifi_last_read = 0;
        static int wifi_pipe_fd = -1;
        static pid_t wifi_pid = -1;
        uint64_t now_ms = (uint64_t)now;

        /* read result from previous async query */
        if (wifi_pipe_fd >= 0) {
            char buf[8] = {0};
            ssize_t n = read(wifi_pipe_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                int dbm = atoi(buf);
                if (dbm < 0) {
                    if (dbm > -50) cached_wifi_bars = 4;
                    else if (dbm > -60) cached_wifi_bars = 3;
                    else if (dbm > -70) cached_wifi_bars = 2;
                    else if (dbm > -90) cached_wifi_bars = 1;
                    else cached_wifi_bars = 0;
                }
                close(wifi_pipe_fd);
                wifi_pipe_fd = -1;
                if (wifi_pid > 0) {
                    waitpid(wifi_pid, NULL, WNOHANG);
                    wifi_pid = -1;
                }
            } else if (n == 0) {
                close(wifi_pipe_fd);
                wifi_pipe_fd = -1;
                if (wifi_pid > 0) {
                    waitpid(wifi_pid, NULL, WNOHANG);
                    wifi_pid = -1;
                }
            }
            /* n < 0 && errno == EAGAIN: not ready yet, try next frame */
        }

        /* launch async wifi query every 30 seconds */
        if (wifi_pipe_fd < 0 &&
                (wifi_last_read == 0 || now_ms >= wifi_last_read + 30)) {
            wifi_last_read = now_ms;
            int pipefd[2];
            if (pipe(pipefd) == 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    close(pipefd[0]);
                    /* child: run iw, write dBm to pipe */
                    FILE *fp = popen("iw dev wlan0 link 2>/dev/null", "r");
                    if (fp) {
                        char line[128];
                        while (fgets(line, sizeof(line), fp)) {
                            int dbm = 0;
                            if (sscanf(line, " signal: %d dBm", &dbm) == 1) {
                                char out[16];
                                int len = snprintf(out, sizeof(out), "%d", dbm);
                                (void)write(pipefd[1], out, (size_t)len);
                                break;
                            }
                        }
                        pclose(fp);
                    }
                    close(pipefd[1]);
                    _exit(0);
                } else if (pid > 0) {
                    close(pipefd[1]);
                    /* set read end non-blocking */
                    int flags = fcntl(pipefd[0], F_GETFL);
                    if (flags >= 0)
                        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
                    wifi_pipe_fd = pipefd[0];
                    wifi_pid = pid;
                } else {
                    close(pipefd[0]);
                    close(pipefd[1]);
                }
            }

            /* quick check: is interface up? */
            if (cached_wifi_bars == 0) {
                FILE *fp = fopen("/sys/class/net/wlan0/operstate", "r");
                if (fp) {
                    char state[16] = {0};
                    if (fgets(state, sizeof(state), fp) &&
                            strncmp(state, "up", 2) == 0)
                        cached_wifi_bars = 1;
                    fclose(fp);
                }
            }
        }
        lumo_draw_wifi_bars(pixels, width, height,
            (int)width - 42, bar_height / 2 - 8, cached_wifi_bars,
            accent_color, wifi_dim);
    }

    /* battery indicator — reads from sysfs, cached every 60 seconds */
    {
        static int cached_battery_pct = -1; /* -1 = no battery */
        static bool cached_battery_charging = false;
        static uint64_t bat_last_read = 0;
        uint64_t now_s = (uint64_t)now;
        if (bat_last_read == 0 || now_s >= bat_last_read + 60) {
            bat_last_read = now_s;
            cached_battery_pct = -1;
            /* scan power_supply entries for a battery */
            const char *bat_paths[] = {
                "/sys/class/power_supply/battery/capacity",
                "/sys/class/power_supply/BAT0/capacity",
                "/sys/class/power_supply/BAT1/capacity",
                NULL
            };
            for (int i = 0; bat_paths[i] != NULL; i++) {
                FILE *fp = fopen(bat_paths[i], "r");
                if (fp != NULL) {
                    int pct = 0;
                    if (fscanf(fp, "%d", &pct) == 1 && pct >= 0 && pct <= 100)
                        cached_battery_pct = pct;
                    fclose(fp);
                    break;
                }
            }
            /* check charging status */
            const char *stat_paths[] = {
                "/sys/class/power_supply/battery/status",
                "/sys/class/power_supply/BAT0/status",
                "/sys/class/power_supply/BAT1/status",
                NULL
            };
            for (int i = 0; stat_paths[i] != NULL; i++) {
                FILE *fp = fopen(stat_paths[i], "r");
                if (fp != NULL) {
                    char st[32] = {0};
                    if (fgets(st, sizeof(st), fp))
                        cached_battery_charging =
                            (strncmp(st, "Charging", 8) == 0);
                    fclose(fp);
                    break;
                }
            }
        }
        if (cached_battery_pct >= 0) {
            char bat_buf[8];
            snprintf(bat_buf, sizeof(bat_buf), "%s%d%%",
                cached_battery_charging ? "+" : "", cached_battery_pct);
            uint32_t bat_color = cached_battery_pct <= 15
                ? lumo_argb(0xFF, 0xFF, 0x44, 0x44) /* red */
                : cached_battery_pct <= 30
                    ? lumo_argb(0xFF, 0xFF, 0xAA, 0x44) /* yellow */
                    : text_color;
            int tw = lumo_text_width(bat_buf, 2);
            lumo_draw_text(pixels, width, height,
                (int)width - 44 - tw - 8, bar_height / 2 - 7,
                2, bat_color, bat_buf);
        }
    }
}

static void lumo_draw_sidebar(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    const struct lumo_shell_client *client,
    const struct lumo_shell_target *active_target,
    double visibility
) {
    /* use theme colors matching the top bar — transitions with time/weather */
    uint32_t separator = lumo_argb(0x30, 0xFF, 0xFF, 0xFF);
    uint32_t icon_bg = lumo_argb(0x60, 0x40, 0x30, 0x50);
    uint32_t icon_active = lumo_theme.accent;
    uint32_t text_color = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);

    /* slide offset: 0 = fully visible, -width = fully off-screen left */
    int slide_x = (int)((1.0 - visibility) * -(double)width);

    /* leave top area transparent to avoid overlapping the status bar */
    int status_h = (int)height / 18;
    if (status_h < 32) status_h = 32;
    if (status_h > 48) status_h = 48;

    /* fill sidebar background below status bar with gradient */
    struct lumo_rect bg_rect = {slide_x, status_h,
        (int)width, (int)height - status_h};
    lumo_fill_vertical_gradient(pixels, width, height, &bg_rect,
        lumo_theme.bar_top, lumo_theme.bar_bottom);

    /* right edge separator line — only below status bar */
    lumo_fill_rect(pixels, width, height,
        (int)width - 1 + slide_x, status_h, 1,
        (int)height - status_h, separator);

    /* draw running app icons */
    for (uint32_t i = 0; i < client->running_app_count && i < 16; i++) {
        struct lumo_rect icon_rect;
        if (!lumo_shell_sidebar_app_rect(width, height, i, &icon_rect))
            break;
        icon_rect.x += slide_x;

        /* highlight if this is the active target */
        bool is_active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_SIDEBAR_APP &&
            active_target->index == i;

        uint32_t fill = is_active ? icon_active : icon_bg;
        lumo_fill_rounded_rect(pixels, width, height,
            &icon_rect, 10, fill);

        /* draw app name initial — strip "lumo-" prefix if present */
        char initial[2] = {0, 0};
        const char *app_id = client->running_app_ids[i];
        const char *name = app_id;
        if (strncmp(name, "lumo-", 5) == 0 && name[5] != '\0')
            name = name + 5;
        if (name[0] != '\0') {
            initial[0] = name[0];
            if (initial[0] >= 'a' && initial[0] <= 'z')
                initial[0] -= 32;
        } else {
            initial[0] = '?';
        }
        int tw = lumo_text_width(initial, 4);
        lumo_draw_text(pixels, width, height,
            icon_rect.x + (icon_rect.width - tw) / 2,
            icon_rect.y + (icon_rect.height - 18) / 2,
            4, text_color, initial);
    }

    /* context menu overlay */
    if (client->sidebar_context_menu_visible &&
            client->sidebar_context_menu_index < client->running_app_count) {
        struct lumo_rect icon_rect;
        if (lumo_shell_sidebar_app_rect(width, height,
                client->sidebar_context_menu_index, &icon_rect)) {
            icon_rect.x += slide_x;
            uint32_t menu_bg = lumo_argb(0xF0, 0x2A, 0x2A, 0x3E);
            uint32_t menu_stroke = lumo_argb(0x60, 0xFF, 0xFF, 0xFF);

            /* check if this app supports multiple windows */
            const char *aid = client->running_app_ids[
                client->sidebar_context_menu_index];
            bool multi_window = (strstr(aid, "terminal") != NULL ||
                strstr(aid, "browser") != NULL);
            int menu_h = multi_window ? 92 : 64;

            struct lumo_rect menu = {
                4, icon_rect.y - menu_h - 4,
                (int)width - 8, menu_h
            };
            if (menu.y < status_h + 4) menu.y = status_h + 4;
            if (menu.y + menu.height > (int)height)
                menu.y = (int)height - menu.height - 4;

            lumo_fill_rounded_rect(pixels, width, height, &menu, 8, menu_bg);
            lumo_draw_outline(pixels, width, height, &menu, 1, menu_stroke);
            int row_y = menu.y + 8;
            lumo_draw_text(pixels, width, height,
                menu.x + 10, row_y, 2, text_color, "OPEN");
            row_y += 28;
            if (multi_window) {
                lumo_draw_text(pixels, width, height,
                    menu.x + 10, row_y, 2,
                    lumo_argb(0xFF, 0x80, 0xC0, 0xFF), "NEW WINDOW");
                row_y += 28;
            }
            lumo_draw_text(pixels, width, height,
                menu.x + 10, row_y, 2,
                lumo_argb(0xFF, 0xFF, 0x66, 0x66), "CLOSE");
        }
    }

    /* drawer button at bottom — Lumo icon */
    {
        struct lumo_rect drawer_rect;
        if (lumo_shell_sidebar_drawer_button_rect(width, height, &drawer_rect)) {
            drawer_rect.x += slide_x;
            bool is_active = active_target != NULL &&
                active_target->kind == LUMO_SHELL_TARGET_SIDEBAR_DRAWER_BTN;
            if (is_active)
                lumo_fill_rounded_rect(pixels, width, height,
                    &drawer_rect, 12, icon_active);
            /* blit the Lumo icon scaled to fit the drawer rect */
            int ix = drawer_rect.x + (drawer_rect.width - LUMO_ICON_W) / 2;
            int iy = drawer_rect.y + (drawer_rect.height - LUMO_ICON_H) / 2;
            for (int sy = 0; sy < LUMO_ICON_H; sy++) {
                int dy = iy + sy;
                if (dy < 0 || dy >= (int)height) continue;
                for (int sx = 0; sx < LUMO_ICON_W; sx++) {
                    int dx = ix + sx;
                    if (dx < 0 || dx >= (int)width) continue;
                    uint32_t src = lumo_icon_48x48[sy * LUMO_ICON_W + sx];
                    uint32_t sa = (src >> 24) & 0xFF;
                    if (sa == 0) continue;
                    if (sa == 255) {
                        pixels[dy * width + dx] = src;
                    } else {
                        /* alpha blend */
                        uint32_t dst = pixels[dy * width + dx];
                        uint32_t inv = 255 - sa;
                        uint32_t r = (((src >> 16) & 0xFF) * sa +
                            ((dst >> 16) & 0xFF) * inv) / 255;
                        uint32_t g = (((src >> 8) & 0xFF) * sa +
                            ((dst >> 8) & 0xFF) * inv) / 255;
                        uint32_t b = ((src & 0xFF) * sa +
                            (dst & 0xFF) * inv) / 255;
                        pixels[dy * width + dx] =
                            0xFF000000 | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }
    }
}

/* ── main render entry point (extern) ─────────────────────────────── */

void lumo_render_surface(
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

    /* skip full-buffer clear for modes that overwrite every pixel:
     * background fills the whole surface with gradient + bokeh,
     * launcher fills with a fullscreen overlay rect.
     * Other modes still need the clear for transparency. */
    if (client->mode != LUMO_SHELL_MODE_BACKGROUND &&
            client->mode != LUMO_SHELL_MODE_LAUNCHER &&
            client->mode != LUMO_SHELL_MODE_GESTURE) {
        lumo_clear_pixels(pixels, width, height);
    }

    /* update shared theme colors from time-of-day + weather */
    lumo_theme_update(client->weather_code);

    visibility = client->mode == LUMO_SHELL_MODE_GESTURE
        || client->mode == LUMO_SHELL_MODE_STATUS
        || client->mode == LUMO_SHELL_MODE_BACKGROUND
        ? 1.0
        : lumo_shell_client_animation_value(client);
    (void)visibility; /* some modes don't use it */

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
        /* subtle touch ripple — expanding translucent ring */
        if (client != NULL && client->ripple_active) {
            uint64_t now = lumo_now_msec();
            uint64_t elapsed = now - client->ripple_start_msec;
            uint32_t duration = 400; /* ms */
            if (elapsed < duration) {
                float t = (float)elapsed / (float)duration;
                float radius = 20.0f + t * 80.0f;
                float alpha = (1.0f - t) * 0.15f; /* very subtle */
                int cx = (int)client->ripple_x;
                int cy = (int)client->ripple_y;
                int r = (int)radius;
                uint32_t a8 = (uint32_t)(alpha * 255.0f);
                uint32_t ring_color = lumo_argb((uint8_t)a8,
                    0xFF, 0xFF, 0xFF);

                /* draw ring (outer circle minus inner circle) */
                int thickness = 2 + (int)(3.0f * (1.0f - t));
                for (int dy = -r - thickness; dy <= r + thickness; dy++) {
                    int py = cy + dy;
                    if (py < 0 || py >= (int)height) continue;
                    for (int dx = -r - thickness; dx <= r + thickness; dx++) {
                        int px = cx + dx;
                        if (px < 0 || px >= (int)width) continue;
                        int dist_sq = dx * dx + dy * dy;
                        int inner = (r - thickness) * (r - thickness);
                        int outer = (r + thickness) * (r + thickness);
                        if (dist_sq >= inner && dist_sq <= outer) {
                            /* alpha blend */
                            uint32_t dst = pixels[py * width + px];
                            uint32_t dr = ((dst >> 16) & 0xFF);
                            uint32_t dg = ((dst >> 8) & 0xFF);
                            uint32_t db = (dst & 0xFF);
                            uint32_t inv = 255 - a8;
                            uint32_t or_ = (0xFF * a8 + dr * inv) / 255;
                            uint32_t og = (0xFF * a8 + dg * inv) / 255;
                            uint32_t ob = (0xFF * a8 + db * inv) / 255;
                            if (or_ > 255) or_ = 255;
                            if (og > 255) og = 255;
                            if (ob > 255) ob = 255;
                            pixels[py * width + px] = 0xFF000000 |
                                (or_ << 16) | (og << 8) | ob;
                        }
                    }
                }
            } else {
                client->ripple_active = false;
            }
        }
        return;
    case LUMO_SHELL_MODE_SIDEBAR:
        lumo_draw_sidebar(pixels, width, height, client, active_target, visibility);
        return;
    default:
        break;
    }
}

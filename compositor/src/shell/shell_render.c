/*
 * shell_render.c — theme system and all rendering functions for the
 * Lumo shell client.  Split out of shell_client.c for maintainability.
 */

#include "shell_client_internal.h"
#include "lumo/version.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── global theme definition ──────────────────────────────────────── */

struct lumo_shell_theme lumo_theme;

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
static void lumo_draw_animated_bg(
    uint32_t *pixels, uint32_t width, uint32_t height,
    int weather_code);

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
    const uint32_t close_fill = lumo_argb(0xFF, 0x3B, 0x1F, 0x34);
    const uint32_t close_label = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
    const uint32_t accent_colors[] = {
        lumo_argb(0xFF, 0xE9, 0x54, 0x20),
        lumo_argb(0xFF, 0x77, 0x21, 0x6F),
        lumo_argb(0xFF, 0xE9, 0x54, 0x20),
        lumo_argb(0xFF, 0x77, 0x21, 0x6F),
    };
    struct lumo_rect panel_rect = {0};
    struct lumo_rect accent_rect = {0};
    struct lumo_rect title_badge = {0};
    struct lumo_rect close_rect = {0};
    struct lumo_rect close_label_rect = {0};
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

    if (lumo_shell_launcher_close_rect(width, height, &close_rect)) {
        bool close_active = active_target != NULL &&
            active_target->kind == LUMO_SHELL_TARGET_LAUNCHER_CLOSE;

        close_rect.y += slide_y;
        close_label_rect = close_rect;
        lumo_fill_rounded_rect(pixels, width, height, &close_rect, 14,
            close_active ? highlight : close_fill);
        lumo_draw_outline(pixels, width, height, &close_rect, 1,
            close_active ? highlight : panel_stroke);
    lumo_draw_text_centered(pixels, width, height, &close_label_rect, 3,
            close_label, "X");
    }

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

    for (uint32_t visible_index = 0;
            visible_index < (uint32_t)lumo_shell_launcher_filtered_tile_count(
                query);
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

        {
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
        }

        label_rect.x = tile_rect.x;
        label_rect.y = cy + 56 + 8;
        label_rect.width = tile_rect.width;
        label_rect.height = 20;
        lumo_draw_text_centered(pixels, width, height, &label_rect, 2,
            active ? highlight : subtitle_color,
            label != NULL ? label : "APP");
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
        /* cache wifi bars — re-read /proc at most once per 5 seconds */
        static int cached_wifi_bars = 0;
        static uint64_t wifi_last_read = 0;
        uint64_t now_ms = (uint64_t)now;
        if (now_ms != wifi_last_read &&
                (wifi_last_read == 0 || now_ms >= wifi_last_read + 5)) {
            wifi_last_read = now_ms;
            cached_wifi_bars = 0;
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
                            if (signal > -50) cached_wifi_bars = 4;
                            else if (signal > -60) cached_wifi_bars = 3;
                            else if (signal > -70) cached_wifi_bars = 2;
                            else cached_wifi_bars = 1;
                        } else {
                            if (quality > 50) cached_wifi_bars = 4;
                            else if (quality > 35) cached_wifi_bars = 3;
                            else if (quality > 20) cached_wifi_bars = 2;
                            else if (quality > 0) cached_wifi_bars = 1;
                        }
                        break;
                    }
                }
                fclose(wfp);
            }
        }
        lumo_draw_wifi_bars(pixels, width, height,
            (int)width - 42, bar_height / 2 - 8, cached_wifi_bars,
            accent_color, wifi_dim);
    }
}

/* ── theme update ─────────────────────────────────────────────────── */

/* ── continuous theme engine ──────────────────────────────────────── */

/* key-color stops at specific hours — the engine interpolates smoothly
 * between adjacent stops using the fractional time of day.  This gives
 * a gradual sunrise/sunset/night shift rather than an abrupt change. */
struct lumo_color_stop { float hour; float r, g, b; };
static const struct lumo_color_stop lumo_day_stops[] = {
    {  0.0f, 0x12, 0x08, 0x1A }, /* midnight — deep aubergine         */
    {  4.0f, 0x10, 0x0C, 0x20 }, /* pre-dawn — slight blue lift       */
    {  5.5f, 0x14, 0x28, 0x38 }, /* dawn — cool teal                  */
    {  7.0f, 0x30, 0x10, 0x28 }, /* early morning — warm rose          */
    { 10.0f, 0x2C, 0x00, 0x1E }, /* mid-morning — pure aubergine       */
    { 13.0f, 0x2C, 0x00, 0x1E }, /* midday — hold aubergine            */
    { 15.0f, 0x28, 0x14, 0x18 }, /* afternoon — dusty warm             */
    { 17.0f, 0x42, 0x0C, 0x16 }, /* late afternoon — sunset orange     */
    { 19.0f, 0x30, 0x0A, 0x22 }, /* dusk — purple                      */
    { 20.5f, 0x10, 0x18, 0x30 }, /* twilight — deep blue               */
    { 22.0f, 0x12, 0x08, 0x1A }, /* night — back to aubergine          */
    { 24.0f, 0x12, 0x08, 0x1A }, /* wrap to midnight                   */
};
#define LUMO_DAY_STOP_COUNT \
    (sizeof(lumo_day_stops) / sizeof(lumo_day_stops[0]))

/* smooth interpolation state — current color approaches target */
static float lumo_smooth_r, lumo_smooth_g, lumo_smooth_b;
static bool lumo_smooth_initialized;

static void lumo_theme_interpolate_time(float fractional_hour,
    float *out_r, float *out_g, float *out_b)
{
    /* find the two stops surrounding the current time */
    size_t i;
    for (i = 1; i < LUMO_DAY_STOP_COUNT; i++) {
        if (fractional_hour < lumo_day_stops[i].hour)
            break;
    }
    if (i >= LUMO_DAY_STOP_COUNT) i = LUMO_DAY_STOP_COUNT - 1;

    const struct lumo_color_stop *a = &lumo_day_stops[i - 1];
    const struct lumo_color_stop *b = &lumo_day_stops[i];
    float span = b->hour - a->hour;
    float t = (span > 0.0f) ? (fractional_hour - a->hour) / span : 0.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    /* smoothstep for a more natural curve */
    t = t * t * (3.0f - 2.0f * t);

    *out_r = a->r + (b->r - a->r) * t;
    *out_g = a->g + (b->g - a->g) * t;
    *out_b = a->b + (b->b - a->b) * t;
}

static void lumo_theme_apply_weather(int weather_code,
    float *r, float *g, float *b)
{
    switch (weather_code) {
    case 1: /* partly cloudy */
        *g += 6.0f; *b += 10.0f; break;
    case 2: /* overcast */
        *r = (*r * 3.0f + 0x30) / 4.0f;
        *g = (*g * 3.0f + 0x28) / 4.0f;
        *b = (*b * 3.0f + 0x28) / 4.0f;
        break;
    case 3: /* rain */
        *r = *r * 2.0f / 3.0f; *g += 8.0f; *b += 26.0f; break;
    case 4: /* storm */
        *r = *r / 2.0f + 8.0f; *g = *g / 2.0f; *b += 36.0f; break;
    case 5: /* snow */
        *r += 20.0f; *g += 26.0f; *b += 36.0f; break;
    case 6: /* fog */
        *r = (*r + 0x28) / 2.0f;
        *g = (*g + 0x24) / 2.0f;
        *b = (*b + 0x22) / 2.0f;
        break;
    default: break;
    }
    if (*r > 255.0f) *r = 255.0f;
    if (*g > 255.0f) *g = 255.0f;
    if (*b > 255.0f) *b = 255.0f;
}

static void lumo_theme_derive_ui(uint32_t r, uint32_t g, uint32_t b) {
    lumo_theme.base_r = r;
    lumo_theme.base_g = g;
    lumo_theme.base_b = b;

    lumo_theme.bar_top = lumo_argb(0xE0, (uint8_t)(r + 0x10 > 0xFF ? 0xFF : r + 0x10),
        (uint8_t)g, (uint8_t)(b + 0x06 > 0xFF ? 0xFF : b + 0x06));
    lumo_theme.bar_bottom = lumo_argb(0xE0, (uint8_t)r, (uint8_t)g, (uint8_t)b);
    lumo_theme.panel_bg = lumo_argb(0xF0, (uint8_t)(r + 0x08 > 0xFF ? 0xFF : r + 0x08),
        (uint8_t)(g + 0x04 > 0xFF ? 0xFF : g + 0x04),
        (uint8_t)(b + 0x06 > 0xFF ? 0xFF : b + 0x06));
    lumo_theme.panel_stroke = lumo_argb(0x60,
        (uint8_t)(r + 0x30 > 0xFF ? 0xFF : r + 0x30),
        (uint8_t)(g + 0x18 > 0xFF ? 0xFF : g + 0x18),
        (uint8_t)(b + 0x28 > 0xFF ? 0xFF : b + 0x28));
    lumo_theme.tile_fill = lumo_argb(0xFF, (uint8_t)(r + 0x0A > 0xFF ? 0xFF : r + 0x0A),
        (uint8_t)(g + 0x08 > 0xFF ? 0xFF : g + 0x08),
        (uint8_t)(b + 0x0A > 0xFF ? 0xFF : b + 0x0A));
    lumo_theme.tile_stroke = lumo_argb(0xFF,
        (uint8_t)(r + 0x20 > 0xFF ? 0xFF : r + 0x20),
        (uint8_t)(g + 0x14 > 0xFF ? 0xFF : g + 0x14),
        (uint8_t)(b + 0x1C > 0xFF ? 0xFF : b + 0x1C));
    lumo_theme.accent = lumo_argb(0xFF, 0xE9, 0x54, 0x20);
    lumo_theme.text_primary = lumo_argb(0xFF, 0xFF, 0xFF, 0xFF);
    lumo_theme.text_secondary = lumo_argb(0xFF, 0xAE, 0xA7, 0x9F);
    lumo_theme.dim = lumo_argb(0x40,
        (uint8_t)(r + 0x30 > 0xFF ? 0xFF : r + 0x30),
        (uint8_t)(g + 0x18 > 0xFF ? 0xFF : g + 0x18),
        (uint8_t)(b + 0x28 > 0xFF ? 0xFF : b + 0x28));
}

void lumo_theme_update(int weather_code) {
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    float fractional_hour = (float)tm_now.tm_hour +
        (float)tm_now.tm_min / 60.0f;

    /* compute target color from time-of-day interpolation */
    float target_r, target_g, target_b;
    lumo_theme_interpolate_time(fractional_hour,
        &target_r, &target_g, &target_b);

    /* apply weather tint to target */
    lumo_theme_apply_weather(weather_code,
        &target_r, &target_g, &target_b);

    /* initialize smooth state on first call */
    if (!lumo_smooth_initialized) {
        lumo_smooth_r = target_r;
        lumo_smooth_g = target_g;
        lumo_smooth_b = target_b;
        lumo_smooth_initialized = true;
    }

    /* exponential approach: move 8% of the way toward target each call.
     * at 5fps background refresh, this gives a ~3 second visual blend
     * for time-of-day shifts and ~1 second for weather changes. */
    float blend = 0.08f;
    lumo_smooth_r += (target_r - lumo_smooth_r) * blend;
    lumo_smooth_g += (target_g - lumo_smooth_g) * blend;
    lumo_smooth_b += (target_b - lumo_smooth_b) * blend;

    uint32_t r = (uint32_t)(lumo_smooth_r + 0.5f);
    uint32_t g = (uint32_t)(lumo_smooth_g + 0.5f);
    uint32_t b = (uint32_t)(lumo_smooth_b + 0.5f);
    if (r > 0xFF) r = 0xFF;
    if (g > 0xFF) g = 0xFF;
    if (b > 0xFF) b = 0xFF;

    lumo_theme.hour = (uint32_t)tm_now.tm_hour;
    lumo_theme.weather_code = weather_code;

    lumo_theme_derive_ui(r, g, b);
}

/* ── bokeh ball system ────────────────────────────────────────────── */

#define BOKEH_COUNT 10

struct bokeh_ball {
    /* base position as fraction of screen (0.0-1.0) */
    float base_x;
    float base_y;
    /* drift speed in fractions-per-frame */
    float drift_x;
    float drift_y;
    /* radius as fraction of screen height */
    float radius_frac;
    /* peak alpha (0-255) */
    uint8_t alpha;
};

/* deterministic particle set — hand-tuned for visual balance
 * drift values scaled for 15fps frame counter */
static const struct bokeh_ball bokeh_particles[BOKEH_COUNT] = {
    { 0.12f, 0.18f,  0.00027f,  0.00010f, 0.08f, 32 },
    { 0.85f, 0.25f, -0.00017f,  0.00013f, 0.11f, 24 },
    { 0.42f, 0.72f,  0.00020f, -0.00007f, 0.06f, 40 },
    { 0.68f, 0.10f, -0.00010f,  0.00020f, 0.14f, 18 },
    { 0.25f, 0.55f,  0.00013f,  0.00017f, 0.05f, 48 },
    { 0.90f, 0.80f, -0.00023f, -0.00010f, 0.09f, 28 },
    { 0.55f, 0.40f,  0.00007f, -0.00013f, 0.07f, 36 },
    { 0.08f, 0.88f,  0.00020f, -0.00017f, 0.10f, 22 },
    { 0.72f, 0.60f, -0.00013f,  0.00007f, 0.13f, 16 },
    { 0.35f, 0.15f,  0.00017f,  0.00020f, 0.04f, 52 },
    { 0.50f, 0.90f, -0.00010f, -0.00020f, 0.08f, 30 },
    { 0.18f, 0.42f,  0.00023f,  0.00003f, 0.06f, 44 },
    { 0.78f, 0.35f, -0.00020f,  0.00017f, 0.12f, 20 },
    { 0.60f, 0.75f,  0.00010f, -0.00010f, 0.05f, 50 },
    { 0.30f, 0.85f, -0.00007f,  0.00013f, 0.09f, 26 },
    { 0.95f, 0.50f, -0.00017f, -0.00007f, 0.07f, 38 },
    { 0.15f, 0.65f,  0.00013f,  0.00010f, 0.11f, 20 },
    { 0.48f, 0.22f, -0.00010f,  0.00017f, 0.06f, 42 },
};

/* alpha-blend a single pixel: src over dst.
 * Uses (x * 257 + 256) >> 16 ≈ x / 255 to avoid per-pixel division. */
static inline uint32_t lumo_blend_pixel(uint32_t dst, uint32_t src_rgb,
                                        uint8_t alpha)
{
    if (alpha == 0) return dst;
    uint32_t a  = alpha;
    uint32_t ia = 255 - a;
    /* blend RB channels together, then G separately — avoids 3 multiplies */
    uint32_t dst_rb = dst & 0x00FF00FF;
    uint32_t dst_g  = dst & 0x0000FF00;
    uint32_t src_rb = src_rgb & 0x00FF00FF;
    uint32_t src_g  = src_rgb & 0x0000FF00;
    uint32_t rb = (src_rb * a + dst_rb * ia + 0x00800080) >> 8;
    uint32_t g  = (src_g  * a + dst_g  * ia + 0x00008000) >> 8;
    return 0xFF000000 | (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

/* draw one soft bokeh ball with radial alpha falloff */
static void lumo_draw_bokeh_ball(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    int cx,
    int cy,
    int radius,
    uint32_t tint_rgb,
    uint8_t peak_alpha
) {
    int x0 = cx - radius;
    int y0 = cy - radius;
    int x1 = cx + radius;
    int y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)width)  x1 = (int)width  - 1;
    if (y1 >= (int)height) y1 = (int)height - 1;

    int r2 = radius * radius;
    if (r2 == 0) return;

    /* pre-compute reciprocal to avoid per-pixel division */
    uint32_t inv_r2 = r2 > 0 ? (255u * 65536u) / (uint32_t)r2 : 0;

    for (int y = y0; y <= y1; y++) {
        int dy = y - cy;
        int dy2 = dy * dy;
        uint32_t *row = pixels + y * width;
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx;
            int dist2 = dx * dx + dy2;
            if (dist2 >= r2) continue;

            /* smooth radial falloff: alpha = peak * (1 - (d/r)^2)^2
             * Use pre-computed reciprocal: frac = dist2 * 255 / r2
             * approximated as (dist2 * inv_r2) >> 16 */
            uint32_t frac = ((uint32_t)dist2 * inv_r2) >> 16;
            if (frac > 255) frac = 255;
            uint32_t inv  = 255 - frac;
            uint32_t a    = ((uint32_t)peak_alpha * inv * inv) >> 16;
            if (a == 0) continue;
            if (a > 255) a = 255;

            row[x] = lumo_blend_pixel(row[x], tint_rgb, (uint8_t)a);
        }
    }
}

/* render all bokeh balls onto the background buffer */
static void lumo_draw_bokeh_layer(
    uint32_t *pixels,
    uint32_t width,
    uint32_t height,
    uint32_t frame,
    uint32_t base_r,
    uint32_t base_g,
    uint32_t base_b
) {
    /* bokeh tint: lighter version of the base palette */
    uint32_t tr = base_r + 0x3A > 0xFF ? 0xFF : base_r + 0x3A;
    uint32_t tg = base_g + 0x2A > 0xFF ? 0xFF : base_g + 0x2A;
    uint32_t tb = base_b + 0x3E > 0xFF ? 0xFF : base_b + 0x3E;
    uint32_t tint = (tr << 16) | (tg << 8) | tb;

    for (int i = 0; i < BOKEH_COUNT; i++) {
        const struct bokeh_ball *b = &bokeh_particles[i];

        /* slow drift — wraps via fmod-style math */
        float fx = b->base_x + b->drift_x * (float)frame;
        float fy = b->base_y + b->drift_y * (float)frame;

        /* wrap to [0, 1) */
        fx = fx - (float)(int)fx;
        fy = fy - (float)(int)fy;
        if (fx < 0.0f) fx += 1.0f;
        if (fy < 0.0f) fy += 1.0f;

        int cx = (int)(fx * (float)width);
        int cy = (int)(fy * (float)height);
        int radius = (int)(b->radius_frac * (float)height);
        if (radius < 3) radius = 3;
        if (radius > 50) radius = 50; /* cap for riscv64 perf */

        lumo_draw_bokeh_ball(pixels, width, height, cx, cy, radius,
                             tint, b->alpha);
    }
}

/* ── multi-core thread pool for background rendering ──────────────── */

#define LUMO_BG_THREADS 8

struct lumo_bg_stripe {
    uint32_t *pixels;
    uint32_t width;
    uint32_t frame;
    uint32_t y_start;
    uint32_t y_end;
    const uint32_t *row_cache;
};

static struct {
    pthread_t threads[LUMO_BG_THREADS];
    struct lumo_bg_stripe tasks[LUMO_BG_THREADS];
    pthread_barrier_t barrier;
    pthread_mutex_t start_mutex;
    pthread_cond_t start_cond;
    bool started;
    bool shutdown;
    bool initialized;
} bg_pool;

static void *bg_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    for (;;) {
        pthread_mutex_lock(&bg_pool.start_mutex);
        while (!bg_pool.started && !bg_pool.shutdown)
            pthread_cond_wait(&bg_pool.start_cond, &bg_pool.start_mutex);
        pthread_mutex_unlock(&bg_pool.start_mutex);

        if (bg_pool.shutdown) break;

        /* render our stripe */
        struct lumo_bg_stripe *t = &bg_pool.tasks[id];
        for (uint32_t y = t->y_start; y < t->y_end; y++) {
            uint32_t row_color = y < 2048 ? t->row_cache[y] :
                t->row_cache[2047];
            uint32_t *row_ptr = t->pixels + y * t->width;
            uint32_t phase = (y * 3 + t->frame * 2) % 512;
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
                row_color = lumo_argb(0xFF, (uint8_t)r, (uint8_t)g,
                    (uint8_t)b);
            }

            lumo_fill_span(row_ptr, (int)t->width, row_color);

            if (glow > 12) {
                uint32_t streak_x = (t->frame + y * 2) %
                    (t->width + 200);
                if (streak_x < t->width) {
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
                    if (end > t->width) end = t->width;
                    lumo_fill_span(row_ptr + streak_x,
                        (int)(end - streak_x), streak_color);
                }
            }
        }

        /* wait for all stripes to complete */
        pthread_barrier_wait(&bg_pool.barrier);
    }
    return NULL;
}

static void bg_pool_init(void) {
    if (bg_pool.initialized) return;
    pthread_barrier_init(&bg_pool.barrier, NULL, LUMO_BG_THREADS + 1);
    pthread_mutex_init(&bg_pool.start_mutex, NULL);
    pthread_cond_init(&bg_pool.start_cond, NULL);
    bg_pool.started = false;
    bg_pool.shutdown = false;
    for (int i = 0; i < LUMO_BG_THREADS; i++) {
        pthread_create(&bg_pool.threads[i], NULL, bg_worker,
            (void *)(intptr_t)i);
    }
    bg_pool.initialized = true;
    fprintf(stderr, "lumo-shell: background thread pool started "
        "(%d workers)\n", LUMO_BG_THREADS);
}

/* dispatch row stripes to the thread pool and wait for completion */
static void bg_parallel_fill(uint32_t *pixels, uint32_t width,
    uint32_t height, uint32_t frame, const uint32_t *row_cache)
{
    if (!bg_pool.initialized) bg_pool_init();

    uint32_t stripe_h = height / LUMO_BG_THREADS;
    for (int i = 0; i < LUMO_BG_THREADS; i++) {
        bg_pool.tasks[i].pixels = pixels;
        bg_pool.tasks[i].width = width;
        bg_pool.tasks[i].frame = frame;
        bg_pool.tasks[i].row_cache = row_cache;
        bg_pool.tasks[i].y_start = (uint32_t)i * stripe_h;
        bg_pool.tasks[i].y_end = (i == LUMO_BG_THREADS - 1)
            ? height : (uint32_t)(i + 1) * stripe_h;
    }

    /* wake all workers */
    pthread_mutex_lock(&bg_pool.start_mutex);
    bg_pool.started = true;
    pthread_cond_broadcast(&bg_pool.start_cond);
    pthread_mutex_unlock(&bg_pool.start_mutex);

    /* wait for all stripes to complete */
    pthread_barrier_wait(&bg_pool.barrier);

    /* reset for next frame */
    pthread_mutex_lock(&bg_pool.start_mutex);
    bg_pool.started = false;
    pthread_mutex_unlock(&bg_pool.start_mutex);
}

/* ── background row cache + animated background ───────────────────── */

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
    frame = (uint32_t)(mono_ts.tv_sec * 15 + mono_ts.tv_nsec / 66666666);

    wall_now = time(NULL);
    localtime_r(&wall_now, &tm_now);
    hour = (uint32_t)tm_now.tm_hour;

    /* palette — always computed (needed for bokeh tint even on cache hit) */
    uint32_t base_r, base_g, base_b;

    /* Multi-palette time-of-day colors:
     * Ubuntu (aubergine/orange), Sailfish (teal/petrol blue),
     * webOS (warm charcoal/slate) blended by time period.
     *
     * Dawn/morning  : Sailfish teal-blue -> Ubuntu warm purple
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

    /* rebuild row gradient cache when palette or size changes */
    if (hour != bg_cache_hour || height != bg_cache_height ||
            weather_code != bg_cache_weather_code) {
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

    /* multi-core parallel background fill — splits rows across 8
     * worker threads for ~6-7x speedup on the 8-core RISC-V CPU.
     * Each thread fills an independent horizontal stripe. */
    bg_parallel_fill(pixels, width, height, frame, bg_row_cache);

    /* bokeh balls — soft depth-of-field circles over the gradient */
    lumo_draw_bokeh_layer(pixels, width, height, frame,
                          base_r, base_g, base_b);
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
            client->mode != LUMO_SHELL_MODE_LAUNCHER) {
        lumo_clear_pixels(pixels, width, height);
    }

    /* update shared theme colors from time-of-day + weather */
    lumo_theme_update(client->weather_code);

    visibility = client->mode == LUMO_SHELL_MODE_GESTURE
        || client->mode == LUMO_SHELL_MODE_STATUS
        || client->mode == LUMO_SHELL_MODE_BACKGROUND
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

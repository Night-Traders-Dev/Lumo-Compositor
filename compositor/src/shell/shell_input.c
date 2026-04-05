/*
 * shell_input.c — touch/pointer input handlers for lumo-shell.
 *
 * Extracted from shell_client.c: hit testing, target management,
 * pointer listeners, and touch listeners.
 */
#include "shell_client_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── hit testing and target management ────────────────────────────── */

void lumo_shell_client_set_active_target(
    struct lumo_shell_client *client,
    const struct lumo_shell_target *target
) {
    if (client == NULL || target == NULL) {
        return;
    }

    client->active_target = *target;
    client->active_target_valid = true;
    if (client->unified) {
        lumo_shell_client_redraw_unified(client);
    } else {
        (void)lumo_shell_client_redraw(client);
    }
}

void lumo_shell_client_clear_active_target(
    struct lumo_shell_client *client
) {
    if (client == NULL) {
        return;
    }

    client->active_target_valid = false;
    memset(&client->active_target, 0, sizeof(client->active_target));
    if (client->unified) {
        lumo_shell_client_redraw_unified(client);
    } else {
        (void)lumo_shell_client_redraw(client);
    }
}

void lumo_shell_client_note_target(
    struct lumo_shell_client *client,
    double x,
    double y
) {
    struct lumo_shell_target target = {0};

    if (client == NULL || client->configured_width == 0 ||
            client->configured_height == 0) {
        return;
    }

    if (lumo_shell_target_for_mode_with_query(client->mode,
            client->configured_width, client->configured_height,
            client->mode == LUMO_SHELL_MODE_LAUNCHER
                ? client->toast_message
                : NULL,
            client->launcher_page,
            x, y, &target)) {
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

/* returns: 1=reload, 2=rotate, 3=screenshot, 4=vol_slider, 5=bri_slider */
int lumo_shell_status_button_hit(
    const struct lumo_shell_client *client,
    double x,
    double y
) {
    struct lumo_rect panel = {0};
    int bar_h;
    struct lumo_rect button_rect = {0};

    if (client == NULL || !client->compositor_quick_settings_visible ||
            !lumo_shell_quick_settings_panel_rect(client->configured_width,
                client->configured_height, &panel)) {
        return 0;
    }

    bar_h = 48; /* must match drawing code bar_h */

    /* layout offsets — must match the drawing code above:
     * title(12) + sep(24) + wifi(10+22) + display(22) + session(22) +
     * device(22) + sep(28) + vol_label(12) + vol_track(20) +
     * bri_label(32) + bri_track(28) + sep(28) + buttons(10) */
    int base_y = bar_h + 4;
    int track_x = panel.x + 16;
    int track_w = panel.width - 32;

    /* volume slider zone: row starts at base + 12+24+10+22+22+22+28+12 */
    int vol_y = base_y + 12 + 24 + 10 + 22 + 22 + 22 + 28 + 6;
    if (y >= vol_y && y <= vol_y + 20 &&
            x >= track_x && x <= track_x + track_w) {
        return 4;
    }

    /* brightness slider zone: vol + 32 */
    int bri_y = vol_y + 32;
    if (y >= bri_y && y <= bri_y + 20 &&
            x >= track_x && x <= track_x + track_w) {
        return 5;
    }

    if (lumo_shell_quick_settings_button_rect(client->configured_width,
            client->configured_height, 0, &button_rect) &&
            lumo_rect_contains(&button_rect, x, y)) {
        return 1;
    }
    if (lumo_shell_quick_settings_button_rect(client->configured_width,
            client->configured_height, 1, &button_rect) &&
            lumo_rect_contains(&button_rect, x, y)) {
        return 2;
    }
    if (lumo_shell_quick_settings_button_rect(client->configured_width,
            client->configured_height, 2, &button_rect) &&
            lumo_rect_contains(&button_rect, x, y)) {
        return 3;
    }
    if (lumo_shell_quick_settings_button_rect(client->configured_width,
            client->configured_height, 3, &button_rect) &&
            lumo_rect_contains(&button_rect, x, y)) {
        return 6; /* settings button */
    }
    return 0;
}

uint32_t lumo_shell_slider_pct_from_touch(
    const struct lumo_shell_client *client,
    double x
) {
    struct lumo_rect panel = {0};
    int track_x;
    int track_w;
    int rel;

    if (client == NULL ||
            !lumo_shell_quick_settings_panel_rect(client->configured_width,
                client->configured_height, &panel)) {
        return 0;
    }

    track_x = panel.x + 16;
    track_w = panel.width - 32;
    rel = (int)x - track_x;
    if (rel < 0) rel = 0;
    if (rel > track_w) rel = track_w;
    return (uint32_t)(rel * 100 / track_w);
}

void lumo_shell_client_activate_target(struct lumo_shell_client *client) {
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

/* ── pointer handlers ─────────────────────────────────────────────── */

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
    if (client == NULL) {
        return;
    }

    if (client->unified) {
        for (int i = 0; i < client->surface_count; i++) {
            struct lumo_shell_surface_slot *slot = &client->slots[i];
            if (slot->surface == surface) {
                client->mode = slot->mode;
                client->configured_width = slot->configured_width;
                client->configured_height = slot->configured_height;
                break;
            }
        }
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

const struct wl_pointer_listener lumo_shell_pointer_listener = {
    .enter = lumo_shell_pointer_handle_enter,
    .leave = lumo_shell_pointer_handle_leave,
    .motion = lumo_shell_pointer_handle_motion,
    .button = lumo_shell_pointer_handle_button,
    .axis = lumo_shell_pointer_handle_axis,
    .frame = lumo_shell_pointer_handle_frame,
};

/* ── touch handlers ───────────────────────────────────────────────── */

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

    fprintf(stderr, "lumo-shell: touch down mode=%s x=%.0f y=%.0f\n",
        lumo_shell_mode_name(client->mode),
        wl_fixed_to_double(x), wl_fixed_to_double(y));

    client->touch_pressed = true;
    client->active_touch_id = id;
    client->pointer_x = wl_fixed_to_double(x);
    client->pointer_y = wl_fixed_to_double(y);

    /* in unified mode, resolve which slot's surface was touched and
     * swap in that slot's mode/dimensions for target detection */
    if (client->unified) {
        for (int i = 0; i < client->surface_count; i++) {
            struct lumo_shell_surface_slot *slot = &client->slots[i];
            if (slot->surface == surface) {
                client->mode = slot->mode;
                client->configured_width = slot->configured_width;
                client->configured_height = slot->configured_height;
                break;
            }
        }
    }

    if (client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            !client->compositor_launcher_visible &&
            (client->compositor_quick_settings_visible ||
                client->compositor_time_panel_visible)) {
        lumo_shell_client_clear_active_target(client);
    } else {
        lumo_shell_client_note_target(client, client->pointer_x,
            client->pointer_y);
    }

    /* record press start time for sidebar long-press detection */
    if (client->mode == LUMO_SHELL_MODE_SIDEBAR)
        client->sidebar_press_start_msec = lumo_now_msec();

    /* record swipe start for launcher page navigation */
    if (client->mode == LUMO_SHELL_MODE_LAUNCHER)
        client->launcher_swipe_x = client->pointer_x;

    /* trigger touch ripple on the surface that received the touch */
    client->ripple_x = client->pointer_x;
    client->ripple_y = client->pointer_y;
    client->ripple_start_msec = lumo_now_msec();
    client->ripple_active = true;
    client->ripple_mode = client->mode;
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
            lumo_shell_client_send_capture_screenshot(client);
            return;
        }
        if (btn == 4) {
            uint32_t pct = lumo_shell_slider_pct_from_touch(client,
                client->pointer_x);
            lumo_shell_send_set_u32(client, "set_volume", "pct", pct);
            return;
        }
        if (btn == 5) {
            uint32_t pct = lumo_shell_slider_pct_from_touch(client,
                client->pointer_x);
            lumo_shell_send_set_u32(client, "set_brightness", "pct", pct);
            return;
        }
        if (btn == 6) {
            /* open settings app — send activate_target for settings tile */
            struct lumo_shell_protocol_frame frame;
            if (lumo_shell_protocol_frame_init(&frame,
                    LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "activate_target",
                    client->next_request_id++)) {
                lumo_shell_protocol_frame_add_string(&frame, "kind",
                    lumo_shell_target_kind_name(
                        LUMO_SHELL_TARGET_LAUNCHER_TILE));
                lumo_shell_protocol_frame_add_u32(&frame, "index", 11);
                lumo_shell_protocol_frame_add_string(&frame, "mode",
                    "launcher");
                (void)lumo_shell_client_send_frame(client, &frame);
            }
            fprintf(stderr, "lumo-shell: settings button pressed\n");
            return;
        }
    }

    /* search bar tap — show keyboard when tapping search area */
    if (client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            client->compositor_launcher_visible) {
        struct lumo_rect search_rect = {0};

        if (lumo_shell_launcher_search_bar_rect(client->configured_width,
                client->configured_height, &search_rect) &&
                lumo_rect_contains(&search_rect, client->pointer_x,
                    client->pointer_y)) {
            struct lumo_shell_protocol_frame kf;
            if (lumo_shell_protocol_frame_init(&kf,
                    LUMO_SHELL_PROTOCOL_FRAME_REQUEST, "set_keyboard_visible",
                    client->next_request_id++)) {
                lumo_shell_protocol_frame_add_bool(&kf, "visible", true);
                (void)lumo_shell_client_send_frame(client, &kf);
            }
            return;
        }
    }

    /* sidebar interactions — handle locally instead of generic activate */
    if (client->mode == LUMO_SHELL_MODE_SIDEBAR &&
            client->active_target_valid) {
        uint64_t now = lumo_now_msec();
        uint64_t held = now - client->sidebar_press_start_msec;

        if (client->active_target.kind == LUMO_SHELL_TARGET_SIDEBAR_DRAWER_BTN) {
            lumo_shell_client_send_open_drawer(client);
            lumo_shell_client_clear_active_target(client);
            return;
        }

        if (client->active_target.kind == LUMO_SHELL_TARGET_SIDEBAR_APP) {
            uint32_t idx = client->active_target.index;

            if (client->sidebar_context_menu_visible) {
                /* context menu: check which row was tapped */
                uint32_t cidx = client->sidebar_context_menu_index;
                const char *aid = client->running_app_ids[cidx];
                bool multi = (strstr(aid, "terminal") != NULL ||
                    strstr(aid, "browser") != NULL);
                double my = client->pointer_y;

                /* compute menu rect matching render code */
                struct lumo_rect icon_rect;
                if (lumo_shell_sidebar_app_rect(client->configured_width,
                        client->configured_height, cidx, &icon_rect)) {
                    int status_h = (int)client->configured_height / 18;
                    if (status_h < 32) status_h = 32;
                    if (status_h > 48) status_h = 48;
                    int menu_h = multi ? 92 : 64;
                    int menu_y = icon_rect.y - menu_h - 4;
                    if (menu_y < status_h + 4) menu_y = status_h + 4;

                    int row = (int)(my - menu_y) / 28;
                    if (row == 0) {
                        /* OPEN */
                        lumo_shell_client_send_focus_app(client, cidx);
                    } else if (multi && row == 1) {
                        /* NEW WINDOW — send new_window request */
                        lumo_shell_send_set_u32(client,
                            "new_window", "index", cidx);
                    } else {
                        /* CLOSE (row 1 if no multi, row 2 if multi) */
                        lumo_shell_client_send_close_app(client, cidx);
                    }
                }
                client->sidebar_context_menu_visible = false;
                lumo_shell_client_clear_active_target(client);
                return;
            }

            if (held > 500) {
                /* long press — show context menu */
                client->sidebar_context_menu_visible = true;
                client->sidebar_context_menu_index = idx;
                lumo_shell_client_clear_active_target(client);
                return;
            }

            /* short tap — focus the app */
            if (idx < client->running_app_count) {
                lumo_shell_client_send_focus_app(client, idx);
            }
            client->sidebar_context_menu_visible = false;
            lumo_shell_client_clear_active_target(client);
            return;
        }

        lumo_shell_client_clear_active_target(client);
        return;
    }

    /* launcher page swipe — horizontal swipe changes page */
    if (client->mode == LUMO_SHELL_MODE_LAUNCHER &&
            client->compositor_launcher_visible) {
        double dx = client->pointer_x - client->launcher_swipe_x;
        uint32_t total_tiles = (uint32_t)lumo_shell_launcher_filtered_tile_count(
            client->search_active ? client->search_query : NULL);
        uint32_t per_page = (uint32_t)lumo_shell_launcher_tile_count();
        int max_page = (int)((total_tiles + per_page - 1) / per_page) - 1;
        if (max_page < 0) max_page = 0;

        if (dx < -40.0) {
            /* swipe left → next page */
            if (client->launcher_page < max_page)
                client->launcher_page++;
            lumo_shell_client_clear_active_target(client);
            if (client->unified)
                lumo_shell_client_redraw_unified(client);
            else
                (void)lumo_shell_client_redraw(client);
            return;
        } else if (dx > 40.0) {
            /* swipe right → previous page */
            if (client->launcher_page > 0)
                client->launcher_page--;
            lumo_shell_client_clear_active_target(client);
            if (client->unified)
                lumo_shell_client_redraw_unified(client);
            else
                (void)lumo_shell_client_redraw(client);
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

    client->pointer_x = wl_fixed_to_double(x);
    client->pointer_y = wl_fixed_to_double(y);
    lumo_shell_client_note_target(client, client->pointer_x,
        client->pointer_y);
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

const struct wl_touch_listener lumo_shell_touch_listener = {
    .down = lumo_shell_touch_handle_down,
    .up = lumo_shell_touch_handle_up,
    .motion = lumo_shell_touch_handle_motion,
    .frame = lumo_shell_touch_handle_frame,
    .cancel = lumo_shell_touch_handle_cancel,
    .shape = lumo_shell_touch_handle_shape,
    .orientation = lumo_shell_touch_handle_orientation,
};
